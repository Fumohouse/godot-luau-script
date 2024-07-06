#include "scripting/resource_format_luau_script.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "scripting/luau_cache.h"
#include "scripting/luau_script.h"

// Loader

PackedStringArray ResourceFormatLoaderLuauScript::_get_recognized_extensions() const {
	PackedStringArray extensions;
	extensions.push_back("lua");

	return extensions;
}

bool ResourceFormatLoaderLuauScript::_handles_type(const StringName &p_type) const {
	return p_type == StringName("Script") || p_type == LuauLanguage::get_singleton()->_get_type();
}

String ResourceFormatLoaderLuauScript::get_resource_type(const String &p_path) {
	// Special case
	if (p_path == INIT_LUA_PATH)
		return "";

	return p_path.get_extension().to_lower() == "lua" ? LuauLanguage::get_singleton()->_get_type() : "";
}

String ResourceFormatLoaderLuauScript::_get_resource_type(const String &p_path) const {
	return get_resource_type(p_path);
}

Variant ResourceFormatLoaderLuauScript::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
	Error err = OK;
	Ref<LuauScript> script = LuauCache::get_singleton()->get_script(p_path, err, p_cache_mode == CACHE_MODE_IGNORE);

	return script;
}

// Saver

PackedStringArray ResourceFormatSaverLuauScript::_get_recognized_extensions(const Ref<Resource> &p_resource) const {
	PackedStringArray extensions;

	Ref<LuauScript> ref = p_resource;
	if (ref.is_valid())
		extensions.push_back("lua");

	return extensions;
}

bool ResourceFormatSaverLuauScript::_recognize(const Ref<Resource> &p_resource) const {
	Ref<LuauScript> ref = p_resource;
	return ref.is_valid();
}

Error ResourceFormatSaverLuauScript::_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Ref<LuauScript> script = p_resource;
	ERR_FAIL_COND_V(script.is_null(), ERR_INVALID_PARAMETER);

	String source = script->get_source_code();

	{
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::ModeFlags::WRITE);
		ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), "Failed to save file at " + p_path);

		file->store_string(source);

		if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF)
			return ERR_CANT_CREATE;
	}

	// TODO: Godot's default language implementations have a check here. It isn't possible in extensions (yet).
	// if (ScriptServer::is_reload_scripts_on_save_enabled())
	LuauLanguage::get_singleton()->_reload_tool_script(p_resource, false);

	return OK;
}
