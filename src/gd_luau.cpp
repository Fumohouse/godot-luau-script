#include "gd_luau.h"

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luau_cache.h"
#include "luau_lib.h"
#include "luau_script.h"

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

void GDLuau::push_module(lua_State *L, LuauScript *p_script) {
    CharString path = p_script->get_path().utf8();

    if (p_script->is_module()) {
        if (p_script->load_module(L) != OK) {
            LuaStackOp<String>::push(L, "failed to load module at " + p_script->get_path());
            return;
        }

        lua_remove(L, -2); // thread
    } else {
        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::alloc(L);
        bool is_valid;

        LuauScript::get_class_definition(p_script, lua_mainthread(L), *def, is_valid);

        if (!is_valid)
            LuaStackOp<String>::push(L, "could not get class definition for script at " + p_script->get_path());
    }
}

int GDLuau::gdluau_require(lua_State *L) {
    String name = LuaStackOp<String>::check(L, 1);

    luaL_findtable(L, LUA_REGISTRYINDEX, GDLuau::MODULE_TABLE, 1);

    String full_path = "res://";

    {
        // Get full path.
        PackedStringArray segments = name.split(".");

        for (int i = 0; i < segments.size(); i++) {
            const String &segment = segments[i];
            full_path = full_path.path_join(segment);
        }

        if (FileAccess::file_exists(full_path + ".lua")) {
            full_path = full_path + ".lua";
        } else if (FileAccess::file_exists(full_path + ".mod.lua")) {
            full_path = full_path + ".mod.lua";
        } else {
            luaL_error(L, "could not find module: %s", name.utf8().get_data());
        }
    }

    CharString full_path_utf8 = full_path.utf8();

    // Checks.
    GDThreadData *udata = luaGD_getthreaddata(L);

    if (udata->script->get_path() == full_path)
        luaL_error(L, "cannot require current script");

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
    push_module(L, script.ptr());

    if (lua_isstring(L, -1))
        lua_error(L);

    lua_pushvalue(L, -1);
    lua_setfield(L, -3, full_path_utf8.get_data());

    return finishrequire(L);
}

void GDLuau::init_vm(VMType p_type) {
    lua_State *L = luaGD_newstate(PERMISSION_BASE);
    luascript_openlibs(L);

    // require
    lua_pushcfunction(L, gdluau_require, "require");
    lua_setglobal(L, "require");

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
