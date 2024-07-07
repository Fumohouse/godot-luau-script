#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

class Utils {
public:
	template <typename T>
	static GDExtensionObjectPtr cast_obj(GDExtensionObjectPtr p_ptr) {
		return internal::gdextension_interface_object_cast_to(p_ptr, internal::gdextension_interface_classdb_get_class_tag(&T::get_class_static()));
	}

	static String to_pascal_case(const String &p_input);
	static String resource_type_hint(const String &p_type);
	static bool variant_types_compatible(Variant::Type p_t1, Variant::Type p_t2);

	static Error load_file(const String &p_path, String &r_out);
};
