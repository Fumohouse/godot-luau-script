#include "luagd_classes.h"

#include <lua.h>
#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

using namespace godot;

int luaGD_class_ctor(lua_State *L)
{
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));

    GDNativeObjectPtr native_ptr = internal::gdn_interface->classdb_construct_object(class_name);
    GDObjectInstanceID id = internal::gdn_interface->object_get_instance_id(native_ptr);

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

int luaGD_class_namecall(lua_State *L)
{
    LUAGD_CLASS_METAMETHOD

    if (const char *name = lua_namecallatom(L, nullptr))
    {
        while (true)
        {
            if (current_class->methods.count(name) > 0)
                return current_class->methods.at(name)(L);

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
        if (current_class->static_funcs.count(key) > 0)
        {
            lua_pushcfunction(L, current_class->static_funcs.at(key), key);
            return 1;
        }

        if (current_class->methods.count(key) > 0)
        {
            lua_pushcfunction(L, current_class->methods.at(key), key);
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

    while (true)
    {
        if (current_class->properties.count(key) > 0)
        {
            lua_remove(L, 2);

            return current_class->methods
                .at(current_class->properties.at(key).getter_name)(L);
        }

        INHERIT_OR_BREAK
    }

    luaL_error(L, "%s is not a valid member of %s", key, current_class->class_name);
}

int luaGD_class_newindex(lua_State *L)
{
    LUAGD_CLASS_METAMETHOD

    const char *key = luaL_checkstring(L, 2);

    while (true)
    {
        if (current_class->properties.count(key) > 0)
        {
            lua_remove(L, 2);

            return current_class->methods
                .at(current_class->properties.at(key).setter_name)(L);
        }

        INHERIT_OR_BREAK
    }

    luaL_error(L, "%s is not a valid member of %s", key, current_class->class_name);
}
