#include "luau_script.h"

#include <Luau/BytecodeBuilder.h>
#include <Luau/CodeGen.h>
#include <Luau/Compiler.h>
#include <Luau/Lexer.h>
#include <Luau/ParseOptions.h>
#include <Luau/ParseResult.h>
#include <Luau/Parser.h>
#include <Luau/StringUtils.h>
#include <gdextension_interface.h>
#include <lua.h>
#include <cstring>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <string>
#include <utility>

#include "error_strings.h"
#include "gd_luau.h"
#include "luagd_lib.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luagd_variant.h"
#include "luau_analysis.h"
#include "luau_cache.h"
#include "luau_lib.h"
#include "services/luau_interface.h"
#include "services/sandbox_service.h"
#include "utils.h"
#include "wrapped_no_binding.h"

#ifdef TESTS_ENABLED
#include <catch_amalgamated.hpp>
#include <vector>
#endif // TESTS_ENABLED

using namespace godot;

////////////
// SCRIPT //
////////////

bool LuauScript::_has_source_code() const {
    return !source.is_empty();
}

String LuauScript::_get_source_code() const {
    return source;
}

void LuauScript::_set_source_code(const String &p_code) {
    source = p_code;
    source_changed_cache = true;
}

Error LuauScript::load_source_code(const String &p_path) {
    String src;
    Error err = Utils::load_file(p_path, src);
    if (err != OK)
        return err;

    _set_source_code(src);
    return OK;
}

Error LuauScript::compile() {
#define COMPILE_METHOD "LuauScript::compile"

    dependencies.clear();

    // See Luau Compiler.cpp
    CharString src = source.utf8();

    Luau::ParseOptions parse_options;
    parse_options.captureComments = true;

    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseResult parse_result = Luau::Parser::parse(src.get_data(), src.length() + 1, names, allocator, parse_options);
    std::string bytecode;

    Error ret = OK;

    if (parse_result.errors.empty()) {
        try {
            Luau::BytecodeBuilder bcb;
            Luau::compileOrThrow(bcb, parse_result, names, luaGD_compileopts());

            bytecode = bcb.getBytecode();
        } catch (Luau::CompileError &err) {
            ret = ERR_COMPILATION_FAILED;
            error(COMPILE_METHOD, err.what(), err.getLocation().begin.line + 1);
        }
    } else {
        const Luau::ParseError &err = parse_result.errors.front();

        ret = ERR_PARSE_ERROR;
        error(COMPILE_METHOD, err.what(), err.getLocation().begin.line + 1);
    }

    // Ensure everything is reset
    luau_data.allocator.~Allocator();
    new (&luau_data.allocator) Luau::Allocator(std::move(allocator)); // tf?
    luau_data.parse_result = parse_result;
    luau_data.bytecode = bytecode;

    return ret;
}

Error LuauScript::analyze() {
    LuauScriptAnalysisResult analysis_result;
    GDClassDefinition new_definition;

    analysis_result = luascript_analyze(this, source.utf8().get_data(), luau_data.parse_result, new_definition);

    if (analysis_result.error == OK) {
        luau_data.analysis_result = analysis_result;
        definition = new_definition;

        return OK;
    } else {
        luau_data.analysis_result = LuauScriptAnalysisResult();
        definition = GDClassDefinition();

        error("LuauScript::analyze", analysis_result.error_msg, analysis_result.error_line);
        return analysis_result.error;
    }
}

Error LuauScript::finish_load() {
    // Load script.
    Error err = reload_tables();
    if (err != OK)
        return err;

    set_name(definition.name);

    // Update base script.
    base = Ref<LuauScript>(definition.base_script);
    if (base.is_valid()) {
        base->load(LOAD_FULL); // Ensure base script is loaded (i.e. advance from ANALYSIS)

        if (!base->_is_valid())
            return ERR_COMPILATION_FAILED;
    }

    // Build method/constant cache.
    lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_SCRIPT_LOAD);
    LUAU_LOCK(L);
    methods.clear();
    constants.clear();

    lua_getref(L, table_refs[GDLuau::VM_SCRIPT_LOAD]);

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            String key = LuaStackOp<String>::get(L, -2);

            HashMap<StringName, int>::ConstIterator E = definition.constants.find(key);
            if (E) {
                if (LuaStackOp<Variant>::is(L, -1)) {
                    constants.insert(key, LuaStackOp<Variant>::get(L, -1));
                } else {
                    error("LuauScript::finish_load", CONST_NOT_VARIANT_ERR(key), E->value);
                    return ERR_INVALID_DECLARATION;
                }
            }

            if (lua_isfunction(L, -1)) {
                methods.insert(key);
            }
        }

        lua_pop(L, 1); // value
    }

    lua_pop(L, 1); // table

    return OK;
}

Error LuauScript::try_load(lua_State *L, String *r_err) {
    if (luau_data.bytecode.empty()) {
        Error err = compile();
        if (err != OK) {
            if (r_err)
                *r_err = COMPILE_ERR;

            return err;
        }
    }

    String chunkname = "@" + get_path();
    const std::string &bytecode = luau_data.bytecode;

    Error ret = luau_load(L, chunkname.utf8().get_data(), bytecode.data(), bytecode.size(), 0) == 0 ? OK : ERR_COMPILATION_FAILED;
    if (Luau::CodeGen::isSupported())
        Luau::CodeGen::compile(L, -1);

    if (ret != OK) {
        String err = LuaStackOp<String>::get(L, -1);
        lua_pop(L, 1); // error
        error("LuauScript::try_load", err);

        if (r_err)
            *r_err = err;
    }

    return ret;
}

String LuauScript::resolve_path(const String &p_relative_path, String &r_error) const {
    String script_path = get_path();
    if (script_path.is_empty()) {
        r_error = RESOLVE_PATH_PATH_EMPTY_ERR;
        return "";
    }

    return script_path.get_base_dir().path_join(p_relative_path);
}

Error LuauScript::load_table(GDLuau::VMType p_vm_type, bool p_force) {
#define LOAD_DEF_METHOD "LuauScript::load_definition"

    if (table_refs[p_vm_type]) {
        if (!p_force)
            return OK;

        unref_table(p_vm_type);
    }

    // TODO: error line numbers?
    if (_is_loading) {
        error(LOAD_DEF_METHOD, ALREADY_LOADING_ERR, 1);
        return ERR_CYCLIC_LINK;
    }
    _is_loading = true;

    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    LUAU_LOCK(L);

    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    GDThreadData *udata = luaGD_getthreaddata(T);
    udata->script = Ref<LuauScript>(this);

    if (try_load(T) == OK) {
        INIT_TIMEOUT(T)
        int status = lua_resume(T, nullptr, 0);

        if (status == LUA_YIELD || status == LUA_BREAK) {
            error(LOAD_DEF_METHOD, YIELD_DURING_LOAD_ERR, 1);
            lua_pop(L, 1); // thread
            _is_loading = false;
            return ERR_COMPILATION_FAILED;
        } else if (status != LUA_OK) {
            error(LOAD_DEF_METHOD, LuaStackOp<String>::get(T, -1));
            lua_pop(L, 1); // thread
            _is_loading = false;
            return ERR_COMPILATION_FAILED;
        }

        if (luascript_class_table_get_script(T, 1) != this) {
            error(LOAD_DEF_METHOD, INVALID_CLASS_TABLE_ERR, 1);
            lua_pop(L, 1); // thread
            _is_loading = false;
            return ERR_COMPILATION_FAILED;
        }

        table_refs[p_vm_type] = lua_ref(T, 1);

        lua_pop(L, 1); // thread
        _is_loading = false;
        return OK;
    }

    lua_pop(L, 1); // thread
    _is_loading = false;
    return ERR_COMPILATION_FAILED;
}

