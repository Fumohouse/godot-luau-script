#pragma once

#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "services/luau_interface.h"

struct lua_State;

class DebugService : public Service {
    static DebugService *singleton;

    const LuaGDClass &get_lua_class() const override;

    lua_State *T = nullptr;
    int thread_ref = 0;

public:
    static DebugService *get_singleton() { return singleton; }

    void lua_push(lua_State *L) override;

    PackedFloat64Array gc_count() const;
    PackedInt32Array gc_step_size() const;
    String exec(const String &p_src);

    DebugService();
    ~DebugService();
};

STACK_OP_SVC_DEF(DebugService)
