#include "luau_script.h"

#include <Luau/BytecodeBuilder.h>
#include <Luau/Compiler.h>
#include <Luau/Lexer.h>
#include <Luau/ParseResult.h>
#include <Luau/Parser.h>
#include <Luau/StringUtils.h>
#include <gdextension_interface.h>
#include <lua.h>
#include <string.h>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <string>
#include <utility>

#include "gd_luau.h"
#include "luagd.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luau_analysis.h"
#include "luau_cache.h"
#include "luau_lib.h"
#include "utils.h"

namespace godot {
class Object;
}

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
    Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
    ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), "Failed to read file: '" + p_path + "'.");

    uint64_t len = file->get_length();
    PackedByteArray bytes = file->get_buffer(len);
    bytes.resize(len + 1);
    bytes[len] = 0; // EOF

    String src;
    src.parse_utf8(reinterpret_cast<const char *>(bytes.ptr()));

    _set_source_code(src);

    return OK;
}

#define LUAU_LOAD_ERR(script, line, msg) LUAU_ERR("LuauScript::_reload", script, line, msg)
#define LUAU_LOAD_YIELD_MSG String("Script yielded when loading definition.")
#define LUAU_LOAD_NO_DEF_MSG String("Script did not return a valid class definition.")

#define LUAU_LOAD_RESUME(script, TL, L)                \
    int status = lua_resume(L, nullptr, 0);            \
                                                       \
    if (status == LUA_YIELD) {                         \
        LUAU_LOAD_ERR(script, 1, LUAU_LOAD_YIELD_MSG); \
                                                       \
        lua_pop(TL, 1); /* thread */                   \
        return ERR_COMPILATION_FAILED;                 \
    } else if (status != LUA_OK) {                     \
        String err = LuaStackOp<String>::get(L, -1);   \
        LUAU_LOAD_ERR(script, 1, err);                 \
                                                       \
        lua_pop(TL, 1); /* thread */                   \
        return ERR_COMPILATION_FAILED;                 \
    }

void LuauScript::compile() {
    // See Luau Compiler.cpp
    CharString src = source.utf8();

    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseResult parse_result = Luau::Parser::parse(src.get_data(), src.length() + 1, names, allocator);
    std::string bytecode;

    LuauScriptAnalysisResult analysis_result;
    bool analysis_valid;

    if (parse_result.errors.empty()) {
        try {
            Luau::CompileOptions opts;

            Luau::BytecodeBuilder bcb;
            Luau::compileOrThrow(bcb, parse_result, names, opts);

            bytecode = bcb.getBytecode();
            analysis_valid = luascript_analyze(parse_result, analysis_result);
        } catch (Luau::CompileError &err) {
            std::string error = Luau::format(":%d: %s", err.getLocation().begin.line + 1, err.what());
            bytecode = Luau::BytecodeBuilder::getError(error);
        }
    } else {
        const Luau::ParseError &err = parse_result.errors.front();
        std::string error = Luau::format(":%d: %s", err.getLocation().begin.line + 1, err.what());
        bytecode = Luau::BytecodeBuilder::getError(error);
    }

    // tf?
    luau_data.allocator.~Allocator();
    new (&luau_data.allocator) Luau::Allocator(std::move(allocator));
    luau_data.parse_result = parse_result;
    luau_data.bytecode = bytecode;

    if (analysis_valid)
        luau_data.analysis_result = analysis_result;
    else
        luau_data.analysis_result = LuauScriptAnalysisResult();
}

Error LuauScript::try_load(lua_State *L, String *r_err) {
    String chunkname = "=" + get_path().get_file();

    if (get_luau_data().bytecode.empty())
        compile();

    const std::string &bytecode = get_luau_data().bytecode;

    Error ret = luau_load(L, chunkname.utf8().get_data(), bytecode.data(), bytecode.size(), 0) == 0 ? OK : ERR_COMPILATION_FAILED;

    if (ret != OK) {
        String err = LuaStackOp<String>::get(L, -1);
        LUAU_LOAD_ERR(this, 1, err);

        if (r_err)
            *r_err = err;
    }

    return ret;
}

