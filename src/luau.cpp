#include "luau.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/templates/pair.hpp>

#include "luagd.h"
#include "luagd_permissions.h"
#include "luau_lib.h"

using namespace godot;

Luau *Luau::singleton = nullptr;

void Luau::init_vm(VMType p_type)
{
    lua_State *L = luaGD_newstate(PERMISSION_BASE);
    luascript_openlibs(L);
    luascript_openclasslib(L, false);

    vms.insert(p_type, L);
}

Luau::Luau()
{
    UtilityFunctions::print_verbose("Luau runtime: initializing...");

    vms.reserve(VM_MAX);
    init_vm(VM_SCRIPT_LOAD);
    init_vm(VM_CORE);
    init_vm(VM_USER);

    singleton = this;
}

Luau::~Luau()
{
    UtilityFunctions::print_verbose("Luau runtime: uninitializing...");

    singleton = nullptr;

    for (const KeyValue<VMType, lua_State *> &kvp : vms)
        luaGD_close(kvp.value);

    vms.clear();
}

lua_State *Luau::get_vm(VMType p_type)
{
    if (!vms.has(p_type))
        return nullptr;

    return vms.get(p_type);
}
