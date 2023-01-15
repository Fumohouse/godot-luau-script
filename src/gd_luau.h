#pragma once

struct lua_State;
class LuauScript;

class GDLuau {
public:
    enum VMType {
        // Runs code for getting basic information for LuauScript.
        VM_SCRIPT_LOAD = 0,

        // Runs the core game code.
        VM_CORE,

        // Runs any potentially unsafe user code.
        VM_USER,

        VM_MAX
    };

private:
    static GDLuau *singleton;

    lua_State *vms[VM_MAX];
    void init_vm(VMType p_type);

    static int gdluau_require(lua_State *L);

public:
    static const char *MODULE_TABLE;

    static GDLuau *get_singleton() { return singleton; }

    lua_State *get_vm(VMType p_type);

    GDLuau();
    ~GDLuau();
};