Error LuauScript::load_definition(GDLuau::VMType p_vm_type, bool p_force) {
    if (vm_defs_valid[p_vm_type]) {
        if (!p_force)
            return OK;

        unref_definition(p_vm_type);
    }

    // TODO: error line numbers?
    ERR_FAIL_COND_V_MSG(_is_loading, ERR_CYCLIC_LINK, "cyclic dependency detected. requested script load when it was already loading.");
    _is_loading = true;

    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    LUAU_LOCK(L);

    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    GDThreadData *udata = luaGD_getthreaddata(T);
    udata->script = Ref<LuauScript>(this);

    if (try_load(T) == OK) {
        LUAU_LOAD_RESUME(this, L, T)

        if (lua_isnil(T, 1) || !LuaStackOp<GDClassDefinition>::is(T, 1)) {
            lua_pop(L, 1); // thread
            LUAU_LOAD_ERR(this, 1, LUAU_LOAD_NO_DEF_MSG);

            _is_loading = false;
            return ERR_COMPILATION_FAILED;
        }

        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::get_ptr(T, -1);
        def->script = this;
        def->is_readonly = true;

        vm_defs[p_vm_type] = *def;
        vm_defs_valid[p_vm_type] = true;

        lua_pop(L, 1); // thread
        _is_loading = false;
        return OK;
    }

    lua_pop(L, 1); // thread
    _is_loading = false;
    return ERR_COMPILATION_FAILED;
}

void LuauScript::unref_definition(GDLuau::VMType vm) {
    if (vm_defs_valid[vm] && vm_defs[vm].table_ref != -1) {
        lua_State *L = GDLuau::get_singleton()->get_vm(vm);
        LUAU_LOCK(L);

        lua_unref(L, vm_defs[vm].table_ref);
        vm_defs_valid[vm] = false;
    }
}

Error LuauScript::reload_defs() {
    for (int i = 0; i < GDLuau::VM_MAX; i++) {
        if (i != GDLuau::VM_SCRIPT_LOAD && !vm_defs_valid[i])
            continue;

        Error err = load_definition(GDLuau::VMType(i), true);
        if (err != OK)
            return err;
    }

    return OK;
}

Error LuauScript::_reload(bool p_keep_state) {
    if (_is_module)
        return OK;

    {
        MutexLock lock(*LuauLanguage::singleton->lock.ptr());
        ERR_FAIL_COND_V(!p_keep_state && instances.size() > 0, ERR_ALREADY_IN_USE);
    }

    dependencies.clear();

    // Load script.
    compile(); // Always recompile on reload.

    valid = false;
    Error err = reload_defs();
    if (err != OK)
        return err;

    const GDClassDefinition &def = get_definition();

    set_name(def.name);

    // Update base script.
    base = Ref<LuauScript>(def.base_script);
    valid = !base.is_valid() || base->_is_valid(); // Already known that this script is valid

    // Build method cache.
    lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_SCRIPT_LOAD);
    LUAU_LOCK(L);
    methods.clear();

    if (def.table_ref != -1) {
        lua_getref(L, def.table_ref);

        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1)) {
                methods.insert(lua_tostring(L, -2));
            }

            lua_pop(L, 1); // value
        }

        lua_pop(L, 1); // table
    }

    return OK;
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
    return !_is_module && valid && (_is_tool() || !Engine::get_singleton()->is_editor_hint());
}

bool LuauScript::_is_tool() const {
    return get_definition().is_tool;
}

StringName LuauScript::_get_instance_base_type() const {
    StringName extends = StringName(get_definition().extends);

    if (extends != StringName() && Utils::class_exists(extends)) {
        return extends;
    }

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
    return get_name();
}

