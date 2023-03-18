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
    static GDExtensionObjectPtr cast_obj(GDExtensionObjectPtr ptr) {
        return internal::gde_interface->object_cast_to(ptr, internal::gde_interface->classdb_get_class_tag(&T::get_class_static()));
    }

    static bool class_exists(const StringName &class_name);
    static bool is_parent_class(const StringName &class_name, const StringName &inherits);
    static StringName get_parent_class(const StringName &class_name);

    static String to_pascal_case(const String &input);
    static String resource_type_hint(const String &type);
    static bool variant_types_compatible(Variant::Type t1, Variant::Type t2);
};
