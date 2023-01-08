#include "luau_script.h"

#include <Luau/Compiler.h>
#include <lua.h>
#include <string.h>
#include <string>

#include <gdextension_interface.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "gd_luau.h"
#include "luagd_stack.h"
#include "luagd_utils.h"
#include "luau_cache.h"
#include "luau_lib.h"

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

#define LUAU_LOAD_ERR(script, line, msg) _err_print_error("LuauScript::_reload", script->get_path().is_empty() ? "built-in" : script->get_path().utf8().get_data(), line, msg);
#define LUAU_LOAD_YIELD_MSG "Luau Error: Script yielded when loading definition."
#define LUAU_LOAD_NO_DEF_MSG "Luau Error: Script did not return a valid class definition."

#define LUAU_LOAD_RESUME(script)                        \
    int status = lua_resume(T, nullptr, 0);             \
                                                        \
    if (status == LUA_YIELD) {                          \
        LUAU_LOAD_ERR(script, 1, LUAU_LOAD_YIELD_MSG);  \
        return ERR_COMPILATION_FAILED;                  \
    } else if (status != LUA_OK) {                      \
        String err = LuaStackOp<String>::get(T, -1);    \
        LUAU_LOAD_ERR(script, 1, "Luau Error: " + err); \
                                                        \
        return ERR_COMPILATION_FAILED;                  \
    }

static Error try_load(lua_State *L, const char *src) {
    Luau::CompileOptions opts;
    std::string bytecode = Luau::compile(src, opts);

    return luau_load(L, "=load", bytecode.data(), bytecode.size(), 0) == 0 ? OK : ERR_COMPILATION_FAILED;
}

static bool class_exists(const StringName &class_name) {
    // TODO: the real ClassDB is not available in godot-cpp yet. this is what we get.
    static Object *class_db = Engine::get_singleton()->get_singleton("ClassDB");
    return class_db->call("class_exists", class_name).operator bool();
}

static Error get_class_definition(Ref<LuauScript> script, const String &source, GDClassDefinition &r_def, bool &r_is_valid) {
    // TODO: error line numbers?

    r_is_valid = false;

    lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_SCRIPT_LOAD);
    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    if (try_load(T, source.utf8().get_data()) == OK) {
        LUAU_LOAD_RESUME(script)

        if (lua_isnil(T, 1) || lua_type(T, 1) != LUA_TTABLE) {
            lua_pop(L, 1); // thread
            LUAU_LOAD_ERR(script, 1, LUAU_LOAD_NO_DEF_MSG);

            return ERR_COMPILATION_FAILED;
        }

        r_def = luascript_read_class(T, 1);
        r_is_valid = true;

        lua_pop(L, 1); // thread
        return OK;
    }

    String err = LuaStackOp<String>::get(T, -1);
    LUAU_LOAD_ERR(script, 1, "Luau Error: " + err);

    lua_pop(L, 1); // thread

    return ERR_COMPILATION_FAILED;
}

void LuauScript::update_base_script(Error &r_error) {
    r_error = OK;

    if (!get_path().is_empty()) {
        base_dir = get_path().get_base_dir();

        if (!definition.extends.is_empty() && !class_exists(definition.extends)) {
            String base_script_path = base_dir.path_join(definition.extends);
            base = LuauCache::get_singleton()->get_script(base_script_path, r_error);
        }
    }
}

Error LuauScript::_reload(bool p_keep_state) {
    bool has_instances;

    {
        MutexLock lock(LuauLanguage::singleton->lock);
        has_instances = instances.size();
    }

    ERR_FAIL_COND_V(!p_keep_state && has_instances, ERR_ALREADY_IN_USE);

    Error err = get_class_definition(this, source, definition, valid);

    if (err != OK) {
        return err;
    }

    update_base_script(err);
    if (err != OK || (base.is_valid() && !base->_is_valid()))
        valid = false;

    for (const KeyValue<GDLuau::VMType, int> &pair : def_table_refs) {
        if (load_methods(pair.key, true) == OK)
            continue;

        valid = false;
        return ERR_COMPILATION_FAILED;
    }

    return OK;
}

Error LuauScript::load_methods(GDLuau::VMType p_vm_type, bool force) {
    if (!force && def_table_refs.has(p_vm_type))
        return ERR_SKIP;

    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    if (try_load(T, source.utf8().get_data()) == OK) {
        LUAU_LOAD_RESUME(this)

        if (lua_gettop(T) < 1) {
            LUAU_LOAD_ERR(this, 1, LUAU_LOAD_NO_DEF_MSG);
            return ERR_COMPILATION_FAILED;
        }

        if (def_table_refs.has(p_vm_type))
            lua_unref(L, def_table_refs[p_vm_type]);

        def_table_refs[p_vm_type] = lua_ref(T, -1);
        lua_pop(L, 1); // thread

        return OK;
    }

    String err = LuaStackOp<String>::get(T, -1);
    LUAU_LOAD_ERR(this, 1, "Luau Error: " + err);

    lua_pop(L, 1); // thread

    return ERR_COMPILATION_FAILED;
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
    return valid && (_is_tool() || !Engine::get_singleton()->is_editor_hint());
}

bool LuauScript::_is_tool() const {
    return definition.is_tool;
}

StringName LuauScript::_get_instance_base_type() const {
    StringName extends = StringName(definition.extends);

    if (extends != StringName() && class_exists(extends)) {
        return extends;
    }

    if (base.is_valid() && base->_is_valid())
        return base->_get_instance_base_type();

    return StringName();
}

