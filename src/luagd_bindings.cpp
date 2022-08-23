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
    lua_newtable(L); // global table
    lua_createtable(L, 0, 3); // global metatable - assume 3 fields: __fortype, __call, __index

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

static MethodMap *luaGD_getmethodmap(lua_State *L, int index)
{
    return reinterpret_cast<MethodMap *>(
        lua_tolightuserdata(L, lua_upvalueindex(index))
    );
}

int luaGD_builtin_namecall(lua_State *L)
{
    const char *class_name =
        lua_tostring(L, lua_upvalueindex(1));

    MethodMap *methods = luaGD_getmethodmap(L, 2);

    if (const char *name = lua_namecallatom(L, nullptr))
    {
        if (methods->count(name) > 0)
            return (methods->at(name))(L);

        luaL_error(L, "%s is not a valid method of %s", name, class_name);
    }

    luaL_error(L, "no namecallatom");
}

int luaGD_builtin_global_index(lua_State *L)
{
    const char *class_name =
        lua_tostring(L, lua_upvalueindex(1));

    const char *key = luaL_checkstring(L, 2);

    // Static functions
    MethodMap *statics = luaGD_getmethodmap(L, 2);
    if (statics && statics->count(key) > 0)
    {
        lua_pushcfunction(L, statics->at(key), key);
        return 1;
    }

    // Instance methods
    MethodMap *methods = luaGD_getmethodmap(L, 3);
    if (methods && methods->count(key) > 0)
    {
        lua_pushcfunction(L, methods->at(key), key);
        return 1;
    }

    // Constants
    MethodMap *consts = luaGD_getmethodmap(L, 4);
    if (consts && consts->count(key) > 0)
        return consts->at(key)(L);

    luaL_error(L, "%s is not a valid member of %s", key, class_name);
}

int luaGD_class_ctor(lua_State *L)
{
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));

    GDNativeObjectPtr native_ptr = internal::gdn_interface->classdb_construct_object(class_name);
    GDObjectInstanceID id = internal::gdn_interface->object_get_instance_id(native_ptr);

    Object* obj = ObjectDB::get_instance(id);
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
