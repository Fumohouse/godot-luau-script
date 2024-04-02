#include "utils.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "error_strings.h"

using namespace godot;

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

Error Utils::load_file(const String &p_path, String &r_out) {
    Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
    ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), FILE_READ_FAILED_ERR(p_path));

    uint64_t len = file->get_length();
    PackedByteArray bytes = file->get_buffer(len);
    bytes.resize(len + 1);
    bytes[len] = 0; // EOF

    r_out.parse_utf8(reinterpret_cast<const char *>(bytes.ptr()));

    return OK;
}