Ref<Script> LuauScript::_get_base_script() const {
    return base;
};

bool LuauScript::_inherits_script(const Ref<Script> &p_script) const {
    Ref<LuauScript> script = p_script;
    if (script.is_null())
        return false;

    const LuauScript *s = this;

    while (s != nullptr) {
        if (s == script.ptr())
            return true;

        s = s->base.ptr();
    }

    return false;
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

static String to_pascal_case(const String &input) {
    String out = input.to_pascal_case();

    // to_pascal_case strips leading/trailing underscores. leading is most common and this handles that
    for (int i = 0; i < input.length() && input[i] == '_'; i++)
        out = "_" + out;

    return out;
}

bool LuauScript::has_method(const StringName &p_method, StringName *r_actual_name) const {
    if (definition.methods.has(p_method))
        return true;

    StringName pascal_name = to_pascal_case(p_method);

    if (definition.methods.has(pascal_name)) {
        if (r_actual_name != nullptr)
            *r_actual_name = pascal_name;

        return true;
    }

    return false;
}

Dictionary LuauScript::_get_method_info(const StringName &p_method) const {
    return definition.methods.get(p_method);
}

TypedArray<Dictionary> LuauScript::_get_script_property_list() const {
    TypedArray<Dictionary> properties;

    for (const KeyValue<StringName, GDClassProperty> &pair : definition.properties)
        properties.push_back(pair.value.property.operator Dictionary());

    return properties;
}

TypedArray<StringName> LuauScript::_get_members() const {
    // TODO: 2022-10-08: evil witchery garbage
    // segfault occurs when initializing with TypedArray<StringName> and relying on copy.
    // conversion works fine. for some reason.
    Array members;

    for (const KeyValue<StringName, GDClassProperty> &pair : definition.properties)
        members.push_back(pair.value.property.name);

    return members;
}

bool LuauScript::_has_property_default_value(const StringName &p_property) const {
    HashMap<StringName, GDClassProperty>::ConstIterator E = definition.properties.find(p_property);

    if (E && E->value.default_value != Variant())
        return true;

    if (base.is_valid())
        return base->_has_property_default_value(p_property);

    return false;
}

Variant LuauScript::_get_property_default_value(const StringName &p_property) const {
    HashMap<StringName, GDClassProperty>::ConstIterator E = definition.properties.find(p_property);

    if (E && E->value.default_value != Variant())
        return E->value.default_value;

    if (base.is_valid())
        return base->_get_property_default_value(p_property);

    return Variant();
}

bool LuauScript::has_property(const StringName &p_name) const {
    return definition.properties.has(p_name);
}

const GDClassProperty &LuauScript::get_property(const StringName &p_name) const {
    return definition.properties.get(p_name);
}

void *LuauScript::_instance_create(Object *p_for_object) const {
    // TODO: decide which vm to use
    LuauScriptInstance *internal = memnew(LuauScriptInstance(Ref<Script>(this), p_for_object, GDLuau::VMType::VM_CORE));

    return internal::gde_interface->script_instance_create(&LuauScriptInstance::INSTANCE_INFO, internal);
}

bool LuauScript::_instance_has(Object *p_object) const {
    MutexLock lock(LuauLanguage::singleton->lock);
    return instances.has(p_object->get_instance_id());
}

LuauScriptInstance *LuauScript::instance_get(Object *p_object) const {
    MutexLock lock(LuauLanguage::singleton->lock);
    return instances.get(p_object->get_instance_id());
}

void *LuauScript::_placeholder_instance_create(Object *p_for_object) const {
    PlaceHolderScriptInstance *internal = memnew(PlaceHolderScriptInstance(Ref<LuauScript>(this), p_for_object));

    return internal::gde_interface->script_instance_create(&PlaceHolderScriptInstance::INSTANCE_INFO, internal);
}

// Based on GDScript implementation. See license information in README.md.
void LuauScript::_update_exports() {
    bool cyclic_error = false;
    update_exports_internal(&cyclic_error, false, nullptr);

    if (cyclic_error)
        return;

    // Update inheriting scripts
    HashSet<uint64_t> copy = inheriters_cache; // Gets modified by _update_exports

    for (const uint64_t &E : copy) {
        Object *ptr = ObjectDB::get_instance(E);
        LuauScript *s = Object::cast_to<LuauScript>(ptr);

        if (s == nullptr)
            continue;

        s->_update_exports();
    }
}

void LuauScript::update_exports_values(List<GDProperty> &properties, HashMap<StringName, Variant> &values) {
    for (const KeyValue<StringName, GDClassProperty> &pair : definition.properties) {
        const GDClassProperty &prop = pair.value;

        properties.push_back(prop.property);
        values[pair.key] = prop.default_value;
    }

    if (base.is_valid()) {
        base->update_exports_values(properties, values);
    }
}

// `p_recursive_call` indicates whether this method was called from within this method.
bool LuauScript::update_exports_internal(bool *r_err, bool p_recursive_call, PlaceHolderScriptInstance *p_instance_to_update) {
    // Step 1: Base cache
    static Vector<LuauScript *> base_caches;
    if (!p_recursive_call) {
        base_caches.clear();
    }

    base_caches.append(this);

    // Step 2: Reload base class, properties, and signals from source
    bool changed = false;

    if (source_changed_cache) {
        source_changed_cache = false;
        changed = true;

        GDClassDefinition def;
        bool is_valid;
        Error err = get_class_definition(this, source, def, is_valid);

        if (err == OK) {
            // Update base class, inheriter cache
            if (base.is_valid()) {
                base->inheriters_cache.erase(get_instance_id());
                base = Ref<LuauScript>();
            }

            definition.extends = def.extends;

            Error err;
            update_base_script(err);

            if (base.is_valid()) {
                base->inheriters_cache.insert(get_instance_id());
            }

            // Update properties, signals
            // TODO: Signals
            definition.properties = def.properties;
        } else {
            placeholder_fallback_enabled = true;
            return false;
        }
    } else if (placeholder_fallback_enabled) {
        return false;
    }

    placeholder_fallback_enabled = false;

    // Step 3: Check base cache for cyclic inheritance
    // See the GDScript implementation.
    if (base.is_valid() && base->_is_valid()) {
        for (int i = 0; i < base_caches.size(); i++) {
            if (base_caches[i] == base.ptr()) {
                if (r_err != nullptr) {
                    *r_err = true;
                }

                valid = false; // Show error in editor
                base->valid = false;
                base->inheriters_cache.clear(); // Prevents stack overflows
                base = Ref<LuauScript>();
                ERR_FAIL_V_MSG(false, "Cyclic inheritance in script class.");
            }
        }

        if (base->update_exports_internal(r_err, true, nullptr)) {
            if (r_err != nullptr && *r_err) {
                return false;
            }

            changed = true;
        }
    }

    // Step 4: Update placeholder instances
    if ((changed || p_instance_to_update != nullptr) && placeholders.size() > 0) {
        List<GDProperty> properties;
        HashMap<StringName, Variant> values;

        update_exports_values(properties, values);

        if (changed) {
            for (const KeyValue<uint64_t, PlaceHolderScriptInstance *> &pair : placeholders) {
                pair.value->update(properties, values);
            }
        } else {
            p_instance_to_update->update(properties, values);
        }
    }

    return changed;
}

void LuauScript::_placeholder_erased(void *p_placeholder) {
    placeholders.erase(((PlaceHolderScriptInstance *)p_placeholder)->get_owner()->get_instance_id());
}

bool LuauScript::placeholder_has(Object *p_object) const {
    return placeholders.has(p_object->get_instance_id());
}

PlaceHolderScriptInstance *LuauScript::placeholder_get(Object *p_object) {
    return placeholders.get(p_object->get_instance_id());
}

void LuauScript::def_table_get(GDLuau::VMType p_vm_type, lua_State *T) const {
    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    ERR_FAIL_COND_MSG(lua_mainthread(L) != lua_mainthread(T), "cannot push definition table to a thread from a different VM than the one being queried");

    lua_getref(T, def_table_refs[p_vm_type]);
    lua_insert(T, -2);
    lua_gettable(T, -2);
    lua_remove(T, -2);
}

/////////////////////
// SCRIPT INSTANCE //
/////////////////////

#define INSTANCE_SELF ((LuauScriptInstance *)self)

static GDExtensionScriptInstanceInfo init_script_instance_info() {
    GDExtensionScriptInstanceInfo info;

    info.set_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) -> GDExtensionBool {
        return INSTANCE_SELF->set(*((const StringName *)p_name), *((const Variant *)p_value));
    };

    info.get_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return INSTANCE_SELF->get(*((const StringName *)p_name), *((Variant *)r_ret));
    };

    info.get_property_list_func = [](void *self, uint32_t *r_count) -> const GDExtensionPropertyInfo * {
        return INSTANCE_SELF->get_property_list(r_count);
    };

    info.free_property_list_func = [](void *self, const GDExtensionPropertyInfo *p_list) {
        INSTANCE_SELF->free_property_list(p_list);
    };

    info.get_property_type_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) -> GDExtensionVariantType {
        return static_cast<GDExtensionVariantType>(INSTANCE_SELF->get_property_type(*((const StringName *)p_name), (bool *)r_is_valid));
    };

    info.property_can_revert_func = [](void *self, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        return INSTANCE_SELF->property_can_revert(*((StringName *)p_name));
    };

    info.property_get_revert_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return INSTANCE_SELF->property_get_revert(*((StringName *)p_name), (Variant *)r_ret);
    };

    info.get_owner_func = [](void *self) {
        return INSTANCE_SELF->get_owner()->_owner;
    };

    info.get_property_state_func = [](void *self, GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
        INSTANCE_SELF->get_property_state(p_add_func, p_userdata);
    };

    info.get_method_list_func = [](void *self, uint32_t *r_count) -> const GDExtensionMethodInfo * {
        return INSTANCE_SELF->get_method_list(r_count);
    };

    info.free_method_list_func = [](void *self, const GDExtensionMethodInfo *p_list) {
        INSTANCE_SELF->free_method_list(p_list);
    };

    info.has_method_func = [](void *self, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        return INSTANCE_SELF->has_method(*((const StringName *)p_name));
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

    info.get_script_func = [](void *self) {
        return INSTANCE_SELF->get_script().ptr()->_owner;
    };

    info.get_language_func = [](void *self) {
        return INSTANCE_SELF->get_language()->_owner;
    };

    info.free_func = [](void *self) {
        memdelete(INSTANCE_SELF);
    };

    return info;
}

