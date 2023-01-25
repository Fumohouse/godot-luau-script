#pragma once

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace godot {
class Object;
}

using namespace godot;

class Utils {
    static Object *class_db;

    static Object *get_class_db();

public:
    static bool class_exists(const StringName &class_name);
    static bool is_parent_class(const StringName &class_name, const StringName &inherits);

    static String to_pascal_case(const String &input);
    static String resource_type_hint(const String &type);
};