void LuauScript::unref_table(GDLuau::VMType p_vm) {
    if (!table_refs[p_vm])
        return;

    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm);

    // See ~LuauScriptInstance
    if (L && luaGD_getthreaddata(L)) {
        LUAU_LOCK(L);

        lua_unref(L, table_refs[p_vm]);
        table_refs[p_vm] = 0;
    }
}

Error LuauScript::reload_tables() {
    for (int i = 0; i < GDLuau::VM_MAX; i++) {
        if (i != GDLuau::VM_SCRIPT_LOAD && !table_refs[i])
            continue;

        Error err = load_table(GDLuau::VMType(i), true);
        if (err != OK)
            return err;
    }

    return OK;
}

Error LuauScript::load(LoadStage p_load_stage, bool p_force) {
    int current_stage = 0;

    if (p_force) {
        current_stage = LOAD_NONE;
    } else {
        current_stage = load_stage;

        if (!valid)
            return ERR_COMPILATION_FAILED;
    }

    Error err = OK;

    while (++current_stage <= p_load_stage) {
        switch (current_stage) {
            case LOAD_COMPILE:
                err = compile();
                break;

            case LOAD_ANALYZE:
                if (!_is_module)
                    err = analyze();

                break;

            case LOAD_FULL:
                if (!_is_module)
                    err = finish_load();

                break;

            default:
                break; // unreachable
        }

        if (err != OK) {
            valid = false;
            return err;
        }
    }

    load_stage = p_load_stage;
    valid = true;
    return OK;
}

Error LuauScript::_reload(bool p_keep_state) {
    if (_is_module)
        return OK;

    {
        MutexLock lock(*LuauLanguage::singleton->lock.ptr());
        ERR_FAIL_COND_V(!p_keep_state && instances.size() > 0, ERR_ALREADY_IN_USE);
    }

    return load(LOAD_FULL, true);
}

ScriptLanguage *LuauScript::_get_language() const {
    return LuauLanguage::get_singleton();
}

bool LuauScript::_is_valid() const {
    return valid;
}

bool LuauScript::_can_instantiate() const {
    // TODO: built-in scripting languages check if scripting is enabled OR if this is a tool script
    // Scripting is disabled by default in the editor, check is ~equivalent
    return !_is_module && valid && (_is_tool() || !nb::Engine::get_singleton_nb()->is_editor_hint());
}

bool LuauScript::_is_tool() const {
    return definition.is_tool;
}

StringName LuauScript::_get_instance_base_type() const {
    StringName extends = StringName(definition.extends);

    if (extends != StringName() && nb::ClassDB::get_singleton_nb()->class_exists(extends))
        return extends;

    if (base.is_valid() && base->_is_valid())
        return base->_get_instance_base_type();

    return StringName();
}

Ref<Script> LuauScript::_get_base_script() const {
    return base;
}

bool LuauScript::_inherits_script(const Ref<Script> &p_script) const {
    Ref<LuauScript> script = p_script;
    if (script.is_null())
        return false;

    const LuauScript *s = this;

    while (s) {
        if (s == script.ptr())
            return true;

        s = s->base.ptr();
    }

    return false;
}

StringName LuauScript::_get_global_name() const {
    return definition.name;
}

TypedArray<Dictionary> LuauScript::_get_script_method_list() const {
    TypedArray<Dictionary> methods;

    for (const KeyValue<StringName, GDMethod> &pair : definition.methods)
        methods.push_back(pair.value);

    return methods;
}

bool LuauScript::_has_method(const StringName &p_method) const {
    return has_method(p_method);
}

bool LuauScript::has_method(const StringName &p_method, StringName *r_actual_name) const {
    if (definition.methods.has(p_method))
        return true;

    StringName pascal_name = Utils::to_pascal_case(p_method);

    if (definition.methods.has(pascal_name)) {
        if (r_actual_name)
            *r_actual_name = pascal_name;

        return true;
    }

    return false;
}

Dictionary LuauScript::_get_method_info(const StringName &p_method) const {
    HashMap<StringName, GDMethod>::ConstIterator E = definition.methods.find(p_method);

    if (E)
        return E->value;

    E = definition.methods.find(Utils::to_pascal_case(p_method));

    if (E)
        return E->value;

    return Dictionary();
}

TypedArray<Dictionary> LuauScript::_get_script_property_list() const {
    TypedArray<Dictionary> properties;

    const LuauScript *s = this;

    while (s) {
        // Reverse to add properties from base scripts first.
        for (int i = definition.properties.size() - 1; i >= 0; i--) {
            const GDClassProperty &prop = definition.properties[i];
            properties.push_front(prop.property.operator Dictionary());
        }

        s = s->base.ptr();
    }

    return properties;
}

TypedArray<StringName> LuauScript::_get_members() const {
    TypedArray<StringName> members;

    for (const GDClassProperty &prop : definition.properties)
        members.push_back(prop.property.name);

    return members;
}

bool LuauScript::_has_property_default_value(const StringName &p_property) const {
    HashMap<StringName, uint64_t>::ConstIterator E = definition.property_indices.find(p_property);

    if (E && definition.properties[E->value].default_value != Variant())
        return true;

    if (base.is_valid())
        return base->_has_property_default_value(p_property);

    return false;
}

Variant LuauScript::_get_property_default_value(const StringName &p_property) const {
    HashMap<StringName, uint64_t>::ConstIterator E = definition.property_indices.find(p_property);

    if (E && definition.properties[E->value].default_value != Variant())
        return definition.properties[E->value].default_value;

    if (base.is_valid())
        return base->_get_property_default_value(p_property);

    return Variant();
}

bool LuauScript::has_property(const StringName &p_name) const {
    return definition.property_indices.has(p_name);
}

const GDClassProperty &LuauScript::get_property(const StringName &p_name) const {
    return definition.properties[definition.property_indices[p_name]];
}

bool LuauScript::_has_script_signal(const StringName &p_signal) const {
    return definition.signals.has(p_signal) || (base.is_valid() && base->definition.signals.has(p_signal));
}

TypedArray<Dictionary> LuauScript::_get_script_signal_list() const {
    TypedArray<Dictionary> signals;

    const LuauScript *s = this;

    while (s) {
        for (const KeyValue<StringName, GDMethod> &pair : s->definition.signals)
            signals.push_back(pair.value);

        s = s->base.ptr();
    }

    return signals;
}

Variant LuauScript::_get_rpc_config() const {
    Dictionary rpcs;

    const LuauScript *s = this;

    while (s) {
        for (const KeyValue<StringName, GDRpc> &pair : s->definition.rpcs)
            rpcs[pair.key] = pair.value;

        s = s->base.ptr();
    }

    return rpcs;
}

Dictionary LuauScript::_get_constants() const {
    Dictionary constants_dict;

    for (const KeyValue<StringName, Variant> &pair : constants)
        constants_dict[pair.key] = pair.value;

    return constants_dict;
}

void *LuauScript::_instance_create(Object *p_for_object) const {
    GDLuau::VMType type = GDLuau::VM_USER;

    if (!get_path().is_empty() && (!SandboxService::get_singleton() || SandboxService::get_singleton()->is_core_script(get_path())))
        type = GDLuau::VM_CORE;

    LuauScriptInstance *internal = memnew(LuauScriptInstance(Ref<Script>(this), p_for_object, type));
    return internal::gdextension_interface_script_instance_create2(&LuauScriptInstance::INSTANCE_INFO, internal);
}

