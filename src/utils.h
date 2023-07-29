#pragma once

#include "gdextension_interface.h"
#include "wrapped_no_binding.h"
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace godot {
class Object;
}

using namespace godot;

class Utils {
    static nb::Object class_db;

    static Object *get_class_db();

public:
    template <typename T>
    static GDExtensionObjectPtr cast_obj(GDExtensionObjectPtr p_ptr) {
        return internal::gdextension_interface_object_cast_to(p_ptr, internal::gdextension_interface_classdb_get_class_tag(&T::get_class_static()));
    }

    static bool class_exists(const StringName &p_class_name);
    static bool class_has_method(const StringName &p_class_name, const StringName &p_method, bool p_no_inheritance = false);
    static bool is_parent_class(const StringName &p_class_name, const StringName &p_inherits);
    static StringName get_parent_class(const StringName &p_class_name);

    static String to_pascal_case(const String &p_input);
    static String resource_type_hint(const String &p_type);
    static bool variant_types_compatible(Variant::Type p_t1, Variant::Type p_t2);
};
