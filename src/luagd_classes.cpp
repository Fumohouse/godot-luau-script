#include "luagd_classes.h"

#include <lua.h>
#include <lualib.h>
#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "luagd_stack.h"
#include "luau_script.h"
#include "luau_lib.h"
#include "luagd_utils.h"

using namespace godot;

int luaGD_class_ctor(lua_State *L)
{
    StringName class_name = lua_tostring(L, lua_upvalueindex(1));

    GDExtensionObjectPtr native_ptr = internal::gde_interface->classdb_construct_object(&class_name);
    GDObjectInstanceID id = internal::gde_interface->object_get_instance_id(native_ptr);

    Object *obj = ObjectDB::get_instance(id);
    LuaStackOp<Object *>::push(L, obj);

    // refcount is instantiated to 1.
    // we add a ref in the call above, so it's ok to decrement now to avoid object getting leaked
    RefCounted *rc = Object::cast_to<RefCounted>(obj);
    if (rc != nullptr)
        rc->unreference();

    return 1;
}

int luaGD_class_no_ctor(lua_State *L)
{
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));
    luaL_error(L, "class %s is not instantiable", class_name);
}

#define LUAGD_CLASS_METAMETHOD                                          \
    int class_idx = lua_tointeger(L, lua_upvalueindex(1));              \
    ClassRegistry *class_reg = luaGD_lightudataup<ClassRegistry>(L, 2); \
                                                                        \
    const ClassInfo *current_class = &(*class_reg)[class_idx];

#define INHERIT_OR_BREAK                                          \
    if (current_class->parent_idx >= 0)                           \
        current_class = &(*class_reg)[current_class->parent_idx]; \
    else                                                          \
        break;

static LuauScriptInstance *get_script_instance(lua_State *L)
{
    Object *self = LuaStackOp<Object *>::get(L, 1);
    Ref<LuauScript> script = self->get_script();

    if (script.is_valid() && script->_instance_has(self))
        return script->instance_get(self);

    return nullptr;
}

int luaGD_class_namecall(lua_State *L)
{
    LUAGD_CLASS_METAMETHOD

    if (const char *name = lua_namecallatom(L, nullptr))
    {
        if (LuauScriptInstance *inst = get_script_instance(L))
        {
            if (inst->has_method(name))
            {
                int arg_count = lua_gettop(L) - 1;

                Vector<Variant> args;
                args.resize(arg_count);

                Vector<const Variant *> pargs;
                pargs.resize(arg_count);

                for (int i = 2; i <= lua_gettop(L); i++)
                {
                    args.set(i - 2, LuaStackOp<Variant>::get(L, i));
                    pargs.set(i - 2, &args[i - 2]);
                }

                Variant ret;
                GDExtensionCallError err;
                inst->call(name, pargs.ptr(), args.size(), &ret, &err);

                switch (err.error)
                {
                case GDEXTENSION_CALL_OK:
                    LuaStackOp<Variant>::push(L, ret);
                    return 1;

                case GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT:
                    luaL_error(L, "invalid argument #%d to '%s' (%s expected, got %s)",
                               err.argument + 1,
                               name,
                               Variant::get_type_name((Variant::Type)err.expected).utf8().get_data(),
                               Variant::get_type_name(args[err.argument].get_type()).utf8().get_data());

                case GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS:
                    luaL_error(L, "missing argument #%d to '%s' (expected at least %d)",
                               args.size() + 2,
                               name,
                               err.argument);

                case GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS:
                    luaL_error(L, "too many arguments to '%s' (expected at most %d)",
                               name,
                               err.argument);

                default:
                    luaL_error(L, "unknown error occurred when calling '%s'", name);
                }
            }
            else
            {
                int nargs = lua_gettop(L);

                LuaStackOp<String>::push(L, name);
                bool is_valid = inst->table_get(L);

                if (is_valid)
                {
                    if (lua_isfunction(L, -1))
                    {
                        lua_insert(L, -nargs - 1);
                        lua_call(L, nargs, LUA_MULTRET);

                        return lua_gettop(L);
                    }
                    else
                    {
                        lua_pop(L, 1);
                    }
                }
            }
        }

        while (true)
        {
            if (current_class->methods.has(name))
                return current_class->methods[name](L);

            INHERIT_OR_BREAK
        }

        luaL_error(L, "%s is not a valid method of %s", name, current_class->class_name);
    }

    luaL_error(L, "no namecallatom");
}

