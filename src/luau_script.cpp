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
}

Error LuauScript::load_source_code(const String &p_path) {
    Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
    ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), "Failed to read file: '" + p_path + "'.");

    PackedByteArray bytes = file->get_buffer(file->get_length());

    String src;
    src.parse_utf8(reinterpret_cast<const char *>(bytes.ptr()));

    set_source_code(src);

    return OK;
}

#define LUAU_LOAD_ERR(line, msg) _err_print_error("LuauScript::_reload", get_path().is_empty() ? "built-in" : get_path().utf8().get_data(), line, msg);
#define LUAU_LOAD_YIELD_MSG "Luau Error: Script yielded when loading definition."
#define LUAU_LOAD_NO_DEF_MSG "Luau Error: Script did not return a valid class definition."

#define LUAU_LOAD_RESUME                             \
    int status = lua_resume(T, nullptr, 0);          \
                                                     \
    if (status == LUA_YIELD) {                       \
        LUAU_LOAD_ERR(1, LUAU_LOAD_YIELD_MSG);       \
        return ERR_COMPILATION_FAILED;               \
    } else if (status != LUA_OK) {                   \
        String err = LuaStackOp<String>::get(T, -1); \
        LUAU_LOAD_ERR(1, "Luau Error: " + err);      \
                                                     \
        return ERR_COMPILATION_FAILED;               \
    }

static Error try_load(lua_State *L, const char *src) {
    Luau::CompileOptions opts;
    std::string bytecode = Luau::compile(src, opts);

    return luau_load(L, "=load", bytecode.data(), bytecode.size(), 0) == 0 ? OK : ERR_COMPILATION_FAILED;
}

Error LuauScript::_reload(bool p_keep_state) {
    bool has_instances;

    {
        MutexLock lock(LuauLanguage::singleton->lock);
        has_instances = instances.size();
    }

    ERR_FAIL_COND_V(!p_keep_state && has_instances, ERR_ALREADY_IN_USE);

    valid = false;

    // TODO: error line numbers?

    lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_SCRIPT_LOAD);
    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    if (try_load(T, source.utf8().get_data()) == OK) {
        LUAU_LOAD_RESUME

        if (lua_isnil(T, 1) || lua_type(T, 1) != LUA_TTABLE) {
            lua_pop(L, 1); // thread
            LUAU_LOAD_ERR(1, LUAU_LOAD_NO_DEF_MSG);

            return ERR_COMPILATION_FAILED;
        }

        definition = luascript_read_class(T, 1);
        valid = true;

        lua_pop(L, 1); // thread

        for (const KeyValue<GDLuau::VMType, int> &pair : def_table_refs) {
            if (load_methods(pair.key, true) == OK)
                continue;

            valid = false;
            return ERR_COMPILATION_FAILED;
        }

        return OK;
    }

    String err = LuaStackOp<String>::get(T, -1);
    LUAU_LOAD_ERR(1, "Luau Error: " + err);

    lua_pop(L, 1); // thread

    return ERR_COMPILATION_FAILED;
}

Error LuauScript::load_methods(GDLuau::VMType p_vm_type, bool force) {
    if (!force && def_table_refs.has(p_vm_type))
        return ERR_SKIP;

    lua_State *L = GDLuau::get_singleton()->get_vm(p_vm_type);
    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    if (try_load(T, source.utf8().get_data()) == OK) {
        LUAU_LOAD_RESUME

        if (lua_gettop(T) < 1) {
            LUAU_LOAD_ERR(1, LUAU_LOAD_NO_DEF_MSG);
            return ERR_COMPILATION_FAILED;
        }

        if (def_table_refs.has(p_vm_type))
            lua_unref(L, def_table_refs[p_vm_type]);

        def_table_refs[p_vm_type] = lua_ref(T, -1);
        lua_pop(L, 1); // thread

        return OK;
    }

    String err = LuaStackOp<String>::get(T, -1);
    LUAU_LOAD_ERR(1, "Luau Error: " + err);

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
    return valid;
}

bool LuauScript::_is_tool() const {
    return definition.is_tool;
}

StringName LuauScript::_get_instance_base_type() const {
    StringName extends = StringName(definition.extends);

    if (extends != StringName()) {
        // TODO: the real ClassDB is not available in godot-cpp yet. this is what we get.
        Object *class_db = Engine::get_singleton()->get_singleton("ClassDB");

        if (class_db->call("class_exists", extends).operator bool())
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

    if (E)
        return E->value.default_value != Variant();

    return false;
}

Variant LuauScript::_get_property_default_value(const StringName &p_property) const {
    // safe as _has_property_default_value is always checked before this
    return definition.properties.get(p_property).default_value;
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

static int instance_table_set(lua_State *L) {
    lua_settable(L, 1);
    return 0;
}

static int instance_table_get(lua_State *L) {
    lua_gettable(L, 1);
    return 1;
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
                lua_pushcfunction(ET, instance_table_set, "instance_table_set");

                lua_getref(ET, table_ref);
                LuaStackOp<String>::push(ET, p_name);
                LuaStackOp<Variant>::push(ET, p_value);

                status = lua_pcall(ET, 3, 0, 0);
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
                lua_pushcfunction(ET, instance_table_get, "instance_table_get");

                lua_getref(ET, table_ref);
                LuaStackOp<String>::push(ET, String(p_name));

                status = lua_pcall(ET, 2, 1, 0);
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

void LuauScriptInstance::get_property_state(GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
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
    // don't ask.
    int size = get_len_from_ptr(p_list);

    for (int i = 0; i < size; i++)
        free_prop(p_list[i]);

    free_with_len((GDExtensionPropertyInfo *)p_list);
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

GDExtensionMethodInfo *LuauScriptInstance::get_method_list(uint32_t *r_count) const {
    Vector<GDExtensionMethodInfo> methods;
    HashSet<StringName> defined;

    const LuauScript *s = script.ptr();

    while (s != nullptr) {
        for (const KeyValue<StringName, GDMethod> pair : s->definition.methods) {
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

void LuauScriptInstance::free_method_list(const GDExtensionMethodInfo *p_list) const {
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
            else
                ERR_PRINT("Lua Error: " + LuaStackOp<String>::get(ET, -1));

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
}

void LuauLanguage::finalize() {
    if (finalized)
        return;

    if (luau != nullptr) {
        memdelete(luau);
        luau = nullptr;
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
    Ref<LuauScript> script = memnew(LuauScript);
    Error err = script->load_source_code(p_path);

    ERR_FAIL_COND_V_MSG(err != OK, Ref<LuauScript>(), "Cannot load Luau script file '" + p_path + "'.");

    script->set_path(p_original_path);
    script->reload();

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