TypedArray<Dictionary> LuauScript::_get_script_method_list() const {
    TypedArray<Dictionary> methods;

    for (const KeyValue<StringName, GDMethod> &pair : get_definition().methods)
        methods.push_back(pair.value);

    return methods;
}

bool LuauScript::_has_method(const StringName &p_method) const {
    return has_method(p_method);
}

bool LuauScript::has_method(const StringName &p_method, StringName *r_actual_name) const {
    if (get_definition().methods.has(p_method))
        return true;

    StringName pascal_name = Utils::to_pascal_case(p_method);

    if (get_definition().methods.has(pascal_name)) {
        if (r_actual_name)
            *r_actual_name = pascal_name;

        return true;
    }

    return false;
}

Dictionary LuauScript::_get_method_info(const StringName &p_method) const {
    HashMap<StringName, GDMethod>::ConstIterator E = get_definition().methods.find(p_method);

    if (E)
        return E->value;

    E = get_definition().methods.find(Utils::to_pascal_case(p_method));

    if (E)
        return E->value;

    return Dictionary();
}

TypedArray<Dictionary> LuauScript::_get_script_property_list() const {
    TypedArray<Dictionary> properties;

    const LuauScript *s = this;

    while (s) {
        // Reverse to add properties from base scripts first.
        for (int i = get_definition().properties.size() - 1; i >= 0; i--) {
            const GDClassProperty &prop = get_definition().properties[i];
            properties.push_front(prop.property.operator Dictionary());
        }

        s = s->base.ptr();
    }

    return properties;
}

TypedArray<StringName> LuauScript::_get_members() const {
    TypedArray<StringName> members;

    for (const GDClassProperty &prop : get_definition().properties)
        members.push_back(prop.property.name);

    return members;
}

bool LuauScript::_has_property_default_value(const StringName &p_property) const {
    HashMap<StringName, uint64_t>::ConstIterator E = get_definition().property_indices.find(p_property);

    if (E && get_definition().properties[E->value].default_value != Variant())
        return true;

    if (base.is_valid())
        return base->_has_property_default_value(p_property);

    return false;
}

Variant LuauScript::_get_property_default_value(const StringName &p_property) const {
    HashMap<StringName, uint64_t>::ConstIterator E = get_definition().property_indices.find(p_property);

    if (E && get_definition().properties[E->value].default_value != Variant())
        return get_definition().properties[E->value].default_value;

    if (base.is_valid())
        return base->_get_property_default_value(p_property);

    return Variant();
}

bool LuauScript::has_property(const StringName &p_name) const {
    return get_definition().property_indices.has(p_name);
}

const GDClassProperty &LuauScript::get_property(const StringName &p_name) const {
    return get_definition().properties[get_definition().property_indices[p_name]];
}

bool LuauScript::_has_script_signal(const StringName &signal) const {
    return get_definition().signals.has(signal);
}

TypedArray<Dictionary> LuauScript::_get_script_signal_list() const {
    TypedArray<Dictionary> signals;

    for (const KeyValue<StringName, GDMethod> &pair : get_definition().signals)
        signals.push_back(pair.value);

    return signals;
}

Variant LuauScript::_get_rpc_config() const {
    Dictionary rpcs;

    for (const KeyValue<StringName, GDRpc> &pair : get_definition().rpcs)
        rpcs[pair.key] = pair.value;

    return rpcs;
}

Dictionary LuauScript::_get_constants() const {
    Dictionary constants;

    for (const KeyValue<StringName, Variant> &pair : get_definition().constants)
        constants[pair.key] = pair.value;

    return constants;
}

void *LuauScript::_instance_create(Object *p_for_object) const {
    GDLuau::VMType type = GDLuau::VM_USER;

    if (!get_path().is_empty() && LuauLanguage::get_singleton()->is_core_script(get_path()))
        type = GDLuau::VM_CORE;

    LuauScriptInstance *internal = memnew(LuauScriptInstance(Ref<Script>(this), p_for_object, GDLuau::VM_CORE));
    return internal::gde_interface->script_instance_create(&LuauScriptInstance::INSTANCE_INFO, internal);
}