bool LuauScript::instance_has(uint64_t p_obj_id) const {
    MutexLock lock(*LuauLanguage::singleton->lock.ptr());
    return instances.has(p_obj_id);
}

bool LuauScript::_instance_has(Object *p_object) const {
    return instance_has(p_object->get_instance_id());
}

LuauScriptInstance *LuauScript::instance_get(uint64_t p_obj_id) const {
    MutexLock lock(*LuauLanguage::singleton->lock.ptr());
    return instances.get(p_obj_id);
}

void LuauScript::def_table_get(lua_State *T) const {
    GDThreadData *udata = luaGD_getthreaddata(T);
    ERR_FAIL_COND_MSG(udata->vm_type >= GDLuau::VM_MAX, THREAD_VM_INVALID_ERR);
    LUAU_LOCK(T);

    int table_ref = table_refs[udata->vm_type];
    if (!table_ref) {
        lua_pushnil(T);
        return;
    }

    lua_getref(T, table_ref);
    lua_insert(T, -2);
    lua_gettable(T, -2);
    lua_remove(T, -2);
}

bool LuauScript::has_dependency(const Ref<LuauScript> &p_script) const {
    return dependencies.has(p_script);
}

bool LuauScript::add_dependency(const Ref<LuauScript> &p_script) {
    dependencies.insert(p_script);
    return !p_script->has_dependency(this);
}

void LuauScript::error(const char *p_method, const String &p_msg, int p_line) const {
    luaGD_gderror(p_method, get_path(), p_msg, p_line);
}

// Based on Luau Repl implementation.
void LuauScript::load_module(lua_State *L) {
    _is_loading = true;

    // Use main thread to avoid inheriting L's environment.
    lua_State *GL = lua_mainthread(L);
    lua_State *ML = lua_newthread(GL);
    luaL_sandboxthread(ML);

    GDThreadData *udata = luaGD_getthreaddata(ML);
    udata->script = Ref<LuauScript>(this);

    lua_xmove(GL, L, 1); // thread

    String err;
    if (try_load(ML, &err) == OK) {
        INIT_TIMEOUT(ML)
        int status = lua_resume(ML, nullptr, 0);

        if (status == LUA_YIELD || status == LUA_BREAK) {
            lua_pushstring(L, MODULE_YIELD_ERR);
            _is_loading = false;
            return;
        } else if (status != LUA_OK) {
            lua_xmove(ML, L, 1); // error
            _is_loading = false;
            return;
        }

        if (lua_gettop(ML) == 0 || (!lua_istable(ML, -1) && !lua_isfunction(L, -1))) {
            lua_pushstring(L, MODULE_RET_ERR);
            _is_loading = false;
            return;
        }

        lua_xmove(ML, L, 1);
        _is_loading = false;
        return;
    }

    LuaStackOp<String>::push(L, err);
    _is_loading = false;
    return;
}

LuauScript::LuauScript() :
        script_list(this) {
    {
        MutexLock lock(*LuauLanguage::get_singleton()->lock.ptr());
        LuauLanguage::get_singleton()->script_list.add(&script_list);
    }
}

LuauScript::~LuauScript() {
    if (GDLuau::get_singleton()) {
        for (int i = 0; i < GDLuau::VM_MAX; i++) {
            if (!table_refs[i])
                continue;

            unref_table(GDLuau::VMType(i));
            table_refs[i] = 0;
        }
    }
}

////////////////////////////
// SCRIPT INSTANCE COMMON //
////////////////////////////

#define COMMON_SELF ((ScriptInstance *)p_self)

void ScriptInstance::init_script_instance_info_common(GDExtensionScriptInstanceInfo2 &p_info) {
    // Must initialize potentially unused struct fields to nullptr
    // (if not, causes segfault on MSVC).
    p_info.property_can_revert_func = nullptr;
    p_info.property_get_revert_func = nullptr;

    p_info.call_func = nullptr;
    p_info.notification_func = nullptr;

    p_info.to_string_func = nullptr;

    p_info.refcount_incremented_func = nullptr;
    p_info.refcount_decremented_func = nullptr;

    p_info.is_placeholder_func = nullptr;

    p_info.set_fallback_func = nullptr;
    p_info.get_fallback_func = nullptr;

    p_info.set_func = [](void *p_self, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) -> GDExtensionBool {
        return COMMON_SELF->set(*(const StringName *)p_name, *(const Variant *)p_value);
    };

    p_info.get_func = [](void *p_self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return COMMON_SELF->get(*(const StringName *)p_name, *(Variant *)r_ret);
    };

    p_info.get_property_list_func = [](void *p_self, uint32_t *r_count) -> const GDExtensionPropertyInfo * {
        return COMMON_SELF->get_property_list(r_count);
    };

    p_info.free_property_list_func = [](void *p_self, const GDExtensionPropertyInfo *p_list) {
        COMMON_SELF->free_property_list(p_list);
    };

    p_info.validate_property_func = [](void *p_self, GDExtensionPropertyInfo *p_property) -> GDExtensionBool {
        return COMMON_SELF->validate_property(p_property);
    };

    p_info.get_owner_func = [](void *p_self) {
        return COMMON_SELF->get_owner()->_owner;
    };

    p_info.get_property_state_func = [](void *p_self, GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
        COMMON_SELF->get_property_state(p_add_func, p_userdata);
    };

    p_info.get_method_list_func = [](void *p_self, uint32_t *r_count) -> const GDExtensionMethodInfo * {
        return COMMON_SELF->get_method_list(r_count);
    };

    p_info.free_method_list_func = [](void *p_self, const GDExtensionMethodInfo *p_list) {
        COMMON_SELF->free_method_list(p_list);
    };

    p_info.get_property_type_func = [](void *p_self, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) -> GDExtensionVariantType {
        return (GDExtensionVariantType)COMMON_SELF->get_property_type(*(const StringName *)p_name, (bool *)r_is_valid);
    };

    p_info.has_method_func = [](void *p_self, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        return COMMON_SELF->has_method(*(const StringName *)p_name);
    };

    p_info.get_script_func = [](void *p_self) {
        return COMMON_SELF->get_script().ptr()->_owner;
    };

    p_info.get_language_func = [](void *p_self) {
        return COMMON_SELF->get_language()->_owner;
    };
}

static String *string_alloc(const String &p_str) {
    String *ptr = memnew(String);
    *ptr = p_str;

    return ptr;
}

static StringName *stringname_alloc(const String &p_str) {
    StringName *ptr = memnew(StringName);
    *ptr = p_str;

    return ptr;
}

int ScriptInstance::get_len_from_ptr(const void *p_ptr) {
    return *((int *)p_ptr - 1);
}

void ScriptInstance::free_with_len(void *p_ptr) {
    memfree((int *)p_ptr - 1);
}

void ScriptInstance::copy_prop(const GDProperty &p_src, GDExtensionPropertyInfo &p_dst) {
    p_dst.type = p_src.type;
    p_dst.name = stringname_alloc(p_src.name);
    p_dst.class_name = stringname_alloc(p_src.class_name);
    p_dst.hint = p_src.hint;
    p_dst.hint_string = string_alloc(p_src.hint_string);
    p_dst.usage = p_src.usage;
}

void ScriptInstance::free_prop(const GDExtensionPropertyInfo &p_prop) {
    // smelly
    memdelete((StringName *)p_prop.name);
    memdelete((StringName *)p_prop.class_name);
    memdelete((String *)p_prop.hint_string);
}

