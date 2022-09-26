#include "gd_luau.h"

#include <lua.h>
#include <lualib.h>

#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/templates/pair.hpp>

#include "luagd.h"
#include "luagd_permissions.h"
#include "luau_lib.h"

using namespace godot;

GDLuau *GDLuau::singleton = nullptr;

void GDLuau::init_vm(VMType p_type)
{
    lua_State *L = luaGD_newstate(PERMISSION_BASE);
    luascript_openlibs(L);
    luascript_openclasslib(L, false);
    luaL_sandbox(L);

    vms.insert(p_type, L);
}

GDLuau::GDLuau()
{
    UtilityFunctions::print_verbose("Luau runtime: initializing...");

    vms.reserve(VM_MAX);
    init_vm(VM_SCRIPT_LOAD);
    init_vm(VM_CORE);
    init_vm(VM_USER);

    if (singleton == nullptr)
        singleton = this;
}

GDLuau::~GDLuau()
{
    UtilityFunctions::print_verbose("Luau runtime: uninitializing...");

    if (singleton == this)
        singleton = nullptr;

    for (const KeyValue<VMType, lua_State *> &kvp : vms)
        luaGD_close(kvp.value);

    vms.clear();
}

lua_State *GDLuau::get_vm(VMType p_type)
{
    if (!vms.has(p_type))
        return nullptr;

    return vms.get(p_type);
}