bool LuauScript::_instance_has(Object *p_object) const {
    MutexLock lock(*LuauLanguage::singleton->lock.ptr());
    return instances.has(p_object->get_instance_id());
}

LuauScriptInstance *LuauScript::instance_get(Object *p_object) const {
    MutexLock lock(*LuauLanguage::singleton->lock.ptr());
    return instances.get(p_object->get_instance_id());
}

void LuauScript::def_table_get(GDLuau::VMType p_vm_type, lua_State *T) const {
    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    ERR_FAIL_COND_MSG(lua_mainthread(L) != lua_mainthread(T), "cannot push definition table to a thread from a different VM than the one being queried");
    LUAU_LOCK(L);

    int table_ref = vm_defs[p_vm_type].table_ref;
    if (table_ref == -1) {
        lua_pushnil(T);
        return;
    }

    lua_getref(T, table_ref);
    lua_insert(T, -2);
    lua_gettable(T, -2);
    lua_remove(T, -2);
}

const GDClassDefinition &LuauScript::get_definition(GDLuau::VMType p_vm_type) const {
    return vm_defs[p_vm_type];
}

bool LuauScript::has_dependency(const Ref<LuauScript> &p_script) const {
    return dependencies.has(p_script);
}

// Based on Luau Repl implementation.
Error LuauScript::load_module(lua_State *L) {
    // Use main thread to avoid inheriting L's environment.
    _is_loading = true;

    lua_State *GL = lua_mainthread(L);
    lua_State *ML = lua_newthread(GL);

    GDThreadData *udata = luaGD_getthreaddata(ML);
    udata->script = Ref<LuauScript>(this);

    lua_xmove(GL, L, 1); // thread

    luaL_sandboxthread(ML);

    String err;

    if (try_load(ML, &err) == OK) {
        LUAU_LOAD_RESUME(this, L, ML)

        if (lua_gettop(ML) == 0 || (!lua_istable(ML, -1) && !lua_isfunction(L, -1))) {
#define RET_ERR "module must return a function or table"

            lua_pushstring(L, RET_ERR);
            _is_loading = false;
            ERR_FAIL_V_MSG(ERR_COMPILATION_FAILED, RET_ERR);
        }

        lua_xmove(ML, L, 1);
        _is_loading = false;
        return OK;
    }

    LuaStackOp<String>::push(L, err);
    _is_loading = false;
    return ERR_COMPILATION_FAILED;
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
            if (!vm_defs_valid[i])
                continue;

            if (vm_defs[i].table_ref != -1)
                unref_definition(GDLuau::VMType(i));

            vm_defs_valid[i] = false;
        }
    }
}

////////////////////////////
// SCRIPT INSTANCE COMMON //
////////////////////////////

#define COMMON_SELF ((ScriptInstance *)self)

