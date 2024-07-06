#include "scripting/luau_cache.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>

#include "scripting/luau_script.h"

using namespace godot;

LuauCache *LuauCache::singleton = nullptr;

Ref<LuauScript> LuauCache::get_script(const String &p_path, Error &r_error, bool p_ignore_cache, LuauScript::LoadStage p_stage) {
	String path = p_path.simplify_path();

	Ref<LuauScript> script;
	r_error = OK;

	HashMap<String, Ref<LuauScript>>::ConstIterator E = cache.find(path);
	if (E) {
		script = E->value;

		if (!p_ignore_cache) {
			script->load(p_stage);
			return script;
		}
	}

	bool needs_init = script.is_null();

	if (needs_init) {
		script.instantiate();

		// This is done for tests, as Godot is holding onto references to scripts for some reason.
		// Shouldn't really have side effects, hopefully.
		script->take_over_path(path);

		// Set cache before `load` to prevent infinite recursion inside.
		cache[path] = script;
	}

	if (p_ignore_cache || needs_init) {
		r_error = script->load_source_code(path);

		if (r_error != OK)
			return script;
	}

	if (path.ends_with(".mod.lua")) {
		script->_is_module = true;
	}

	script->load(p_stage, true);

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