void ScriptInstance::get_property_state(GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
    // ! refer to script_language.cpp get_property_state
    // the default implementation is not carried over to GDExtension

    uint32_t count = 0;
    GDExtensionPropertyInfo *props = get_property_list(&count);

    for (int i = 0; i < count; i++) {
        StringName *name = (StringName *)props[i].name;

        if (props[i].usage & PROPERTY_USAGE_STORAGE) {
            Variant value;
            bool is_valid = get(*name, value);

            if (is_valid)
                p_add_func(name, &value, p_userdata);
        }
    }

    free_property_list(props);
}

static void add_to_state(GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value, void *p_userdata) {
    List<Pair<StringName, Variant>> *list = reinterpret_cast<List<Pair<StringName, Variant>> *>(p_userdata);
    list->push_back({ *(const StringName *)p_name, *(const Variant *)p_value });
}

void ScriptInstance::get_property_state(List<Pair<StringName, Variant>> &p_list) {
    get_property_state(add_to_state, &p_list);
}

void ScriptInstance::free_property_list(const GDExtensionPropertyInfo *p_list) const {
    if (!p_list)
        return;

    // don't ask.
    int size = get_len_from_ptr(p_list);

    for (int i = 0; i < size; i++)
        free_prop(p_list[i]);

    free_with_len((GDExtensionPropertyInfo *)p_list);
}

GDExtensionMethodInfo *ScriptInstance::get_method_list(uint32_t *r_count) const {
    LocalVector<GDExtensionMethodInfo> methods;
    HashSet<StringName> defined;

    const LuauScript *s = get_script().ptr();

    while (s) {
        for (const KeyValue<StringName, GDMethod> &pair : s->get_definition().methods) {
            if (defined.has(pair.key))
                continue;

            defined.insert(pair.key);

            const GDMethod &src = pair.value;

            GDExtensionMethodInfo dst;

            dst.name = stringname_alloc(src.name);
            copy_prop(src.return_val, dst.return_value);
            dst.flags = src.flags;
            dst.argument_count = src.arguments.size();

            if (dst.argument_count > 0) {
                GDExtensionPropertyInfo *arg_list = memnew_arr(GDExtensionPropertyInfo, dst.argument_count);

                for (int j = 0; j < dst.argument_count; j++)
                    copy_prop(src.arguments[j], arg_list[j]);

                dst.arguments = arg_list;
            }

            dst.default_argument_count = src.default_arguments.size();

            if (dst.default_argument_count > 0) {
                Variant *defargs = memnew_arr(Variant, dst.default_argument_count);

                for (int j = 0; j < dst.default_argument_count; j++)
                    defargs[j] = src.default_arguments[j];

                dst.default_arguments = (GDExtensionVariantPtr *)defargs;
            }

            methods.push_back(dst);
        }

        s = s->get_base().ptr();
    }

    int size = methods.size();
    *r_count = size;

    GDExtensionMethodInfo *list = alloc_with_len<GDExtensionMethodInfo>(size);
    memcpy(list, methods.ptr(), sizeof(GDExtensionMethodInfo) * size);

    return list;
}

void ScriptInstance::free_method_list(const GDExtensionMethodInfo *p_list) const {
    if (!p_list)
        return;

    // don't ask.
    int size = get_len_from_ptr(p_list);

    for (int i = 0; i < size; i++) {
        const GDExtensionMethodInfo &method = p_list[i];

        memdelete((StringName *)method.name);

        free_prop(method.return_value);

        if (method.argument_count > 0) {
            for (int i = 0; i < method.argument_count; i++)
                free_prop(method.arguments[i]);

            memdelete(method.arguments);
        }

        if (method.default_argument_count > 0)
            memdelete((Variant *)method.default_arguments);
    }

    free_with_len((GDExtensionMethodInfo *)p_list);
}

ScriptLanguage *ScriptInstance::get_language() const {
    return LuauLanguage::get_singleton();
}

/////////////////////
// SCRIPT INSTANCE //
/////////////////////

#define INSTANCE_SELF ((LuauScriptInstance *)p_self)

static GDExtensionScriptInstanceInfo2 init_script_instance_info() {
    GDExtensionScriptInstanceInfo2 info;
    ScriptInstance::init_script_instance_info_common(info);

    info.property_can_revert_func = [](void *p_self, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        return INSTANCE_SELF->property_can_revert(*((StringName *)p_name));
    };

    info.property_get_revert_func = [](void *p_self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return INSTANCE_SELF->property_get_revert(*((StringName *)p_name), (Variant *)r_ret);
    };

    info.call_func = [](void *p_self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
        return INSTANCE_SELF->call(*((StringName *)p_method), (const Variant **)p_args, p_argument_count, (Variant *)r_return, r_error);
    };

    info.notification_func = [](void *p_self, int32_t p_what, GDExtensionBool p_reversed) {
        INSTANCE_SELF->notification(p_what);
    };

    info.to_string_func = [](void *p_self, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
        INSTANCE_SELF->to_string(r_is_valid, (String *)r_out);
    };

    info.free_func = [](void *p_self) {
        memdelete(INSTANCE_SELF);
    };

    info.refcount_decremented_func = [](void *) -> GDExtensionBool {
        // If false (default), object cannot die
        return true;
    };

    return info;
}

const GDExtensionScriptInstanceInfo2 LuauScriptInstance::INSTANCE_INFO = init_script_instance_info();

int LuauScriptInstance::call_internal(const StringName &p_method, lua_State *ET, int p_nargs, int p_nret) {
    LUAU_LOCK(ET);

    const LuauScript *s = script.ptr();

    while (s) {
        if (s->methods.has(p_method)) {
            LuaStackOp<String>::push(ET, p_method);
            s->def_table_get(ET);

            if (!lua_isfunction(ET, -1)) {
                lua_pop(ET, 1);
                return -1;
            }

            lua_insert(ET, -p_nargs - 1);

            LuaStackOp<Object *>::push(ET, owner);
            lua_insert(ET, -p_nargs - 1);

            INIT_TIMEOUT(ET)
            int status = lua_resume(ET, nullptr, p_nargs + 1);

            if (status != LUA_OK && status != LUA_YIELD) {
                s->error("LuauScriptInstance::call_internal", LuaStackOp<String>::get(ET, -1));

                lua_pop(ET, 1);
                return status;
            }

            lua_settop(ET, p_nret);
            return status;
        }

        s = s->base.ptr();
    }

    return -1;
}

bool LuauScriptInstance::set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err) {
#define SET_METHOD "LuauScriptInstance::set"
#define SET_NAME "_Set"

    const LuauScript *s = script.ptr();

    while (s) {
        HashMap<StringName, uint64_t>::ConstIterator E = s->get_definition().property_indices.find(p_name);

        if (E) {
            const GDClassProperty &prop = s->get_definition().properties[E->value];

            // Check type
            if (!Utils::variant_types_compatible(p_value.get_type(), Variant::Type(prop.property.type))) {
                if (r_err)
                    *r_err = PROP_WRONG_TYPE;

                return false;
            }

            // Check read-only (getter, no setter)
            if (prop.setter == StringName() && prop.getter != StringName()) {
                if (r_err)
                    *r_err = PROP_READ_ONLY;

                return false;
            }

            // Set
            LUAU_LOCK(T);
            lua_State *ET = lua_newthread(T);
            int status = LUA_OK;

            if (prop.setter != StringName()) {
                LuaStackOp<Variant>::push(ET, p_value);
                status = call_internal(prop.setter, ET, 1, 0);
            } else {
                LuaStackOp<String>::push(ET, p_name);
                LuaStackOp<Variant>::push(ET, p_value);
                table_set(ET);
            }

            lua_pop(T, 1); // thread

            if (status == LUA_OK || status == LUA_YIELD || status == LUA_BREAK) {
                if (r_err)
                    *r_err = PROP_OK;

                return true;
            } else if (status == -1) {
                if (r_err)
                    *r_err = PROP_NOT_FOUND;

                s->error(SET_METHOD, SETTER_NOT_FOUND_ERR(p_name), 1);
            } else {
                if (r_err)
                    *r_err = PROP_SET_FAILED;
            }

            return false;
        }

        if (s->methods.has(SET_NAME)) {
            lua_State *ET = lua_newthread(T);

            LuaStackOp<String>::push(ET, p_name);
            LuaStackOp<Variant>::push(ET, p_value);
            int status = call_internal(SET_NAME, ET, 2, 1);

            if (status == OK) {
                if (lua_type(ET, -1) == LUA_TBOOLEAN) {
                    bool valid = lua_toboolean(ET, -1);

                    if (valid) {
                        if (r_err) {
                            *r_err = PROP_OK;
                        }

                        lua_pop(T, 1); // thread
                        return true;
                    }
                } else {
                    if (r_err) {
                        *r_err = PROP_SET_FAILED;
                    }

                    s->error(SET_METHOD, EXPECTED_RET_ERR(SET_NAME, "boolean"), 1);
                    lua_pop(T, 1); // thread
                    return false;
                }
            }

            lua_pop(T, 1); // thread
        }

        s = s->base.ptr();
    }

    if (r_err)
        *r_err = PROP_NOT_FOUND;

    return false;
}