const GDExtensionScriptInstanceInfo LuauScriptInstance::INSTANCE_INFO = init_script_instance_info();

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

static void copy_prop(const GDProperty &src, GDExtensionPropertyInfo &dst) {
    dst.type = src.type;
    dst.name = stringname_alloc(src.name);
    dst.class_name = stringname_alloc(src.class_name);
    dst.hint = src.hint;
    dst.hint_string = string_alloc(src.hint_string);
    dst.usage = src.usage;
}

static void free_prop(const GDExtensionPropertyInfo &prop) {
    // smelly
    memdelete((StringName *)prop.name);
    memdelete((StringName *)prop.class_name);
    memdelete((String *)prop.hint_string);
}

int LuauScriptInstance::call_internal(const StringName &p_method, lua_State *ET, int nargs, int nret) {
    LuauScript *s = script.ptr();

    while (s != nullptr) {
        LuaStackOp<String>::push(ET, p_method);
        s->def_table_get(vm_type, ET);

        if (!lua_isnil(ET, -1)) {
            if (lua_type(ET, -1) != LUA_TFUNCTION)
                luaGD_valueerror(ET, String(p_method).utf8().get_data(), luaGD_typename(ET, -1), "function");

            lua_insert(ET, -nargs - 1);

            LuaStackOp<Object *>::push(ET, owner);
            lua_insert(ET, -nargs - 1);

            int status = lua_pcall(ET, nargs + 1, nret, 0); // +1 for self

            if (status != LUA_OK) {
                ERR_PRINT("Lua Error: " + LuaStackOp<String>::get(ET, -1));
                lua_pop(ET, 1);
            }

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
    LuauScript *s = script.ptr();

    while (s != nullptr) {
        if (s->definition.properties.has(p_name)) {
            const GDClassProperty &prop = s->definition.properties[p_name];

            // Check type
            if (prop.property.type != GDEXTENSION_VARIANT_TYPE_NIL && (GDExtensionVariantType)p_value.get_type() != prop.property.type) {
                if (r_err != nullptr)
                    *r_err = PROP_WRONG_TYPE;

                return false;
            }

            // Check read-only (getter, no setter)
            if (prop.setter == StringName() && prop.getter != StringName()) {
                if (r_err != nullptr)
                    *r_err = PROP_READ_ONLY;

                return false;
            }

            // Prepare for set
            lua_State *ET = lua_newthread(T);
            int status;

            // Set
            if (prop.setter != StringName()) {
                LuaStackOp<Variant>::push(ET, p_value);
                status = call_internal(prop.setter, ET, 1, 0);
            } else {
                status = protected_table_set(ET, String(p_name), p_value);
            }

            lua_pop(T, 1); // thread

            if (status == LUA_OK) {
                if (r_err != nullptr)
                    *r_err = PROP_OK;

                return true;
            } else if (status == -1) {
                ERR_PRINT("setter for " + p_name + " not found");

                if (r_err != nullptr)
                    *r_err = PROP_NOT_FOUND;

                return false;
            } else {
                if (r_err != nullptr)
                    *r_err = PROP_SET_FAILED;

                return false;
            }
        }

        s = s->base.ptr();
    }

    if (r_err != nullptr)
        *r_err = PROP_NOT_FOUND;

    return false;
}

bool LuauScriptInstance::get(const StringName &p_name, Variant &r_ret, PropertySetGetError *r_err) {
    LuauScript *s = script.ptr();

    while (s != nullptr) {
        if (s->definition.properties.has(p_name)) {
            const GDClassProperty &prop = s->definition.properties[p_name];

            // Check write-only (setter, no getter)
            if (prop.setter != StringName() && prop.getter == StringName()) {
                if (r_err != nullptr)
                    *r_err = PROP_WRITE_ONLY;

                return false;
            }

            // Prepare for get
            lua_State *ET = lua_newthread(T);
            int status;

            // Get
            if (prop.getter != StringName()) {
                status = call_internal(prop.getter, ET, 0, 1);
            } else {
                status = protected_table_get(ET, String(p_name));
            }

            if (status == LUA_OK) {
                r_ret = LuaStackOp<Variant>::get(ET, -1);
                lua_pop(T, 1); // thread

                if (r_err != nullptr)
                    *r_err = PROP_OK;

                return true;
            } else if (status == -1) {
                ERR_PRINT("getter for " + p_name + " not found");

                if (r_err != nullptr)
                    *r_err = PROP_NOT_FOUND;
            } else {
                if (r_err != nullptr)
                    *r_err = PROP_GET_FAILED;
            }

            lua_pop(T, 1); // thread

            return false;
        }

        s = s->base.ptr();
    }

    if (r_err != nullptr)
        *r_err = PROP_NOT_FOUND;

    return false;
}

const GDClassProperty *LuauScriptInstance::get_property(const StringName &p_name) const {
    const LuauScript *s = script.ptr();

    while (s != nullptr) {
        if (s->has_property(p_name))
            return &s->get_property(p_name);

        s = s->base.ptr();
    }

    return nullptr;
}

template <typename T>
static void get_property_state_static(T &self, GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
    // ! refer to script_language.cpp get_property_state
    // the default implementation is not carried over to GDExtension

    uint32_t count;
    GDExtensionPropertyInfo *props = self.get_property_list(&count);

    for (int i = 0; i < count; i++) {
        StringName *name = (StringName *)props[i].name;

        if (props[i].usage & PROPERTY_USAGE_STORAGE) {
            Variant value;
            bool is_valid = self.get(*name, value);

            if (is_valid)
                p_add_func(name, &value, p_userdata);
        }
    }

    self.free_property_list(props);
}

void LuauScriptInstance::get_property_state(GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
    get_property_state_static(*this, p_add_func, p_userdata);
}

// allocates list with an int at the front saying how long it is
// it's a miracle, really, that this doesn't segfault.
// reminder: arithmetic on a pointer increments by the size of the type and not bytes
template <typename T>
static T *alloc_with_len(int size) {
    uint64_t list_size = sizeof(T) * size;
    void *ptr = memalloc(list_size + sizeof(int));

    *((int *)ptr) = size;

    return (T *)((int *)ptr + 1);
}

static void free_with_len(void *ptr) {
    memfree((int *)ptr - 1);
}

static int get_len_from_ptr(const void *ptr) {
    return *((int *)ptr - 1);
}

static void free_property_list_static(const GDExtensionPropertyInfo *p_list) {
    // don't ask.
    int size = get_len_from_ptr(p_list);

    for (int i = 0; i < size; i++)
        free_prop(p_list[i]);

    free_with_len((GDExtensionPropertyInfo *)p_list);
}

GDExtensionPropertyInfo *LuauScriptInstance::get_property_list(uint32_t *r_count) const {
    Vector<GDExtensionPropertyInfo> properties;
    HashSet<StringName> defined;

    const LuauScript *s = script.ptr();

    while (s != nullptr) {
        for (const KeyValue<StringName, GDClassProperty> &pair : s->definition.properties) {
            if (defined.has(pair.key))
                continue;

            defined.insert(pair.key);

            GDExtensionPropertyInfo dst;
            copy_prop(pair.value.property, dst);

            properties.push_back(dst);
        }

        s = s->base.ptr();
    }

    int size = properties.size();
    *r_count = size;

    GDExtensionPropertyInfo *list = alloc_with_len<GDExtensionPropertyInfo>(size);
    memcpy(list, properties.ptr(), sizeof(GDExtensionPropertyInfo) * size);

    return list;
}

void LuauScriptInstance::free_property_list(const GDExtensionPropertyInfo *p_list) const {
    free_property_list_static(p_list);
}

Variant::Type LuauScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
    const LuauScript *s = script.ptr();

    while (s != nullptr) {
        if (s->definition.properties.has(p_name)) {
            if (r_is_valid != nullptr)
                *r_is_valid = true;

            return (Variant::Type)s->definition.properties[p_name].property.type;
        }

        s = s->base.ptr();
    }

    if (r_is_valid != nullptr)
        *r_is_valid = false;

    return Variant::NIL;
}

// TODO: these two methods are for custom properties via virtual _property_can_revert, _property_get_revert, _get, _set
// functionality for _get and _set should be part of ScriptInstance::get/set
bool LuauScriptInstance::property_can_revert(const StringName &p_name) const {
    return false;
}

bool LuauScriptInstance::property_get_revert(const StringName &p_name, Variant *r_ret) const {
    return false;
}

Object *LuauScriptInstance::get_owner() const {
    return owner;
}

static GDExtensionMethodInfo *get_method_list_static(Ref<LuauScript> p_script, uint32_t *r_count) {
    Vector<GDExtensionMethodInfo> methods;
    HashSet<StringName> defined;

    const LuauScript *s = p_script.ptr();

    while (s != nullptr) {
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

        s = s->base.ptr();
    }

    int size = methods.size();
    *r_count = size;

    GDExtensionMethodInfo *list = alloc_with_len<GDExtensionMethodInfo>(size);
    memcpy(list, methods.ptr(), sizeof(GDExtensionMethodInfo) * size);

    return list;
}

static void free_method_list_static(const GDExtensionMethodInfo *p_list) {
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

GDExtensionMethodInfo *LuauScriptInstance::get_method_list(uint32_t *r_count) const {
    return get_method_list_static(script, r_count);
}

void LuauScriptInstance::free_method_list(const GDExtensionMethodInfo *p_list) const {
    free_method_list_static(p_list);
}

bool LuauScriptInstance::has_method(const StringName &p_name) const {
    const LuauScript *s = script.ptr();

    while (s != nullptr) {
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
    LuauScript *s = script.ptr();

    while (s != nullptr) {
        StringName actual_name = p_method;

        // check name given and name converted to pascal
        // (e.g. if Node::_ready is called -> _Ready)
        if (s->has_method(p_method, &actual_name)) {
            const GDMethod &method = s->definition.methods[actual_name];

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

                if ((GDExtensionVariantType)arg.get_type() != method.arguments[i].type) {
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

            if (status == LUA_OK)
                *r_return = LuaStackOp<Variant>::get(ET, -1);

            lua_pop(T, 1); // thread

            return;
        }

        s = s->base.ptr();
    }

    r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
}

void LuauScriptInstance::notification(int32_t p_what) {
    LuauScript *s = script.ptr();

    while (s != nullptr) {
        // TODO: cache whether user created _Notification in class definition to avoid having to make a thread, etc. to check

        lua_State *ET = lua_newthread(T);

        LuaStackOp<int32_t>::push(ET, p_what);
        call_internal("_Notification", ET, 1, 0);

        lua_pop(T, 1); // thread

        s = s->base.ptr();
    }
}

void LuauScriptInstance::to_string(GDExtensionBool *r_is_valid, String *r_out) {
    LuauScript *s = script.ptr();

    while (s != nullptr) {
        // TODO: cache whether user created _ToString in class definition to avoid having to make a thread, etc. to check

        lua_State *ET = lua_newthread(T);

        int status = call_internal("_ToString", ET, 0, 1);

        if (status != -1) {
            if (status == LUA_OK)
                *r_out = LuaStackOp<String>::get(ET, -1);

            if (r_is_valid != nullptr)
                *r_is_valid = status == LUA_OK;

            return;
        }

        lua_pop(T, 1); // thread

        s = s->base.ptr();
    }
}

Ref<Script> LuauScriptInstance::get_script() const {
    return script;
}

ScriptLanguage *LuauScriptInstance::get_language() const {
    return LuauLanguage::get_singleton();
}

bool LuauScriptInstance::table_set(lua_State *T) const {
    if (lua_mainthread(T) != lua_mainthread(L))
        return false;

    lua_getref(T, table_ref);
    lua_insert(T, -3);
    lua_settable(T, -3);
    lua_remove(T, -1);

    return true;
}

bool LuauScriptInstance::table_get(lua_State *T) const {
    if (lua_mainthread(T) != lua_mainthread(L))
        return false;

    lua_getref(T, table_ref);
    lua_insert(T, -2);
    lua_gettable(T, -2);
    lua_remove(T, -2);

    return true;
}

LuauScriptInstance::LuauScriptInstance(Ref<LuauScript> p_script, Object *p_owner, GDLuau::VMType p_vm_type) :
        script(p_script), owner(p_owner), vm_type(p_vm_type) {
    // this usually occurs in _instance_create, but that is marked const for ScriptExtension
    {
        MutexLock lock(LuauLanguage::singleton->lock);
        p_script->instances.insert(p_owner->get_instance_id(), this);
    }

    L = GDLuau::get_singleton()->get_vm(p_vm_type);
    T = lua_newthread(L);
    luaL_sandboxthread(T);
    thread_ref = lua_ref(L, -1);

    lua_newtable(T);
    table_ref = lua_ref(T, -1);

    LuauScript *s = p_script.ptr();

    while (s != nullptr) {
        // Initialize default values
        for (const KeyValue<StringName, GDClassProperty> pair : s->definition.properties) {
            const GDClassProperty &prop = pair.value;

            if (prop.getter == StringName() && prop.setter == StringName()) {
                int status = protected_table_set(T, String(pair.key), prop.default_value);
                ERR_FAIL_COND_MSG(status != LUA_OK, "Failed to set default value");
            }
        }

        // Run _Init for each script
        Error method_err = s->load_methods(p_vm_type);

        if (method_err == OK || method_err == ERR_SKIP) {
            LuaStackOp<String>::push(T, "_Init");
            s->def_table_get(vm_type, T);

            if (!lua_isnil(T, -1)) {
                if (lua_type(T, -1) != LUA_TFUNCTION)
                    luaGD_valueerror(T, "_Init", luaGD_typename(T, -1), "function");

                LuaStackOp<Object *>::push(T, p_owner);
                lua_getref(T, table_ref);

                int status = lua_pcall(T, 2, 0, 0);

                if (status == LUA_YIELD) {
                    ERR_PRINT(p_script->definition.name + ":_Init yielded unexpectedly");
                } else if (status != LUA_OK) {
                    ERR_PRINT(p_script->definition.name + ":_Init failed: " + LuaStackOp<String>::get(T, -1));
                    lua_pop(T, 1);
                }
            } else {
                lua_pop(T, 1);
            }
        } else {
            ERR_PRINT("Couldn't load script methods for " + p_script->definition.name);
        }

        s = s->base.ptr();
    }
}

LuauScriptInstance::~LuauScriptInstance() {
    if (script.is_valid() && owner != nullptr) {
        MutexLock lock(LuauLanguage::singleton->lock);
        script->instances.erase(owner->get_instance_id());
    }

    lua_unref(L, table_ref);
    table_ref = 0;

    lua_unref(L, thread_ref);
    thread_ref = 0;
}

//////////////////////////
// PLACEHOLDER INSTANCE //
//////////////////////////

// Implementation mostly mirrors Godot's implementation.
// See license information in README.md.

#define PLACEHOLDER_SELF ((PlaceHolderScriptInstance *)self)

static GDExtensionScriptInstanceInfo init_placeholder_instance_info() {
    // Methods which essentially have no utility (e.g. call) are implemented here instead of in the class.

    GDExtensionScriptInstanceInfo info;

    info.set_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) -> GDExtensionBool {
        return PLACEHOLDER_SELF->set(*(const StringName *)p_name, *(const Variant *)p_value);
    };

    info.get_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return PLACEHOLDER_SELF->get(*(const StringName *)p_name, *(Variant *)r_ret);
    };

    info.get_property_list_func = [](void *self, uint32_t *r_count) -> const GDExtensionPropertyInfo * {
        return PLACEHOLDER_SELF->get_property_list(r_count);
    };

    info.free_property_list_func = [](void *self, const GDExtensionPropertyInfo *p_list) {
        PLACEHOLDER_SELF->free_property_list(p_list);
    };

    info.property_can_revert_func = [](void *, GDExtensionConstStringNamePtr) -> GDExtensionBool {
        return false;
    };

    info.property_get_revert_func = [](void *, GDExtensionConstStringNamePtr, GDExtensionVariantPtr) -> GDExtensionBool {
        return false;
    };

    info.get_owner_func = [](void *self) {
        return PLACEHOLDER_SELF->get_owner()->_owner;
    };

    info.get_property_state_func = [](void *self, GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
        PLACEHOLDER_SELF->get_property_state(p_add_func, p_userdata);
    };

    info.get_method_list_func = [](void *self, uint32_t *r_count) -> const GDExtensionMethodInfo * {
        return PLACEHOLDER_SELF->get_method_list(r_count);
    };

    info.free_method_list_func = [](void *self, const GDExtensionMethodInfo *p_list) {
        PLACEHOLDER_SELF->free_method_list(p_list);
    };

    info.get_property_type_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) -> GDExtensionVariantType {
        return (GDExtensionVariantType)PLACEHOLDER_SELF->get_property_type(*(const StringName *)p_name, (bool *)r_is_valid);
    };

    info.has_method_func = [](void *self, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        return PLACEHOLDER_SELF->has_method(*(const StringName *)p_name);
    };

    info.call_func = [](void *self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
        r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
        *(Variant *)r_return = Variant();
    };

    info.get_script_func = [](void *self) {
        return PLACEHOLDER_SELF->get_script().ptr()->_owner;
    };

    info.is_placeholder_func = [](void *) -> GDExtensionBool {
        return true;
    };

    info.set_fallback_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) -> GDExtensionBool {
        return PLACEHOLDER_SELF->property_set_fallback(*(const StringName *)p_name, *(const Variant *)p_value);
    };

    info.get_fallback_func = [](void *self, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        return PLACEHOLDER_SELF->property_get_fallback(*(const StringName *)p_name, *(Variant *)r_ret);
    };

    info.get_language_func = [](void *self) {
        return PLACEHOLDER_SELF->get_language()->_owner;
    };

    info.free_func = [](void *self) {
        memdelete(PLACEHOLDER_SELF);
    };

    return info;
}

