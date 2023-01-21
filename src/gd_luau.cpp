#include "gd_luau.h"

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "godot_cpp/classes/resource_loader.hpp"
#include "luagd.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luau_cache.h"
#include "luau_lib.h"
#include "luau_script.h"
#include "task_scheduler.h"

using namespace godot;

GDLuau *GDLuau::singleton = nullptr;
const char *GDLuau::MODULE_TABLE = "_MODULES";

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

// Based on Luau Repl implementation.
static int finishrequire(lua_State *L) {
    if (lua_isstring(L, -1))
        lua_error(L);

    return 1;
}

int GDLuau::gdluau_require(lua_State *L) {
    String path = LuaStackOp<String>::check(L, 1);
    GDThreadData *udata = luaGD_getthreaddata(L);

    luaL_findtable(L, LUA_REGISTRYINDEX, GDLuau::MODULE_TABLE, 1);

    // Get full path.
    String full_path = String("res://").path_join(path);

    if (FileAccess::file_exists(full_path + ".lua")) {
        full_path = full_path + ".lua";
    } else {
        luaL_error(L, "could not find module: %s", path.utf8().get_data());
    }

    CharString full_path_utf8 = full_path.utf8();

    // Checks.
    if (udata->script->get_path() == full_path)
        luaL_error(L, "cannot require current script");

    if (udata->script->has_dependent(full_path)) {
        luaL_error(L, "cyclic dependency detected in %s. halting require of %s.",
                udata->script->get_path().utf8().get_data(),
                full_path_utf8.get_data());
    }

    // Load and write dependency.
    Error err;
    Ref<LuauScript> script = LuauCache::get_singleton()->get_script(full_path, err, false, udata->script->get_path());

    // Return from cache.
    lua_getfield(L, -1, full_path_utf8.get_data());
    if (!lua_isnil(L, -1)) {
        return finishrequire(L);
    }

    lua_pop(L, 1);

    // Load module and return.
    if (script->is_module()) {
        if (script->load_module(L) != OK) {
            LuaStackOp<String>::push(L, "failed to load module at " + script->get_path());
        } else {
            lua_remove(L, -2); // thread
        }
    } else {
        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::alloc(L);
        bool is_valid;

        LuauScript::get_class_definition(script, lua_mainthread(L), *def, is_valid);

        if (!is_valid) {
            lua_pop(L, 1); // def
            LuaStackOp<String>::push(L, "could not get class definition for script at " + script->get_path());
        }

        def->is_readonly = true;
    }

    lua_pushvalue(L, -1);
    lua_setfield(L, -3, full_path_utf8.get_data());

    return finishrequire(L);
}

static int gdluau_load(lua_State *L) {
    String path = LuaStackOp<String>::check(L, 1);
    GDThreadData *udata = luaGD_getthreaddata(L);

    if (!path.begins_with("res://") && !path.begins_with("user://")) {
        path = udata->script->get_path().get_base_dir().path_join(path);
    }

    Ref<Resource> res = ResourceLoader::get_singleton()->load(path);
    LuaStackOp<Object *>::push(L, res.ptr());
    return 1;
}

static int gdluau_wait(lua_State *L) {
    double duration = LuaStackOp<double>::check(L, 1);

    WaitTask *task = memnew(WaitTask(L, duration));
    LuauLanguage::get_singleton()->get_task_scheduler().register_task(L, task);

    return lua_yield(L, 0);
}

const luaL_Reg GDLuau::global_funcs[] = {
    { "require", gdluau_require },
    { "wait", gdluau_wait },
    { "load", gdluau_load },
    { nullptr, nullptr }
};

void GDLuau::init_vm(VMType p_type) {
    lua_State *L = luaGD_newstate(p_type, PERMISSION_BASE);
    luascript_openlibs(L);

    luaL_register(L, "_G", global_funcs);

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