void ScriptInstance::init_script_instance_info_common(GDExtensionScriptInstanceInfo &p_info) {
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

    p_info.set_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) -> GDExtensionBool {
        return COMMON_SELF->set(*(const StringName *)p_name, *(const Variant *)p_value);
    };

    p_info.get_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return COMMON_SELF->get(*(const StringName *)p_name, *(Variant *)r_ret);
    };

    p_info.get_property_list_func = [](void *self, uint32_t *r_count) -> const GDExtensionPropertyInfo * {
        return COMMON_SELF->get_property_list(r_count);
    };

    p_info.free_property_list_func = [](void *self, const GDExtensionPropertyInfo *p_list) {
        COMMON_SELF->free_property_list(p_list);
    };

    p_info.get_owner_func = [](void *self) {
        return COMMON_SELF->get_owner()->_owner;
    };

    p_info.get_property_state_func = [](void *self, GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
        COMMON_SELF->get_property_state(p_add_func, p_userdata);
    };

    p_info.get_method_list_func = [](void *self, uint32_t *r_count) -> const GDExtensionMethodInfo * {
        return COMMON_SELF->get_method_list(r_count);
    };

    p_info.free_method_list_func = [](void *self, const GDExtensionMethodInfo *p_list) {
        COMMON_SELF->free_method_list(p_list);
    };

    p_info.get_property_type_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) -> GDExtensionVariantType {
        return (GDExtensionVariantType)COMMON_SELF->get_property_type(*(const StringName *)p_name, (bool *)r_is_valid);
    };

    p_info.has_method_func = [](void *self, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        return COMMON_SELF->has_method(*(const StringName *)p_name);
    };

    p_info.get_script_func = [](void *self) {
        return COMMON_SELF->get_script().ptr()->_owner;
    };

    p_info.get_language_func = [](void *self) {
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

int ScriptInstance::get_len_from_ptr(const void *ptr) {
    return *((int *)ptr - 1);
}

void ScriptInstance::free_with_len(void *ptr) {
    memfree((int *)ptr - 1);
}

void ScriptInstance::copy_prop(const GDProperty &src, GDExtensionPropertyInfo &dst) {
    dst.type = src.type;
    dst.name = stringname_alloc(src.name);
    dst.class_name = stringname_alloc(src.class_name);
    dst.hint = src.hint;
    dst.hint_string = string_alloc(src.hint_string);
    dst.usage = src.usage;
}

void ScriptInstance::free_prop(const GDExtensionPropertyInfo &prop) {
    // smelly
    memdelete((StringName *)prop.name);
    memdelete((StringName *)prop.class_name);
    memdelete((String *)prop.hint_string);
}

void ScriptInstance::get_property_state(GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
    // ! refer to script_language.cpp get_property_state
    // the default implementation is not carried over to GDExtension

    uint32_t count;
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
        for (const KeyValue<StringName, GDMethod> pair : s->get_definition().methods) {
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

#define INSTANCE_SELF ((LuauScriptInstance *)self)

static GDExtensionScriptInstanceInfo init_script_instance_info() {
    GDExtensionScriptInstanceInfo info;
    ScriptInstance::init_script_instance_info_common(info);

    info.property_can_revert_func = [](void *self, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        return INSTANCE_SELF->property_can_revert(*((StringName *)p_name));
    };

    info.property_get_revert_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return INSTANCE_SELF->property_get_revert(*((StringName *)p_name), (Variant *)r_ret);
    };

    info.call_func = [](void *self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
        return INSTANCE_SELF->call(*((StringName *)p_method), (const Variant **)p_args, p_argument_count, (Variant *)r_return, r_error);
    };

    info.notification_func = [](void *self, int32_t p_what) {
        INSTANCE_SELF->notification(p_what);
    };

    info.to_string_func = [](void *self, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
        INSTANCE_SELF->to_string(r_is_valid, (String *)r_out);
    };

    info.free_func = [](void *self) {
        memdelete(INSTANCE_SELF);
    };

    return info;
}

const GDExtensionScriptInstanceInfo LuauScriptInstance::INSTANCE_INFO = init_script_instance_info();

int LuauScriptInstance::call_internal(const StringName &p_method, lua_State *ET, int nargs, int nret) {
    LUAU_LOCK(ET);

    const LuauScript *s = script.ptr();

    while (s) {
        LuaStackOp<String>::push(ET, p_method);
        s->def_table_get(vm_type, ET);

        if (lua_isfunction(ET, -1)) {
            lua_insert(ET, -nargs - 1);

            LuaStackOp<Object *>::push(ET, owner);
            lua_insert(ET, -nargs - 1);

            int status = lua_resume(ET, nullptr, nargs + 1);

            if (status != LUA_OK && status != LUA_YIELD) {
                LUAU_ERR("LuauScriptInstance::call_internal", s, 1, LuaStackOp<String>::get(ET, -1));

                lua_pop(ET, 1);
                return status;
            }

            lua_settop(ET, nret);
            return status;
        } else {
            lua_pop(ET, 1);
        }

        s = s->base.ptr();
    }

    return -1;
}

int LuauScriptInstance::protected_table_set(lua_State *L, const Variant &p_key, const Variant &p_value) {
    lua_pushcfunction(
            L, [](lua_State *FL) {
                lua_settable(FL, 1);
                return 0;
            },
            "instance_table_set");

    lua_getref(L, table_ref);
    LuaStackOp<Variant>::push(L, p_key);
    LuaStackOp<Variant>::push(L, p_value);

    return lua_pcall(L, 3, 0, 0);
}

int LuauScriptInstance::protected_table_get(lua_State *L, const Variant &p_key) {
    lua_pushcfunction(
            L, [](lua_State *FL) {
                lua_gettable(FL, 1);
                return 1;
            },
            "instance_table_get");

    lua_getref(L, table_ref);
    LuaStackOp<Variant>::push(L, p_key);

    return lua_pcall(L, 2, 1, 0);
}

bool LuauScriptInstance::set(const StringName &p_name, const Variant &p_value, PropertySetGetError *r_err) {
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
            int status;

            if (prop.setter != StringName()) {
                LuaStackOp<Variant>::push(ET, p_value);
                status = call_internal(prop.setter, ET, 1, 0);
            } else {
                status = protected_table_set(ET, String(p_name), p_value);
            }

            lua_pop(T, 1); // thread

            if (status == LUA_OK) {
                if (r_err)
                    *r_err = PROP_OK;

                return true;
            } else if (status == LUA_YIELD) {
                if (r_err)
                    *r_err = PROP_SET_FAILED;

                ERR_FAIL_V_MSG(false, "setter for " + p_name + " yielded unexpectedly");
            } else if (status == -1) {
                if (r_err)
                    *r_err = PROP_NOT_FOUND;

                ERR_FAIL_V_MSG(false, "setter for " + p_name + " not found");
            } else {
                if (r_err)
                    *r_err = PROP_SET_FAILED;

                return false;
            }
        }

        if (s->methods.has(SET_NAME)) {
            lua_State *ET = lua_newthread(T);

            LuaStackOp<String>::push(ET, p_name);
            LuaStackOp<Variant>::push(ET, p_value);
            int status = call_internal(SET_NAME, ET, 2, 1);

            if (status == OK) {
                if (lua_type(ET, -1) != LUA_TBOOLEAN) {
                    if (r_err) {
                        *r_err = PROP_SET_FAILED;
                    }

                    LUAU_ERR("LuauScriptInstance::set", script, 1, String("expected " SET_NAME " to return a boolean"));
                } else {
                    bool valid = lua_toboolean(ET, -1);

                    if (valid) {
                        if (r_err) {
                            *r_err = PROP_OK;
                        }

                        lua_pop(T, 1); // thread
                        return true;
                    }
                }
            } else if (status != -1) {
                lua_pop(T, 1); // thread
                return false;
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
            int status;

            if (prop.getter != StringName()) {
                status = call_internal(prop.getter, ET, 0, 1);
            } else {
                status = protected_table_get(ET, String(p_name));
            }

            if (status == LUA_OK) {
                r_ret = LuaStackOp<Variant>::get(ET, -1);
                lua_pop(T, 1); // thread

                if (r_err)
                    *r_err = PROP_OK;

                return true;
            } else if (status == -1) {
                ERR_PRINT("getter for " + p_name + " not found");

                if (r_err)
                    *r_err = PROP_NOT_FOUND;
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
                if (!LuaStackOp<Variant>::is(ET, -1)) {
                    if (r_err) {
                        *r_err = PROP_GET_FAILED;
                    }

                    LUAU_ERR("LuauScriptInstance::get", script, 1, String("expected " GET_NAME " to return a Variant"));
                } else {
                    Variant ret = LuaStackOp<Variant>::get(ET, -1);

                    if (ret != Variant()) {
                        if (r_err) {
                            *r_err = PROP_OK;
                        }

                        r_ret = ret;
                        lua_pop(T, 1); // thread
                        return true;
                    }
                }
            } else if (status != -1) {
                lua_pop(T, 1); // thread
                return false;
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
#define GET_PROPERTY_LIST_NAME "_GetPropertyList"
#define GET_PROPERTY_ERR(err) LUAU_ERR("LuauScriptInstance::get_property_list", script, 1, String("failed to get custom property list: ") + err)

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
                GET_PROPERTY_ERR("expected " GET_PROPERTY_LIST_NAME " to return a table");
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

                                GDProperty *ret = reinterpret_cast<GDProperty *>(lua_newuserdatadtor(FL, sizeof(GDProperty), [](void *ptr) {
                                    ((GDProperty *)ptr)->~GDProperty();
                                }));

                                new (ret) GDProperty();
                                *ret = luascript_read_property(FL, -2);

                                lua_remove(FL, -2); // value
                            }

                            return sz;
                        },
                        "get_property_list");

                lua_insert(ET, 1);

                int get_status = lua_pcall(ET, 1, LUA_MULTRET, 0);
                if (get_status != LUA_OK) {
                    GET_PROPERTY_ERR(LuaStackOp<String>::get(ET, -1));
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
                LUAU_ERR("LuauScriptInstance::property_can_revert", script, 1, String("expected " PROPERTY_CAN_REVERT_NAME " to return a boolean"));
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
                LUAU_ERR("LuauScriptInstance::property_get_revert", script, 1, String("expected " PROPERTY_GET_REVERT_NAME " to return a Variant"));
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
                    ERR_FAIL_MSG("non-void method yielded unexpectedly");
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

#define DEF_GETTER(type, method_name, def_key)                                                     \
    const type *LuauScriptInstance::get_##method_name(const StringName &p_name) const {            \
        const LuauScript *s = script.ptr();                                                        \
                                                                                                   \
        while (s) {                                                                                \
            HashMap<StringName, type>::ConstIterator E = s->get_definition().def_key.find(p_name); \
                                                                                                   \
            if (E)                                                                                 \
                return &E->value;                                                                  \
                                                                                                   \
            s = s->base.ptr();                                                                     \
        }                                                                                          \
                                                                                                   \
        return nullptr;                                                                            \
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
DEF_GETTER(Variant, constant, constants)

LuauScriptInstance *LuauScriptInstance::from_object(Object *p_object) {
    if (!p_object)
        return nullptr;

    Ref<LuauScript> script = p_object->get_script();
    if (script.is_valid() && script->_instance_has(p_object))
        return script->instance_get(p_object);

    return nullptr;
}

LuauScriptInstance::LuauScriptInstance(Ref<LuauScript> p_script, Object *p_owner, GDLuau::VMType p_vm_type) :
        script(p_script), owner(p_owner), vm_type(p_vm_type) {
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
        CRASH_COND_MSG(!LuauLanguage::get_singleton()->is_core_script(p_script->get_path()), "!!! non-core script declared permissions !!!");
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
                int status = protected_table_set(T, String(prop.property.name), prop.default_value);
                ERR_FAIL_COND_MSG(status != LUA_OK, "Failed to set default value");
            }
        }

        // Run _Init for each script
        Error method_err = scr->load_definition(p_vm_type);

        if (method_err == OK) {
            LuaStackOp<String>::push(T, "_Init");
            scr->def_table_get(vm_type, T);

            if (lua_isfunction(T, -1)) {
                LuaStackOp<Object *>::push(T, p_owner);
                lua_getref(T, table_ref);

                int status = lua_pcall(T, 2, 0, 0);

                if (status == LUA_YIELD) {
                    ERR_PRINT(scr->get_path() + ":_Init yielded unexpectedly");
                } else if (status != LUA_OK) {
                    ERR_PRINT(scr->get_path() + ":_Init failed: " + LuaStackOp<String>::get(T, -1));
                    lua_pop(T, 1);
                }
            } else {
                lua_pop(T, 1);
            }
        } else {
            ERR_PRINT("Couldn't load script methods for " + scr->get_path());
        }
    }
}