const GDExtensionScriptInstanceInfo PlaceHolderScriptInstance::INSTANCE_INFO = init_placeholder_instance_info();

bool PlaceHolderScriptInstance::set(const StringName &p_name, const Variant &p_value) {
    if (script->_is_placeholder_fallback_enabled())
        return false;

    if (values.has(p_name)) {
        if (script->_has_property_default_value(p_name)) {
            Variant defval = script->_get_property_default_value(p_name);

            Variant op_result;
            bool op_valid = false;
            Variant::evaluate(Variant::OP_EQUAL, defval, p_value, op_result, op_valid);

            if (op_valid && op_result.operator bool()) {
                values.erase(p_name);
                return true;
            }
        }

        values[p_name] = p_value;
        return true;
    } else {
        if (script->_has_property_default_value(p_name)) {
            Variant defval = script->get_property_default_value(p_name);

            Variant op_result;
            bool op_valid = false;
            Variant::evaluate(Variant::OP_NOT_EQUAL, defval, p_value, op_result, op_valid);

            if (op_valid && op_result.operator bool())
                values[p_name] = p_value;

            return true;
        }
    }

    return false;
}

bool PlaceHolderScriptInstance::get(const StringName &p_name, Variant &r_ret) {
    if (values.has(p_name)) {
        r_ret = values[p_name];
        return true;
    }

    if (constants.has(p_name)) {
        r_ret = constants[p_name];
        return true;
    }

    if (!script->_is_placeholder_fallback_enabled() && script->_has_property_default_value(p_name)) {
        r_ret = script->_get_property_default_value(p_name);
        return true;
    }

    return false;
}

