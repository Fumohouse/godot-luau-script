#pragma once

#include <cstdint>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/pair.hpp>

#include "gd_luau.h"

using namespace godot;

struct lua_State;

// Luau uses 1 "step unit" ~= 1KB
// amount (bytes) = step << 10
#define STEPSIZE_MIN 50
#define STEPSIZE_INC 25
#define STEPSIZE_MAX 10000

class ScheduledTask {
private:
    int thread_ref;

public:
    int get_thread_ref() { return thread_ref; }

    virtual bool is_complete() = 0;
    virtual int num_results() = 0;
    virtual int push_results(lua_State *L) = 0;
    virtual void update(double delta) = 0;

    ScheduledTask(lua_State *L);
    virtual ~ScheduledTask() {}
};

class WaitTask : public ScheduledTask {
private:
    // usecs
    uint64_t duration;
    uint64_t start_time;
    uint64_t remaining;

public:
    bool is_complete() override;
    int num_results() override { return 1; }
    int push_results(lua_State *L) override;
    void update(double delta) override;

    WaitTask(lua_State *L, double duration_secs);
};

typedef List<Pair<lua_State *, ScheduledTask *>> TaskList;

class TaskScheduler {
private:
    TaskList tasks;

    uint32_t gc_stepsize[GDLuau::VM_MAX] = { STEPSIZE_MIN };

    int32_t last_gc_size[GDLuau::VM_MAX] = { 0 };
    int32_t gc_rate[GDLuau::VM_MAX] = { 0 };

public:
    void frame(double delta);
    void register_task(lua_State *L, ScheduledTask *task);
};