bool LuauScriptInstance::get(const StringName &p_name, Variant &r_ret, PropertySetGetError *r_err) {
#define GET_METHOD "LuauScriptInstance::get"
#define GET_NAME "_Get"

    const LuauScript *s = script.ptr();

    while (s) {
        HashMap<StringName, uint64_t>::ConstIterator E = s->get_definition().property_indices.find(p_name);

        if (E) {
            const GDClassProperty &prop = s->get_definition().properties[E->value];

            // Check write-only (setter, no getter)
            if (prop.setter != StringName() && prop.getter == StringName()) {
                if (r_err)
                    *r_err = PROP_WRITE_ONLY;

                return false;
            }

            // Get
            LUAU_LOCK(T);
            lua_State *ET = lua_newthread(T);
            int status = LUA_OK;

            if (prop.getter != StringName()) {
                status = call_internal(prop.getter, ET, 0, 1);
            } else {
                LuaStackOp<String>::push(ET, p_name);
                table_get(ET);
            }

            if (status == LUA_OK) {
                if (!LuauVariant::lua_is(ET, -1, prop.property.type)) {
                    if (r_err)
                        *r_err = PROP_WRONG_TYPE;

                    s->error(
                            GET_METHOD,
                            prop.getter == StringName() ? GET_TABLE_TYPE_ERR(p_name) : GETTER_RET_ERR(p_name),
                            1);

                    lua_pop(T, 1); // thread
                    return false;
                }

                LuauVariant ret;
                ret.lua_check(ET, -1, prop.property.type);
                r_ret = ret.to_variant();

                if (r_err)
                    *r_err = PROP_OK;

                lua_pop(T, 1); // thread
                return true;
            } else if (status == LUA_YIELD || status == LUA_BREAK) {
                if (r_err)
                    *r_err = PROP_GET_FAILED;

                s->error(GET_METHOD, GETTER_YIELD_ERR(p_name), 1);
            } else if (status == -1) {
                if (r_err)
                    *r_err = PROP_NOT_FOUND;

                s->error(GET_METHOD, GETTER_NOT_FOUND_ERR(p_name), 1);
            } else {
                if (r_err)
                    *r_err = PROP_GET_FAILED;
            }

            lua_pop(T, 1); // thread
            return false;
        }

        if (s->methods.has(GET_NAME)) {
            lua_State *ET = lua_newthread(T);

            LuaStackOp<String>::push(ET, p_name);
            int status = call_internal(GET_NAME, ET, 1, 1);

            if (status == OK) {
                if (LuaStackOp<Variant>::is(ET, -1)) {
                    Variant ret = LuaStackOp<Variant>::get(ET, -1);

                    if (ret != Variant()) {
                        if (r_err) {
                            *r_err = PROP_OK;
                        }

                        r_ret = ret;
                        lua_pop(T, 1); // thread
                        return true;
                    }
                } else {
                    if (r_err) {
                        *r_err = PROP_GET_FAILED;
                    }

                    s->error("LuauScriptInstance::get", EXPECTED_RET_ERR(GET_NAME, "Variant"), 1);
                    lua_pop(T, 1); // thread
                    return false;
                }
            }

            lua_pop(T, 1); // thread
        }

        s = s->base.ptr();
    }

    if (r_err)
        *r_err = PROP_NOT_FOUND;

    return false;
}

GDExtensionPropertyInfo *LuauScriptInstance::get_property_list(uint32_t *r_count) {
#define GET_PROPERTY_LIST_METHOD "LuauScriptInstance::get_property_list"
#define GET_PROPERTY_LIST_NAME "_GetPropertyList"

    LocalVector<GDExtensionPropertyInfo> properties;
    LocalVector<GDExtensionPropertyInfo> custom_properties;
    HashSet<StringName> defined;

    const LuauScript *s = script.ptr();

    // Push properties in reverse then reverse the entire vector.
    // Ensures base properties are first.
    // (see _get_script_property_list)
    while (s) {
        for (int i = s->get_definition().properties.size() - 1; i >= 0; i--) {
            const GDClassProperty &prop = s->get_definition().properties[i];

            if (defined.has(prop.property.name))
                continue;

            defined.insert(prop.property.name);

            GDExtensionPropertyInfo dst;
            copy_prop(prop.property, dst);

            properties.push_back(dst);
        }

        if (s->methods.has(GET_PROPERTY_LIST_NAME)) {
            lua_State *ET = lua_newthread(T);
            int status = call_internal(GET_PROPERTY_LIST_NAME, ET, 0, 1);

            if (status != LUA_OK) {
                goto next;
            }

            if (!lua_istable(ET, -1)) {
                s->error(GET_PROPERTY_LIST_METHOD, EXPECTED_RET_ERR(GET_PROPERTY_LIST_NAME, "table"), 1);
                goto next;
            }

            {
                // Process method return value
                // Must be protected to handle errors, which is why this is jank
                lua_pushcfunction(
                        ET, [](lua_State *FL) {
                            int sz = lua_objlen(FL, 1);
                            for (int i = 1; i <= sz; i++) {
                                lua_rawgeti(FL, 1, i);

                                GDProperty *ret = reinterpret_cast<GDProperty *>(lua_newuserdatadtor(FL, sizeof(GDProperty), [](void *p_ptr) {
                                    ((GDProperty *)p_ptr)->~GDProperty();
                                }));

                                new (ret) GDProperty();
                                *ret = luascript_read_property(FL, -2);

                                lua_remove(FL, -2); // value
                            }

                            return sz;
                        },
                        "get_property_list");

                lua_insert(ET, 1);

                INIT_TIMEOUT(ET)
                int get_status = lua_pcall(ET, 1, LUA_MULTRET, 0);
                if (get_status != LUA_OK) {
                    s->error(GET_PROPERTY_LIST_METHOD, LuaStackOp<String>::get(ET, -1));
                    goto next;
                }

                // The entire stack of ET is now the list of GDProperty values.
                for (int i = lua_gettop(ET); i >= 1; i--) {
                    GDProperty *property = reinterpret_cast<GDProperty *>(lua_touserdata(ET, i));

                    GDExtensionPropertyInfo dst;
                    copy_prop(*property, dst);

                    custom_properties.push_back(dst);
                }
            }

        next:
            lua_pop(T, 1); // thread
        }

        s = s->base.ptr();
    }

    properties.invert();

    // Custom properties are last.
    for (int i = custom_properties.size() - 1; i >= 0; i--) {
        properties.push_back(custom_properties[i]);
    }

    int size = properties.size();
    *r_count = size;

    GDExtensionPropertyInfo *list = alloc_with_len<GDExtensionPropertyInfo>(size);
    memcpy(list, properties.ptr(), sizeof(GDExtensionPropertyInfo) * size);

    return list;
}

