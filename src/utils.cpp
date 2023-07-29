#include "utils.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "wrapped_no_binding.h"

using namespace godot;

// TODO: the real ClassDB is not available in godot-cpp yet. this is what we get.
nb::Object Utils::class_db = nullptr;

Object *Utils::get_class_db() {
    if (!class_db._owner) {
        StringName classdb_name = "ClassDB";
        class_db._owner = internal::gdextension_interface_global_get_singleton(&classdb_name);
    }

    return &class_db;
}

bool Utils::class_exists(const StringName &p_class_name) {
    return get_class_db()->call("class_exists", p_class_name);
}

bool Utils::class_has_method(const StringName &p_class_name, const StringName &p_method, bool p_no_inheritance) {
    return get_class_db()->call("class_has_method", p_class_name, p_method, p_no_inheritance);
}

bool Utils::is_parent_class(const StringName &p_class_name, const StringName &p_inherits) {
    return get_class_db()->call("is_parent_class", p_class_name, p_inherits);
}

StringName Utils::get_parent_class(const StringName &p_class_name) {
    return get_class_db()->call("get_parent_class", p_class_name);
}

String Utils::to_pascal_case(const String &p_input) {
    String out = p_input.to_pascal_case();

    // to_pascal_case strips leading/trailing underscores. leading is most common and this handles that
    for (int i = 0; i < p_input.length() && p_input[i] == '_'; i++)
        out = "_" + out;

    return out;
}

String Utils::resource_type_hint(const String &p_type) {
    // see core/object/object.h
    Array hint_values;
    hint_values.resize(3);
    hint_values[0] = Variant::OBJECT;
    hint_values[1] = PROPERTY_HINT_RESOURCE_TYPE;
    hint_values[2] = p_type;

    return String("{0}/{1}:{2}").format(hint_values);
}

static bool variant_types_compatible_internal(Variant::Type p_t1, Variant::Type p_t2) {
    return (p_t1 == Variant::FLOAT && p_t2 == Variant::INT) ||
            (p_t1 == Variant::NIL && p_t2 == Variant::OBJECT) ||
            (p_t1 == Variant::STRING && p_t2 == Variant::NODE_PATH) ||
            (p_t1 == Variant::STRING && p_t2 == Variant::STRING_NAME);
}

bool Utils::variant_types_compatible(Variant::Type p_t1, Variant::Type p_t2) {
    return p_t1 == p_t2 || variant_types_compatible_internal(p_t1, p_t2) || variant_types_compatible_internal(p_t2, p_t1);
}