LuauScriptInstance::~LuauScriptInstance() {
    if (script.is_valid() && owner) {
        MutexLock lock(*LuauLanguage::singleton->lock.ptr());
        script->instances.erase(owner->get_instance_id());
    }

    lua_State *L = GDLuau::get_singleton()->get_vm(vm_type);
    LUAU_LOCK(L);

    lua_unref(L, table_ref);
    table_ref = 0;

    lua_unref(L, thread_ref);
    thread_ref = 0;
}

//////////////
// LANGUAGE //
//////////////

LuauLanguage *LuauLanguage::singleton = nullptr;

LuauLanguage::LuauLanguage() {
    singleton = this;
    lock.instantiate();
}

LuauLanguage::~LuauLanguage() {
    finalize();
    singleton = nullptr;
}

void LuauLanguage::discover_core_scripts(const String &path) {
    // Will be scanned on Windows
    if (path == "res://.godot") {
        return;
    }

    UtilityFunctions::print_verbose("Searching for core scripts in ", path, "...");

    Ref<DirAccess> dir = DirAccess::open(path);
    ERR_FAIL_COND_MSG(!dir.is_valid(), "Failed to open directory");

    Error err = dir->list_dir_begin();
    ERR_FAIL_COND_MSG(err != OK, "Failed to list directory");

    String file_name = dir->get_next();

    while (!file_name.is_empty()) {
        String file_name_full = path.path_join(file_name);

        if (dir->current_is_dir()) {
            discover_core_scripts(file_name_full);
        } else {
            String res_type = ResourceFormatLoaderLuauScript::get_resource_type(file_name_full);

            if (res_type == LuauLanguage::get_singleton()->_get_type()) {
                UtilityFunctions::print_verbose("Discovered core script: ", file_name_full);
                core_scripts.insert(file_name_full);
            }
        }

        file_name = dir->get_next();
    }
}

