#include "gd_luau.h"

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/variant/utility_functions.hpp>

#include "luagd.h"
#include "luagd_permissions.h"
#include "luau_lib.h"

using namespace godot;

GDLuau *GDLuau::singleton = nullptr;

void GDLuau::init_vm(VMType p_type) {
    lua_State *L = luaGD_newstate(p_type, PERMISSION_BASE);
    luascript_openlibs(L);

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

void GDLuau::gc_step(const uint32_t *p_step, double delta) {
    for (int i = 0; i < VM_MAX; i++) {
        lua_State *L = get_vm(VMType(i));
        lua_gc(L, LUA_GCSTEP, p_step[i] * delta);
    }
}

void GDLuau::gc_size(int32_t *r_buffer) {
    for (int i = 0; i < VM_MAX; i++) {
        lua_State *L = get_vm(VMType(i));
        r_buffer[i] = lua_gc(L, LUA_GCCOUNT, 0);
    }
}
