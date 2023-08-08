#pragma once

#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "luagd_binder.h"
#include "services/luau_interface.h"

class SandboxService : public Service {
    static SandboxService *singleton;

    const LuaGDClass &get_lua_class() const override;

    HashSet<String> core_scripts;
    Vector<String> ignore_paths;

    void discover_core_scripts_internal(const String &p_path = "res://");

public:
    static SandboxService *get_singleton() { return singleton; }

    void lua_push(lua_State *L) override;

    bool is_core_script(const String &p_path) const;
    void discover_core_scripts();
    void core_script_ignore(const String &p_path);
    void core_script_add(const String &p_path);
    Array core_script_list() const;

    SandboxService();
    ~SandboxService();
};

STACK_OP_SVC_DEF(SandboxService)