void LuauLanguage::_init() {
    luau = memnew(GDLuau);
    cache = memnew(LuauCache);

    UtilityFunctions::print_verbose("======== Discovering core scripts ========");
    discover_core_scripts();
    UtilityFunctions::print_verbose("Done! ", core_scripts.size(), " core scripts discovered.");
    UtilityFunctions::print_verbose("==========================================");
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
    uint64_t new_ticks = Time::get_singleton()->get_ticks_usec();
    double time_scale = Engine::get_singleton()->get_time_scale();

    double delta;
    if (ticks_usec == 0)
        delta = 0;
    else
        delta = (new_ticks - ticks_usec) / 1e6f;

    task_scheduler.frame(delta * time_scale);

    ticks_usec = new_ticks;
}

Error LuauLanguage::_execute_file(const String &p_path) {
    // Unused by Godot; purpose unclear
    return OK;
}

bool LuauLanguage::_has_named_classes() const {
    // not true for any of Godot's built in languages. why
    return false;
}

bool LuauLanguage::is_core_script(const String &p_path) const {
    return core_scripts.has(p_path);
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
    return p_path.get_extension().to_lower() == "lua" ? LuauLanguage::get_singleton()->_get_type() : "";
}

String ResourceFormatLoaderLuauScript::_get_resource_type(const String &p_path) const {
    return get_resource_type(p_path);
}

Variant ResourceFormatLoaderLuauScript::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
    Error err;
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
        ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), "Cannot save Luau script file '" + p_path + "'.");

        file->store_string(source);

        if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF)
            return ERR_CANT_CREATE;
    }

    // TODO: Godot's default language implementations have a check here. It isn't possible in extensions (yet).
    // if (ScriptServer::is_reload_scripts_on_save_enabled())
    LuauLanguage::get_singleton()->_reload_tool_script(p_resource, false);

    return OK;
}
