#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>

#include "luau_script.h"

using namespace godot;

// Based on GDScriptCache
class LuauCache {
    HashMap<String, Ref<LuauScript>> cache;

    static LuauCache *singleton;

public:
    static LuauCache *get_singleton() { return singleton; }

    Ref<LuauScript> get_script(const String &p_path, Error &r_error, bool p_ignore_cache = false, LuauScript::LoadStage p_stage = LuauScript::LOAD_FULL);

    LuauCache();
    ~LuauCache();
};