void PlaceHolderScriptInstance::get_property_state(GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
    get_property_state_static(*this, p_add_func, p_userdata);
}

GDExtensionPropertyInfo *PlaceHolderScriptInstance::get_property_list(uint32_t *r_count) const {
    Vector<GDExtensionPropertyInfo> props;

    int size = properties.size();
    props.resize(size);

    GDExtensionPropertyInfo *props_ptr = props.ptrw();

    if (script->_is_placeholder_fallback_enabled()) {
        for (int i = 0; i < size; i++) {
            GDExtensionPropertyInfo dst;
            copy_prop(properties[i], dst);

            props_ptr[i] = dst;
        }
    } else {
        for (int i = 0; i < size; i++) {
            GDExtensionPropertyInfo pinfo;
            copy_prop(properties[i], pinfo);

            if (!values.has(properties[i].name))
                pinfo.usage |= PROPERTY_USAGE_SCRIPT_DEFAULT_VALUE;

            props_ptr[i] = pinfo; // this is actually wrong in godot source (?)
        }
    }

    *r_count = size;

    GDExtensionPropertyInfo *list = alloc_with_len<GDExtensionPropertyInfo>(size);
    memcpy(list, props.ptr(), sizeof(GDExtensionPropertyInfo) * size);

    return list;
}

