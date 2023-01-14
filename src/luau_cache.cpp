#include "luau_cache.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include "luau_script.h"

using namespace godot;

LuauCache *LuauCache::singleton = nullptr;

Ref<LuauScript> LuauCache::get_script(const String &p_path, Error &r_error, bool p_ignore_cache, const String &p_dependent) {
    Ref<LuauScript> script;

    if (cache.has(p_path)) {
        script = cache[p_path];

        if (script->is_reloading()) {
            r_error = ERR_CYCLIC_LINK;
            ERR_FAIL_V_MSG(script, "cyclic dependency detected in " + p_path + ". script requested from cache while it was loading.");
        }

        if (!p_ignore_cache) {
            if (!p_dependent.is_empty()) {
                script->dependents.insert(p_dependent);
            }

            return script;
        }
    }

    bool needs_init = script.is_null();

    if (needs_init) {
        script.instantiate();

        // This is done for tests, as Godot is holding onto references to scripts for some reason.
        // Shouldn't really have side effects, hopefully.
        script->take_over_path(p_path);
    }

    if (p_ignore_cache || needs_init) {
        r_error = script->load_source_code(p_path);

        if (r_error != OK)
            return script;
    }

    if (!p_dependent.is_empty()) {
        script->dependents.insert(p_dependent);
    }

    // Set cache before _reload to prevent infinite recursion inside.
    cache[p_path] = script;

    if (p_path.ends_with(".mod.lua")) {
        script->_is_module = true;
    } else {
        script->_reload(true);
    }

    return script;
}

LuauCache::LuauCache() {
    if (singleton == nullptr)
        singleton = this;
}

LuauCache::~LuauCache() {
    if (singleton == this)
        singleton = nullptr;
}