#include "luau_cache.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>

#include "luau_script.h"

using namespace godot;

LuauCache *LuauCache::singleton = nullptr;

Ref<LuauScript> LuauCache::get_script(const String &p_path, Error &r_error, bool p_ignore_cache) {
    String path = p_path.simplify_path();

    Ref<LuauScript> script;
    r_error = OK;

    if (cache.has(path)) {
        script = cache[path];

        if (!p_ignore_cache) {
            return script;
        }
    }

    bool needs_init = script.is_null();

    if (needs_init) {
        script.instantiate();

        // This is done for tests, as Godot is holding onto references to scripts for some reason.
        // Shouldn't really have side effects, hopefully.
        script->take_over_path(path);
    }

    if (p_ignore_cache || needs_init) {
        r_error = script->load_source_code(path);

        if (r_error != OK)
            return script;
    }

    // Set cache before _reload to prevent infinite recursion inside.
    cache[path] = script;

    if (path.ends_with(".mod.lua")) {
        script->_is_module = true;
    } else {
        script->_reload(true);
    }

    return script;
}

LuauCache::LuauCache() {
    if (!singleton)
        singleton = this;
}

LuauCache::~LuauCache() {
    if (singleton == this)
        singleton = nullptr;
}