Variant::Type LuauScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
    const LuauScript *s = script.ptr();

    while (s) {
        HashMap<StringName, uint64_t>::ConstIterator E = s->get_definition().property_indices.find(p_name);

        if (E) {
            if (r_is_valid)
                *r_is_valid = true;

            return (Variant::Type)s->get_definition().properties[E->value].property.type;
        }

        s = s->base.ptr();
    }

    if (r_is_valid)
        *r_is_valid = false;

    return Variant::NIL;
}

bool LuauScriptInstance::property_can_revert(const StringName &p_name) {
#define PROPERTY_CAN_REVERT_NAME "_PropertyCanRevert"

    const LuauScript *s = script.ptr();

    while (s) {
        if (s->methods.has(PROPERTY_CAN_REVERT_NAME)) {
            lua_State *ET = lua_newthread(T);

            LuaStackOp<String>::push(ET, p_name);
            int status = call_internal(PROPERTY_CAN_REVERT_NAME, ET, 1, 1);

            if (status != OK) {
                lua_pop(T, 1); // thread
                return false;
            }

            if (lua_type(ET, -1) != LUA_TBOOLEAN) {
                s->error("LuauScriptInstance::property_can_revert", EXPECTED_RET_ERR(PROPERTY_CAN_REVERT_NAME, "boolean"), 1);
                lua_pop(T, 1); // thread
                return false;
            }

            bool ret = lua_toboolean(ET, -1);
            lua_pop(T, 1); // thread
            return ret;
        }

        s = s->base.ptr();
    }

    return false;
}

bool LuauScriptInstance::property_get_revert(const StringName &p_name, Variant *r_ret) {
#define PROPERTY_GET_REVERT_NAME "_PropertyGetRevert"

    const LuauScript *s = script.ptr();

    while (s) {
        if (s->methods.has(PROPERTY_GET_REVERT_NAME)) {
            lua_State *ET = lua_newthread(T);

            LuaStackOp<String>::push(ET, p_name);
            int status = call_internal(PROPERTY_GET_REVERT_NAME, ET, 1, 1);

            if (status != OK) {
                lua_pop(T, 1); // thread
                return false;
            }

            if (!LuaStackOp<Variant>::is(ET, -1)) {
                s->error("LuauScriptInstance::property_get_revert", EXPECTED_RET_ERR(PROPERTY_GET_REVERT_NAME, "Variant"), 1);
                lua_pop(T, 1); // thread
                return false;
            }

            *r_ret = LuaStackOp<Variant>::get(ET, -1);
            lua_pop(T, 1); // thread
            return true;
        }

        s = s->base.ptr();
    }

    return false;
}

bool LuauScriptInstance::has_method(const StringName &p_name) const {
    const LuauScript *s = script.ptr();

    while (s) {
        if (s->has_method(p_name))
            return true;

        s = s->base.ptr();
    }

    return false;
}

void LuauScriptInstance::call(
        const StringName &p_method,
        const Variant *const *p_args, const GDExtensionInt p_argument_count,
        Variant *r_return, GDExtensionCallError *r_error) {
    const LuauScript *s = script.ptr();

    while (s) {
        StringName actual_name = p_method;

        // check name given and name converted to pascal
        // (e.g. if Node::_ready is called -> _Ready)
        if (s->has_method(p_method, &actual_name)) {
            const GDMethod &method = s->get_definition().methods[actual_name];

            // Check argument count
            int args_allowed = method.arguments.size();
            int args_default = method.default_arguments.size();
            int args_required = args_allowed - args_default;

            if (p_argument_count < args_required) {
                r_error->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
                r_error->argument = args_required;

                return;
            }

            if (p_argument_count > args_allowed) {
                r_error->error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
                r_error->argument = args_allowed;

                return;
            }

            // Prepare for call
            lua_State *ET = lua_newthread(T); // execution thread

            for (int i = 0; i < p_argument_count; i++) {
                const Variant &arg = *p_args[i];

                if (!(method.arguments[i].usage & PROPERTY_USAGE_NIL_IS_VARIANT) &&
                        !Utils::variant_types_compatible(arg.get_type(), Variant::Type(method.arguments[i].type))) {
                    r_error->error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
                    r_error->argument = i;
                    r_error->expected = method.arguments[i].type;

                    lua_pop(T, 1); // thread
                    return;
                }

                LuaStackOp<Variant>::push(ET, arg);
            }

            for (int i = p_argument_count - args_required; i < args_default; i++)
                LuaStackOp<Variant>::push(ET, method.default_arguments[i]);

            // Call
            r_error->error = GDEXTENSION_CALL_OK;

            int status = call_internal(actual_name, ET, args_allowed, 1);

            if (status == LUA_OK) {
                *r_return = LuaStackOp<Variant>::get(ET, -1);
            } else if (status == LUA_YIELD) {
                if (method.return_val.type != GDEXTENSION_VARIANT_TYPE_NIL) {
                    lua_pop(T, 1); // thread
                    ERR_FAIL_MSG(NON_VOID_YIELD_ERR);
                }

                *r_return = Variant();
            }

            lua_pop(T, 1); // thread
            return;
        }

        s = s->base.ptr();
    }

    r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
}

void LuauScriptInstance::notification(int32_t p_what) {
#define NOTIF_NAME "_Notification"

    // These notifications will fire at program exit; see ~LuauScriptInstance
    // 3: NOTIFICATION_PREDELETE_CLEANUP (not bound)
    if (p_what == Object::NOTIFICATION_PREDELETE || p_what == 3) {
        lua_State *L = GDLuau::get_singleton()->get_vm(vm_type);

        if (!L || !luaGD_getthreaddata(L))
            return;
    }

    const LuauScript *s = script.ptr();

    while (s) {
        if (s->methods.has(NOTIF_NAME)) {
            lua_State *ET = lua_newthread(T);

            LuaStackOp<int32_t>::push(ET, p_what);
            call_internal(NOTIF_NAME, ET, 1, 0);

            lua_pop(T, 1); // thread
        }

        s = s->base.ptr();
    }
}

void LuauScriptInstance::to_string(GDExtensionBool *r_is_valid, String *r_out) {
#define TO_STRING_NAME "_ToString"

    const LuauScript *s = script.ptr();

    while (s) {
        if (s->methods.has(TO_STRING_NAME)) {
            lua_State *ET = lua_newthread(T);

            int status = call_internal(TO_STRING_NAME, ET, 0, 1);

            if (status == LUA_OK)
                *r_out = LuaStackOp<String>::get(ET, -1);

            if (r_is_valid)
                *r_is_valid = status == LUA_OK;

            lua_pop(T, 1); // thread
            return;
        }

        s = s->base.ptr();
    }
}

bool LuauScriptInstance::table_set(lua_State *T) const {
    if (lua_mainthread(T) != lua_mainthread(this->T))
        return false;

    LUAU_LOCK(T);
    lua_getref(T, table_ref);
    lua_insert(T, -3);
    lua_settable(T, -3);
    lua_remove(T, -1);

    return true;
}