void PlaceHolderScriptInstance::free_property_list(const GDExtensionPropertyInfo *p_list) const {
    free_property_list_static(p_list);
}

Variant::Type PlaceHolderScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
    if (values.has(p_name)) {
        if (r_is_valid != nullptr)
            *r_is_valid = true;

        return values[p_name].get_type();
    }

    if (constants.has(p_name)) {
        if (r_is_valid != nullptr)
            *r_is_valid = true;

        return constants[p_name].get_type();
    }

    if (r_is_valid != nullptr)
        *r_is_valid = false;

    return Variant::NIL;
}

GDExtensionMethodInfo *PlaceHolderScriptInstance::get_method_list(uint32_t *r_count) const {
    if (!script->_is_placeholder_fallback_enabled() && script.is_valid())
        return get_method_list_static(script, r_count);

    return alloc_with_len<GDExtensionMethodInfo>(0);
}

void PlaceHolderScriptInstance::free_method_list(const GDExtensionMethodInfo *p_list) const {
    free_method_list_static(p_list);
}

bool PlaceHolderScriptInstance::has_method(const StringName &p_name) const {
    if (script->_is_placeholder_fallback_enabled())
        return false;

    if (script.is_valid())
        return script->_has_method(p_name);

    return false;
}

ScriptLanguage *PlaceHolderScriptInstance::get_language() const {
    return LuauLanguage::get_singleton();
}