int luaGD_class_global_index(lua_State *L)
{
    LUAGD_CLASS_METAMETHOD

    const char *key = luaL_checkstring(L, 2);

    while (true)
    {
        if (current_class->static_funcs.has(key))
        {
            lua_pushcfunction(L, current_class->static_funcs[key], key);
            return 1;
        }

        if (current_class->methods.has(key))
        {
            lua_pushcfunction(L, current_class->methods[key], key);
            return 1;
        }

        INHERIT_OR_BREAK
    }

    luaL_error(L, "%s is not a valid member of %s", key, current_class->class_name);
}

int luaGD_class_index(lua_State *L)
{
    LUAGD_CLASS_METAMETHOD

    const char *key = luaL_checkstring(L, 2);

    bool attempt_table_get = false;
    LuauScriptInstance *inst = get_script_instance(L);

    if (inst != nullptr)
    {
        if (const GDClassProperty *prop = inst->get_property(key))
        {
            if (prop->setter != StringName() && prop->getter == StringName())
                luaL_error(L, "property '%s' is write-only", key);

            Variant ret;
            LuauScriptInstance::PropertySetGetError err;
            bool is_valid = inst->get(key, ret, &err);

            if (is_valid)
            {
                LuaStackOp<Variant>::push(L, ret);
                return 1;
            }
            else if (err == LuauScriptInstance::PROP_GET_FAILED)
                luaL_error(L, "failed to get property '%s'; see previous errors for more information", key);
            else
                luaL_error(L, "failed to get property '%s': unknown error", key); // due to the checks above, this should hopefully never happen
        }
        else
        {
            // object properties should take precedence over arbitrary values
            attempt_table_get = true;
        }
    }

    while (true)
    {
        if (current_class->properties.has(key))
        {
            lua_remove(L, 2);

            return current_class->methods[current_class->properties[key].getter_name](L);
        }

        INHERIT_OR_BREAK
    }

    if (attempt_table_get)
    {
        // the key is already on the top of the stack
        bool is_valid = inst->table_get(L);

        if (is_valid)
            return 1;
    }

    luaL_error(L, "%s is not a valid member of %s", key, current_class->class_name);
}

int luaGD_class_newindex(lua_State *L)
{
    LUAGD_CLASS_METAMETHOD

    const char *key = luaL_checkstring(L, 2);

    bool attempt_table_set = false;
    LuauScriptInstance *inst = get_script_instance(L);

    if (inst != nullptr)
    {
        if (const GDClassProperty *prop = inst->get_property(key))
        {
            if (prop->getter != StringName() && prop->setter == StringName())
                luaL_error(L, "property '%s' is read-only", key);

            LuauScriptInstance::PropertySetGetError err;
            Variant val = LuaStackOp<Variant>::get(L, 3);
            bool is_valid = inst->set(key, val, &err);

            if (is_valid)
                return 0;
            else if (err == LuauScriptInstance::PROP_WRONG_TYPE)
                luaGD_valueerror(L, key,
                                 Variant::get_type_name(val.get_type()).utf8().get_data(),
                                 Variant::get_type_name((Variant::Type)prop->property.type).utf8().get_data());
            else if (err == LuauScriptInstance::PROP_SET_FAILED)
                luaL_error(L, "failed to set property '%s'; see previous errors for more information", key);
            else
                luaL_error(L, "failed to set property '%s': unknown error", key); // should never happen
        }
        else
        {
            // object properties should take precedence over arbitrary values
            attempt_table_set = true;
        }
    }

    while (true)
    {
        if (current_class->properties.has(key))
        {
            lua_remove(L, 2);

            const StringName &setter_name = current_class->properties[key].setter_name;

            if (setter_name == StringName())
                luaL_error(L, "property '%s' is read-only", key);

            return current_class->methods[setter_name](L);
        }

        INHERIT_OR_BREAK
    }

    if (attempt_table_set)
    {
        // key and value are already on the top of the stack
        bool is_valid = inst->table_set(L);
        if (is_valid)
            return 0;
    }

    luaL_error(L, "%s is not a valid member of %s", key, current_class->class_name);
}