bool LuauScriptInstance::table_get(lua_State *T) const {
    if (lua_mainthread(T) != lua_mainthread(this->T))
        return false;

    LUAU_LOCK(T);
    lua_getref(T, table_ref);
    lua_insert(T, -2);
    lua_gettable(T, -2);
    lua_remove(T, -2);

    return true;
}

#define DEF_GETTER(m_type, m_method_name, m_def_key)                                                   \
    const m_type *LuauScriptInstance::get_##m_method_name(const StringName &p_name) const {            \
        const LuauScript *s = script.ptr();                                                            \
                                                                                                       \
        while (s) {                                                                                    \
            HashMap<StringName, m_type>::ConstIterator E = s->get_definition().m_def_key.find(p_name); \
                                                                                                       \
            if (E)                                                                                     \
                return &E->value;                                                                      \
                                                                                                       \
            s = s->base.ptr();                                                                         \
        }                                                                                              \
                                                                                                       \
        return nullptr;                                                                                \
    }

DEF_GETTER(GDMethod, method, methods)

const GDClassProperty *LuauScriptInstance::get_property(const StringName &p_name) const {
    const LuauScript *s = script.ptr();

    while (s) {
        if (s->has_property(p_name))
            return &s->get_property(p_name);

        s = s->base.ptr();
    }

    return nullptr;
}

DEF_GETTER(GDMethod, signal, signals)

const Variant *LuauScriptInstance::get_constant(const StringName &p_name) const {
    HashMap<StringName, Variant>::ConstIterator E = script->constants.find(p_name);
    return E ? &E->value : nullptr;
}

LuauScriptInstance *LuauScriptInstance::from_object(GDExtensionObjectPtr p_object) {
    if (!p_object)
        return nullptr;

    Ref<LuauScript> script = nb::Object(p_object).get_script();
    uint64_t id = internal::gdextension_interface_object_get_instance_id(p_object);
    if (script.is_valid() && script->instance_has(id))
        return script->instance_get(id);

    return nullptr;
}

LuauScriptInstance::LuauScriptInstance(const Ref<LuauScript> &p_script, Object *p_owner, GDLuau::VMType p_vm_type) :
        script(p_script), owner(p_owner), vm_type(p_vm_type) {
#define INST_CTOR_METHOD "LuauScriptInstance::LuauScriptInstance"

    // this usually occurs in _instance_create, but that is marked const for ScriptExtension
    {
        MutexLock lock(*LuauLanguage::singleton->lock.ptr());
        p_script->instances.insert(p_owner->get_instance_id(), this);
    }

    LocalVector<LuauScript *> base_scripts;
    LuauScript *s = p_script.ptr();

    while (s) {
        base_scripts.push_back(s);
        permissions = permissions | s->get_definition().permissions;

        s = s->base.ptr();
    }

    base_scripts.invert(); // To initialize base-first

    if (permissions != PERMISSION_BASE) {
        CRASH_COND_MSG(SandboxService::get_singleton() && !SandboxService::get_singleton()->is_core_script(p_script->get_path()), NON_CORE_PERM_DECL_ERR);
        UtilityFunctions::print_verbose("Creating instance of script ", p_script->get_path(), " with requested permissions ", p_script->get_definition().permissions);
    }

    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    LUAU_LOCK(L);
    T = luaGD_newthread(L, permissions);
    luaL_sandboxthread(T);

    GDThreadData *udata = luaGD_getthreaddata(T);
    udata->script = p_script;

    thread_ref = lua_ref(L, -1);
    lua_pop(L, 1); // thread

    lua_newtable(T);
    table_ref = lua_ref(T, -1);
    lua_pop(T, 1); // table

    for (LuauScript *&scr : base_scripts) {
        // Initialize default values
        for (const GDClassProperty &prop : scr->get_definition().properties) {
            if (prop.getter == StringName() && prop.setter == StringName()) {
                LuaStackOp<String>::push(T, prop.property.name);
                LuaStackOp<Variant>::push(T, prop.default_value);
                table_set(T);
            }
        }

        // Run _Init for each script
        Error method_err = scr->load_table(p_vm_type);

        if (method_err == OK) {
            LuaStackOp<String>::push(T, "_Init");
            scr->def_table_get(T);

            if (lua_isfunction(T, -1)) {
                // This object can be considered as the full script instance (minus some initialized values) because Object sets its script
                // before instance_create was called, and this instance was registered with the script before now.
                LuaStackOp<Object *>::push(T, p_owner);

                INIT_TIMEOUT(T)
                int status = lua_pcall(T, 1, 0, 0);

                if (status == LUA_YIELD) {
                    p_script->error(INST_CTOR_METHOD, INIT_YIELD_ERR);
                } else if (status != LUA_OK) {
                    p_script->error(INST_CTOR_METHOD, INIT_ERR(LuaStackOp<String>::get(T, -1)));
                    lua_pop(T, 1);
                }
            } else {
                lua_pop(T, 1);
            }
        } else {
            ERR_PRINT(METHOD_LOAD_ERR(scr->get_path()));
        }
    }
}

LuauScriptInstance::~LuauScriptInstance() {
    if (script.is_valid() && owner) {
        MutexLock lock(*LuauLanguage::singleton->lock.ptr());
        script->instances.erase(owner->get_instance_id());
    }

    lua_State *L = GDLuau::get_singleton()->get_vm(vm_type);

    // Check to prevent issues with unref during thread free (luaGD_close in ~GDLuau)
    if (L && luaGD_getthreaddata(L)) {
        LUAU_LOCK(L);

        lua_unref(L, table_ref);
        lua_unref(L, thread_ref);
    }

    table_ref = -1;
    thread_ref = -1;
}

//////////////
// LANGUAGE //
//////////////

LuauLanguage *LuauLanguage::singleton = nullptr;

LuauLanguage::LuauLanguage() {
    singleton = this;
    lock.instantiate();
    debug.call_lock.instantiate();
}

LuauLanguage::~LuauLanguage() {
    finalize();
    singleton = nullptr;
}

#ifdef TESTS_ENABLED
static void run_tests() {
    if (!nb::OS::get_singleton_nb()->get_cmdline_args().has("--luau-tests"))
        return;

    UtilityFunctions::print("Catch2: Running tests...");

    Catch::Session session;

    // Fetch args
    PackedStringArray args = nb::OS::get_singleton_nb()->get_cmdline_user_args();
    int argc = args.size();

    // CharString does not work with godot::Vector
    std::vector<CharString> charstr_vec(argc);

    std::vector<const char *> argv_vec(argc + 1);
    argv_vec[0] = "luau-script"; // executable name

    for (int i = 0; i < argc; i++) {
        charstr_vec[i] = args[i].utf8();
        argv_vec[i + 1] = charstr_vec[i].get_data();
    }

    session.applyCommandLine(argc + 1, argv_vec.data());

    // Run
    session.run();
}
#endif // TESTS_ENABLED

