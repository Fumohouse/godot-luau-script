#include "luagd_bindings.h"

#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <lua.h>
#include <lualib.h>

using namespace godot;

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name)
{
    luaL_newmetatable(L, mt_name); // instance metatable
    lua_newtable(L);               // global table
    lua_createtable(L, 0, 3);      // global metatable - assume 3 fields: __fortype, __call, __index

    lua_pushstring(L, mt_name);
    lua_setfield(L, -2, "__fortype");

    // set global table's metatable
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -3);

    lua_pushvalue(L, -2);
    lua_setglobal(L, global_name);
}

void luaGD_poplib(lua_State *L, bool is_obj)
{
    if (is_obj)
    {
        lua_pushboolean(L, true);
        lua_setfield(L, -4, "__isgdobj");
    }

    // global will be set readonly on sandbox
    lua_setreadonly(L, -3, true); // instance metatable
    lua_setreadonly(L, -1, true); // global metatable

    lua_pop(L, 3);
}

template <typename T>
static T *luaGD_lightudataup(lua_State *L, int index)
{
    return reinterpret_cast<T *>(
        lua_tolightuserdata(L, lua_upvalueindex(index)));
}

int luaGD_builtin_namecall(lua_State *L)
{
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));

    MethodMap *methods = luaGD_lightudataup<MethodMap>(L, 2);

    if (const char *name = lua_namecallatom(L, nullptr))
    {
        if (methods->count(name) > 0)
            return (methods->at(name))(L);

        luaL_error(L, "%s is not a valid method of %s", name, class_name);
    }

    luaL_error(L, "no namecallatom");
}

int luaGD_builtin_newindex(lua_State *L)
{
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));
    luaL_error(L, "%s is readonly", class_name);
}

int luaGD_builtin_global_index(lua_State *L)
{
    const char *class_name =
        lua_tostring(L, lua_upvalueindex(1));

    const char *key = luaL_checkstring(L, 2);

    // Static functions
    MethodMap *statics = luaGD_lightudataup<MethodMap>(L, 2);
    if (statics && statics->count(key) > 0)
    {
        lua_pushcfunction(L, statics->at(key), key);
        return 1;
    }

    // Instance methods
    MethodMap *methods = luaGD_lightudataup<MethodMap>(L, 3);
    if (methods && methods->count(key) > 0)
    {
        lua_pushcfunction(L, methods->at(key), key);
        return 1;
    }

    luaL_error(L, "%s is not a valid member of %s", key, class_name);
}

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
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));      \
    int class_idx = lua_tointeger(L, lua_upvalueindex(2));              \
    ClassRegistry *class_reg = luaGD_lightudataup<ClassRegistry>(L, 3); \
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

        luaL_error(L, "%s is not a valid method of %s", name, class_name);
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

    luaL_error(L, "%s is not a valid member of %s", key, class_name);
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

    luaL_error(L, "%s is not a valid member of %s", key, class_name);
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

    luaL_error(L, "%s is not a valid member of %s", key, class_name);
}
