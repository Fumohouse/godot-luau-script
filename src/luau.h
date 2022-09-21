#pragma once

#include <godot_cpp/templates/hash_map.hpp>

using namespace godot;

struct lua_State;

class Luau
{
    enum VMType
    {
        // Runs code for getting basic information for LuauScript.
        VM_SCRIPT_LOAD = 0,

        // Runs the core game code.
        VM_CORE = 1,

        // Runs any potentially unsafe user code.
        VM_USER = 2,

        VM_MAX = 3
    };

private:
    static Luau *singleton;

    HashMap<VMType, lua_State *> vms;
    void init_vm(VMType p_type);

public:
    static Luau *get_singleton() { return singleton; }

    lua_State *get_vm(VMType p_type);

    Luau();
    ~Luau();
};