void LuauLanguage::_init() {
#define INIT_METHOD "LuauLanguage::_init"

#ifdef TESTS_ENABLED
    // Tests are run at this stage (before GDLuau and LuauCache are initialized and after _init is called)
    // in order to ensure singletons/methods are all registered and available for immediate retrieval.
    run_tests();
#endif // TESTS_ENABLED

    // Initialize LuauInterface first, deinit last due to GDLuau dependency
    interface = memnew(LuauInterface);
    luau = memnew(GDLuau);
    cache = memnew(LuauCache);

    if (FileAccess::file_exists(INIT_LUA_PATH)) {
        String src;
        Error err = Utils::load_file(INIT_LUA_PATH, src);

        if (err == OK) {
            std::string bytecode = Luau::compile(src.utf8().get_data(), luaGD_compileopts());
            lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_CORE);
            lua_State *T = lua_newthread(L);

            GDThreadData *udata = luaGD_getthreaddata(T);
            udata->permissions = PERMISSIONS_ALL;

            if (luau_load(T, "@" INIT_LUA_PATH, bytecode.data(), bytecode.size(), 0) == 0) {
                int status = lua_resume(T, nullptr, 0);

                if (status == LUA_YIELD) {
                    luaGD_gderror(INIT_METHOD, INIT_LUA_PATH, INIT_FILE_YIELD_ERR, 1);
                } else if (status != LUA_OK) {
                    luaGD_gderror(INIT_METHOD, INIT_LUA_PATH, LuaStackOp<String>::get(T, -1));
                }
            } else {
                luaGD_gderror(INIT_METHOD, INIT_LUA_PATH, LuaStackOp<String>::get(T, -1));
            }

            lua_pop(L, 1); // thread
        }
    }

    // TODO: Only if EngineDebugger is active
    // if (nb::EngineDebugger::get_singleton_nb()->is_active())
    debug_init();
}

void LuauLanguage::finalize() {
    if (finalized)
        return;

    if (luau) {
        memdelete(luau);
        luau = nullptr;
    }

    if (cache) {
        memdelete(cache);
        cache = nullptr;
    }

    if (interface) {
        memdelete(interface);
        interface = nullptr;
    }

    finalized = true;
}

void LuauLanguage::_finish() {
    finalize();
}

String LuauLanguage::_get_name() const {
    return "Luau";
}

String LuauLanguage::_get_type() const {
    return "LuauScript";
}

String LuauLanguage::_get_extension() const {
    return "lua";
}

PackedStringArray LuauLanguage::_get_recognized_extensions() const {
    PackedStringArray extensions;
    extensions.push_back("lua");

    return extensions;
}

PackedStringArray LuauLanguage::_get_reserved_words() const {
    static const char *_reserved_words[] = {
        "and",
        "break",
        "do",
        "else",
        "elseif",
        "end",
        "false",
        "for",
        "function",
        "if",
        "in",
        "local",
        "nil",
        "not",
        "or",
        "repeat",
        "return",
        "then",
        "true",
        "until",
        "while",
        "continue", // not technically a keyword, but ...
        nullptr
    };

    PackedStringArray keywords;

    const char **w = _reserved_words;

    while (*w) {
        keywords.push_back(*w);
        w++;
    }

    return keywords;
}

bool LuauLanguage::_is_control_flow_keyword(const String &p_keyword) const {
    return p_keyword == "break" ||
            p_keyword == "else" ||
            p_keyword == "elseif" ||
            p_keyword == "for" ||
            p_keyword == "if" ||
            p_keyword == "repeat" ||
            p_keyword == "return" ||
            p_keyword == "until" ||
            p_keyword == "while";
}

PackedStringArray LuauLanguage::_get_comment_delimiters() const {
    PackedStringArray delimiters;
    delimiters.push_back("--");
    delimiters.push_back("--[[ ]]");

    return delimiters;
}

PackedStringArray LuauLanguage::_get_string_delimiters() const {
    PackedStringArray delimiters;
    delimiters.push_back("\" \"");
    delimiters.push_back("' '");
    delimiters.push_back("[[ ]]");

    // TODO: does not include the [=======[ style strings

    return delimiters;
}

bool LuauLanguage::_supports_builtin_mode() const {
    // don't currently wish to deal with overhead (if any) of supporting this
    // and honestly I don't care for builtin scripts anyway
    return false;
}

bool LuauLanguage::_can_inherit_from_file() const {
    return true;
}

Object *LuauLanguage::_create_script() const {
    return memnew(LuauScript);
}

void LuauLanguage::_add_global_constant(const StringName &p_name, const Variant &p_value) {
    // TODO: Difference between these two functions?
    _add_named_global_constant(p_name, p_value);
}

void LuauLanguage::_add_named_global_constant(const StringName &p_name, const Variant &p_value) {
    global_constants[p_name] = p_value;
}

void LuauLanguage::_remove_named_global_constant(const StringName &p_name) {
    global_constants.erase(p_name);
}

void LuauLanguage::_frame() {
#ifdef TOOLS_ENABLED
    if (ticks_usec == 0) {
        // Manually register LuauScript icon
        // Probably the earliest point where EditorNode is available
        static constexpr const char *icon_path = "res://bin/luau-script/icons/LuauScript.svg";
        if (nb::Engine::get_singleton_nb()->is_editor_hint() && FileAccess::file_exists(icon_path)) {
            Ref<Theme> editor_theme = nb::EditorInterface::get_singleton_nb()->get_editor_theme();
            Ref<Texture2D> tex = nb::ResourceLoader::get_singleton_nb()->load(icon_path);

            editor_theme->set_icon("LuauScript", "EditorIcons", tex);
        }
    }
#endif // TOOLS_ENABLED

    uint64_t new_ticks = nb::Time::get_singleton_nb()->get_ticks_usec();
    double time_scale = nb::Engine::get_singleton_nb()->get_time_scale();

    double delta = 0;
    if (ticks_usec != 0)
        delta = (new_ticks - ticks_usec) / 1e6f;

    task_scheduler.frame(delta * time_scale);

    ticks_usec = new_ticks;
}

bool LuauLanguage::_has_named_classes() const {
    // not true for any of Godot's built in languages. why
    return false;
}

//////////////
// RESOURCE //
//////////////

// Loader

PackedStringArray ResourceFormatLoaderLuauScript::_get_recognized_extensions() const {
    PackedStringArray extensions;
    extensions.push_back("lua");

    return extensions;
}

bool ResourceFormatLoaderLuauScript::_handles_type(const StringName &p_type) const {
    return p_type == StringName("Script") || p_type == LuauLanguage::get_singleton()->_get_type();
}

String ResourceFormatLoaderLuauScript::get_resource_type(const String &p_path) {
    // Special case
    if (p_path == INIT_LUA_PATH)
        return "";

    return p_path.get_extension().to_lower() == "lua" ? LuauLanguage::get_singleton()->_get_type() : "";
}

String ResourceFormatLoaderLuauScript::_get_resource_type(const String &p_path) const {
    return get_resource_type(p_path);
}

Variant ResourceFormatLoaderLuauScript::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
    Error err = OK;
    Ref<LuauScript> script = LuauCache::get_singleton()->get_script(p_path, err, p_cache_mode == CACHE_MODE_IGNORE);

    return script;
}

// Saver

PackedStringArray ResourceFormatSaverLuauScript::_get_recognized_extensions(const Ref<Resource> &p_resource) const {
    PackedStringArray extensions;

    Ref<LuauScript> ref = p_resource;
    if (ref.is_valid())
        extensions.push_back("lua");

    return extensions;
}

bool ResourceFormatSaverLuauScript::_recognize(const Ref<Resource> &p_resource) const {
    Ref<LuauScript> ref = p_resource;
    return ref.is_valid();
}

Error ResourceFormatSaverLuauScript::_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
    Ref<LuauScript> script = p_resource;
    ERR_FAIL_COND_V(script.is_null(), ERR_INVALID_PARAMETER);

    String source = script->get_source_code();

    {
        Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::ModeFlags::WRITE);
        ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), FILE_SAVE_FAILED_ERR(p_path));

        file->store_string(source);

        if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF)
            return ERR_CANT_CREATE;
    }

    // TODO: Godot's default language implementations have a check here. It isn't possible in extensions (yet).
    // if (ScriptServer::is_reload_scripts_on_save_enabled())
    LuauLanguage::get_singleton()->_reload_tool_script(p_resource, false);

    return OK;
}
