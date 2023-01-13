#include "gd_luau.h"

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd.h"
#include "luagd_stack.h"
#include "luau_lib.h"
#include "luau_script.h"

using namespace godot;

GDLuau *GDLuau::singleton = nullptr;

static int gdluau_global_index(lua_State *L) {
    lua_pushvalue(L, 2); // key
    lua_rawget(L, 1); // pop key push value

    if (lua_isnil(L, -1)) {
        StringName key = luaL_optstring(L, 2, "");

        if (!key.is_empty()) {
            HashMap<StringName, Variant>::ConstIterator E = LuauLanguage::get_singleton()->get_global_constants().find(key);

            if (E)
                LuaStackOp<Variant>::push(L, E->value);
        }
    }

    return 1;
}

void GDLuau::init_vm(VMType p_type) {
    lua_State *L = luaGD_newstate(PERMISSION_BASE);
    luascript_openlibs(L);

    // Global metatable
    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, gdluau_global_index, "_G.__index");
    lua_setfield(L, -2, "__index");

    lua_setreadonly(L, -1, true);

    lua_setmetatable(L, LUA_GLOBALSINDEX);

    // Seal main global state
    luaL_sandbox(L);

    vms[p_type] = L;
}

GDLuau::GDLuau() {
    UtilityFunctions::print_verbose("Luau runtime: initializing...");

    init_vm(VM_SCRIPT_LOAD);
    init_vm(VM_CORE);
    init_vm(VM_USER);

    if (singleton == nullptr)
        singleton = this;
}

GDLuau::~GDLuau() {
    UtilityFunctions::print_verbose("Luau runtime: uninitializing...");

    if (singleton == this)
        singleton = nullptr;

    for (lua_State *&L : vms) {
        luaGD_close(L);
        L = nullptr;
    }
}

lua_State *GDLuau::get_vm(VMType p_type) {
    return vms[p_type];
}