bool PlaceHolderScriptInstance::property_set_fallback(const StringName &p_name, const Variant &p_value) {
    if (script->_is_placeholder_fallback_enabled()) {
        HashMap<StringName, Variant>::Iterator E = values.find(p_name);

        if (E) {
            E->value = p_value;
        } else {
            values.insert(p_name, p_value);
        }

        bool found = false;
        for (const GDProperty &F : properties) {
            if (F.name == p_name) {
                found = true;
                break;
            }
        }

        if (!found) {
            GDProperty pinfo;

            pinfo.type = (GDExtensionVariantType)p_value.get_type();
            pinfo.name = p_name;
            pinfo.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_SCRIPT_VARIABLE;

            properties.push_back(pinfo);
        }
    }

    return false;
}

bool PlaceHolderScriptInstance::property_get_fallback(const StringName &p_name, Variant &r_ret) {
    if (script->_is_placeholder_fallback_enabled()) {
        HashMap<StringName, Variant>::ConstIterator E = values.find(p_name);

        if (E) {
            r_ret = E->value;
            return true;
        }

        E = constants.find(p_name);

        if (E) {
            r_ret = E->value;
            return true;
        }
    }

    r_ret = Variant();
    return false;
}

void PlaceHolderScriptInstance::update(const List<GDProperty> &p_properties, const HashMap<StringName, Variant> &p_values) {
    HashSet<StringName> new_values;

    for (const GDProperty &E : p_properties) {
        StringName n = E.name;
        new_values.insert(n);

        if (!values.has(n) || values[n].get_type() != (Variant::Type)E.type) {
            if (p_values.has(n))
                values[n] = p_values[n];
        }
    }

    properties = p_properties;

    List<StringName> to_remove;

    for (KeyValue<StringName, Variant> &E : values) {
        if (!new_values.has(E.key))
            to_remove.push_back(E.key);

        if (script->_has_property_default_value(E.key)) {
            Variant defval = script->_get_property_default_value(E.key);

            if (defval == E.value)
                to_remove.push_back(E.key);
        }
    }

    while (to_remove.size()) {
        values.erase(to_remove.front()->get());
        to_remove.pop_front();
    }

    if (owner != nullptr && script.is_valid() && script->placeholder_has(owner) && script->placeholder_get(owner) == this) {
        owner->notify_property_list_changed();
    }

    constants.clear();

    Dictionary new_constants = script->_get_constants();
    Array keys = new_constants.keys();

    for (int i = 0; i < keys.size(); i++) {
        const Variant &key = keys[i];
        constants[key] = new_constants[key];
    }
}

