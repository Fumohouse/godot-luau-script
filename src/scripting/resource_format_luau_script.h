#pragma once

#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>

using namespace godot;

class ResourceFormatLoaderLuauScript : public ResourceFormatLoader {
	GDCLASS(ResourceFormatLoaderLuauScript, ResourceFormatLoader);

protected:
	static void _bind_methods() {}

public:
	PackedStringArray _get_recognized_extensions() const override;
	bool _handles_type(const StringName &p_type) const override;
	static String get_resource_type(const String &p_path);
	String _get_resource_type(const String &p_path) const override;
	Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const override;
};

class ResourceFormatSaverLuauScript : public ResourceFormatSaver {
	GDCLASS(ResourceFormatSaverLuauScript, ResourceFormatSaver);

protected:
	static void _bind_methods() {}

public:
	PackedStringArray _get_recognized_extensions(const Ref<Resource> &p_resource) const override;
	bool _recognize(const Ref<Resource> &p_resource) const override;
	Error _save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) override;
};
