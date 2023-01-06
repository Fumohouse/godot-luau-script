#include "luau_cache.h"

#include <godot_cpp/classes/global_constants.hpp>

#include "luau_script.h"

using namespace godot;

LuauCache *LuauCache::singleton = nullptr;

Ref<LuauScript> LuauCache::get_script(const String &p_path, Error &r_error, bool p_ignore_cache) {
    Ref<LuauScript> script;

    if (cache.has(p_path)) {
        script = cache[p_path];

        if (!p_ignore_cache)
            return script;
    }

    bool needs_init = script.is_null();

    if (needs_init) {
        script.instantiate();
        script->set_path(p_path);
    }

    if (p_ignore_cache || needs_init) {
        r_error = script->load_source_code(p_path);

        if (r_error != OK)
            return script;
    }

    script->_reload(true);
    cache[p_path] = script;

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