PlaceHolderScriptInstance::PlaceHolderScriptInstance(Ref<LuauScript> p_script, Object *p_owner) :
        script(p_script),
        owner(p_owner) {
    // Placeholder instance creation takes place in a const method.
    script->placeholders.insert(p_owner->get_instance_id(), this);
    script->update_exports_internal(nullptr, false, this);
}

PlaceHolderScriptInstance::~PlaceHolderScriptInstance() {
    if (script.is_valid()) {
        script->_placeholder_erased(this);
    }
}

//////////////
// LANGUAGE //
//////////////

LuauLanguage *LuauLanguage::singleton = nullptr;

LuauLanguage::LuauLanguage() {
    singleton = this;
}

LuauLanguage::~LuauLanguage() {
    finalize();
    singleton = nullptr;
}

void LuauLanguage::_init() {
    luau = memnew(GDLuau);
    cache = memnew(LuauCache);
}

void LuauLanguage::finalize() {
    if (finalized)
        return;

    if (luau != nullptr) {
        memdelete(luau);
        luau = nullptr;
    }

    if (cache != nullptr) {
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

Object *LuauLanguage::_create_script() const {
    return memnew(LuauScript);
}

Ref<Script> LuauLanguage::_make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
    Ref<LuauScript> script;
    script.instantiate();

    // TODO: actual template stuff

    return script;
}

Error LuauLanguage::_execute_file(const String &p_path) {
    // Unused by Godot; purpose unclear
    return OK;
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

String ResourceFormatLoaderLuauScript::_get_resource_type(const String &p_path) const {
    return p_path.get_extension().to_lower() == "lua" ? LuauLanguage::get_singleton()->_get_type() : "";
}

Variant ResourceFormatLoaderLuauScript::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int64_t p_cache_mode) const {
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

int64_t ResourceFormatSaverLuauScript::_save(const Ref<Resource> &p_resource, const String &p_path, int64_t p_flags) {
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
