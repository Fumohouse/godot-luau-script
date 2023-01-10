#include "extension_api.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd.h"
#include "luagd_variant.h"

using namespace godot;

#define LOG(...) UtilityFunctions::print_verbose("[extension_api] ", __VA_ARGS__)
#define LOG_PROGRESS LOG(idx, " out of ", api_bin_length, " bytes loaded...")
#define LOG_LEN(len, what) LOG("found ", len, " ", what)

//////////////////////////////
// Reading from the bin/inc //
//////////////////////////////

static void get_variant_const(LuauVariant &out, int32_t idx) {
    const Variant &value = get_variant_value(idx);

    out.initialize((GDExtensionVariantType)value.get_type());
    out.assign_variant(value);
}

template <typename T>
static T read(uint64_t &idx) {
    T val = *((T *)(api_bin + idx));
    idx += sizeof(T);

    return val;
}

template <typename T>
static T read_uenum(uint64_t &idx) {
    return (T)read<uint32_t>(idx);
}

template <typename T>
static T read_enum(uint64_t &idx) {
    return (T)read<int32_t>(idx);
}

static const char *read_string(uint64_t &idx) {
    uint64_t len = read<uint64_t>(idx);
    if (len == 0)
        return "";

    const char *ptr = (const char *)(api_bin + idx);

    idx += len;
    return ptr;
}

static ApiEnum read_api_enum(uint64_t &idx) {
    ApiEnum e;
    e.name = read_string(idx);
    e.is_bitfield = read<uint8_t>(idx);

    uint64_t num_values = read<uint64_t>(idx);
    e.values.resize(num_values);

    Pair<String, int32_t> *values = e.values.ptrw();

    for (int j = 0; j < num_values; j++) {
        values[j] = {
            read_string(idx), // name
            read<int32_t>(idx) // value
        };
    }

    return e;
}

static ApiConstant read_api_constant(uint64_t &idx) {
    return {
        read_string(idx), // name
        read<int64_t>(idx) // value
    };
}

static ApiArgumentNoDefault read_arg_no_default(uint64_t &idx) {
    return {
        read_string(idx), // name
        read_uenum<GDExtensionVariantType>(idx) // type
    };
}

static ApiVariantMethod read_builtin_method(GDExtensionVariantType type, uint64_t &idx) {
    ApiVariantMethod method;
    method.name = read_string(idx);
    method.gd_name = read_string(idx);
    method.debug_name = read_string(idx);

    method.is_vararg = read<uint8_t>(idx);
    method.is_static = read<uint8_t>(idx);
    method.is_const = read<uint8_t>(idx);

    uint32_t hash = read<uint32_t>(idx);
    method.func = internal::gde_interface->variant_get_ptr_builtin_method(type, &method.gd_name, hash);

    uint64_t num_arguments = read<uint64_t>(idx);
    method.arguments.resize(num_arguments);

    ApiArgument *args = method.arguments.ptrw();

    for (int i = 0; i < num_arguments; i++) {
        ApiArgument &arg = args[i];
        arg.name = read_string(idx);
        arg.type = read_uenum<GDExtensionVariantType>(idx);

        int32_t default_variant_index = read<int32_t>(idx);
        if (default_variant_index != -1) {
            arg.has_default_value = true;
            get_variant_const(arg.default_value, default_variant_index);
        }
    }

    method.return_type = read<int32_t>(idx);

    return method;
}

static ApiClassType read_class_type(uint64_t &idx) {
    ApiClassType type;

    type.type = read<int32_t>(idx);
    type.type_name = read_string(idx);

    type.is_enum = read<uint8_t>(idx);
    type.is_bitfield = read<uint8_t>(idx);
    type.typed_array_type = read_uenum<GDExtensionVariantType>(idx);

    return type;
}

static ApiClassArgument read_class_arg(uint64_t &idx) {
    ApiClassArgument arg;

    arg.name = read_string(idx);
    arg.type = read_class_type(idx);

    int32_t default_variant_index = read<int32_t>(idx);

    if (default_variant_index != -1) {
        arg.has_default_value = true;
        get_variant_const(arg.default_value, default_variant_index);
    }

    return arg;
}

static ApiClassMethod read_class_method(uint64_t &idx, const char *class_name) {
    ApiClassMethod method;

    method.class_name = class_name;
    method.name = read_string(idx);
    method.gd_name = read_string(idx);
    method.debug_name = read_string(idx);

    method.permissions = read_enum<ThreadPermissions>(idx);

    method.is_const = read<uint8_t>(idx);
    method.is_static = read<uint8_t>(idx);
    method.is_vararg = read<uint8_t>(idx);

    method.hash = read<uint32_t>(idx);

    uint64_t num_args = read<uint64_t>(idx);
    method.arguments.resize(num_args);

    ApiClassArgument *args = method.arguments.ptrw();

    for (int i = 0; i < num_args; i++)
        args[i] = read_class_arg(idx);

    method.return_type = read_class_type(idx);

    return method;
}

//////////////////////////
// Array __len operator //
//////////////////////////

template <typename T>
static void array_get_len(GDExtensionConstTypePtr p_left, GDExtensionConstTypePtr, GDExtensionTypePtr r_result) {
    *((int64_t *)r_result) = ((T *)p_left)->size();
}

// ! sync with any new arrays
static GDExtensionPtrOperatorEvaluator get_len_evaluator(GDExtensionVariantType type) {
    switch (type) {
        case GDEXTENSION_VARIANT_TYPE_ARRAY:
            return array_get_len<Array>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY:
            return array_get_len<PackedByteArray>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY:
            return array_get_len<PackedInt32Array>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_INT64_ARRAY:
            return array_get_len<PackedInt64Array>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY:
            return array_get_len<PackedFloat32Array>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT64_ARRAY:
            return array_get_len<PackedFloat64Array>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY:
            return array_get_len<PackedStringArray>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR2_ARRAY:
            return array_get_len<PackedVector2Array>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY:
            return array_get_len<PackedVector3Array>;
        case GDEXTENSION_VARIANT_TYPE_PACKED_COLOR_ARRAY:
            return array_get_len<PackedColorArray>;

        default:
            return nullptr;
    }
}

//////////
// Main //
//////////

ExtensionApi &get_extension_api() {
    static ExtensionApi extension_api;
    static bool did_init;

    if (!did_init) {
        LOG("loading GDExtension api from binary...");

        uint64_t idx = 0;

        // Global enums
        {
            uint64_t num_enums = read<uint64_t>(idx);
            LOG_LEN(num_enums, "global enum(s)");
            extension_api.global_enums.resize(num_enums);

            ApiEnum *enums = extension_api.global_enums.ptrw();

            for (int i = 0; i < num_enums; i++)
                enums[i] = read_api_enum(idx);
        }

        LOG_PROGRESS;

        // Global constants
        {
            uint64_t num_constants = read<uint64_t>(idx);
            LOG_LEN(num_constants, "global constant(s)");
            extension_api.global_constants.resize(num_constants);

            ApiConstant *constants = extension_api.global_constants.ptrw();

            for (int i = 0; i < num_constants; i++)
                constants[i] = read_api_constant(idx);
        }

        LOG_PROGRESS;

        // Utility functions
        {
            uint64_t num_utility_functions = read<uint64_t>(idx);
            LOG_LEN(num_utility_functions, "utility function(s)");
            extension_api.utility_functions.resize(num_utility_functions);

            ApiUtilityFunction *utility_functions = extension_api.utility_functions.ptrw();

            for (int i = 0; i < num_utility_functions; i++) {
                ApiUtilityFunction &func = utility_functions[i];

                func.name = read_string(idx);
                StringName gd_name = read_string(idx);
                func.debug_name = read_string(idx);
                func.is_vararg = read<uint8_t>(idx);

                uint32_t hash = read<uint32_t>(idx);
                func.func = internal::gde_interface->variant_get_ptr_utility_function(&gd_name, hash);

                uint64_t num_args = read<uint64_t>(idx);
                func.arguments.resize(num_args);

                ApiArgumentNoDefault *args = func.arguments.ptrw();

                for (int j = 0; j < num_args; j++)
                    args[j] = read_arg_no_default(idx);

                func.return_type = read<int32_t>(idx);
            }
        }

        LOG_PROGRESS;

        // Builtin classes
        {
            uint64_t num_builtin_classes = read<uint64_t>(idx);
            LOG_LEN(num_builtin_classes, "builtin class(es)");
            extension_api.builtin_classes.resize(num_builtin_classes);

            ApiBuiltinClass *builtin_classes = extension_api.builtin_classes.ptrw();

            for (int i = 0; i < num_builtin_classes; i++) {
                // Info
                ApiBuiltinClass &new_class = builtin_classes[i];

                new_class.name = read_string(idx);
                new_class.metatable_name = read_string(idx);
                new_class.type = read_uenum<GDExtensionVariantType>(idx);

                // Keyed setget
                bool is_keyed = read<uint8_t>(idx);

                if (is_keyed) {
                    new_class.keyed_setter = internal::gde_interface->variant_get_ptr_keyed_setter(new_class.type);
                    new_class.keyed_getter = internal::gde_interface->variant_get_ptr_keyed_getter(new_class.type);
                    new_class.keyed_checker = internal::gde_interface->variant_get_ptr_keyed_checker(new_class.type);
                }

                // Indexed setget
                int32_t indexing_return_type = read<int32_t>(idx);

                if (indexing_return_type != -1) {
                    new_class.indexing_return_type = (GDExtensionVariantType)indexing_return_type;

                    if (String(new_class.name).ends_with("Array"))
                        new_class.indexed_setter = internal::gde_interface->variant_get_ptr_indexed_setter(new_class.type);

                    new_class.indexed_getter = internal::gde_interface->variant_get_ptr_indexed_getter(new_class.type);
                }

                // Enums
                uint64_t num_enums = read<uint64_t>(idx);
                new_class.enums.resize(num_enums);

                ApiEnum *enums = new_class.enums.ptrw();

                for (int j = 0; j < num_enums; j++)
                    enums[j] = read_api_enum(idx);

                // Constants
                uint64_t num_constants = read<uint64_t>(idx);
                new_class.constants.resize(num_constants);

                ApiVariantConstant *constants = new_class.constants.ptrw();

                for (int j = 0; j < num_constants; j++) {
                    ApiVariantConstant &constant = constants[j];

                    constant.name = read_string(idx);

                    StringName constant_name = constant.name;
                    internal::gde_interface->variant_get_constant_value(new_class.type, &constant_name, &constant.value);
                }

                // Constructors
                uint64_t num_constructors = read<uint64_t>(idx);
                new_class.constructors.resize(num_constructors);

                ApiVariantConstructor *constructors = new_class.constructors.ptrw();

                for (int j = 0; j < num_constructors; j++) {
                    ApiVariantConstructor &constructor = constructors[j];

                    uint64_t num_args = read<uint64_t>(idx);
                    constructor.arguments.resize(num_args);

                    ApiArgumentNoDefault *args = constructor.arguments.ptrw();

                    for (int k = 0; k < num_args; k++)
                        args[k] = read_arg_no_default(idx);

                    constructor.func = internal::gde_interface->variant_get_ptr_constructor(new_class.type, j);
                }

                new_class.constructor_debug_name = read_string(idx);
                new_class.constructor_error_string = read_string(idx);

                // Members
                uint64_t num_members = read<uint64_t>(idx);
                new_class.members.reserve(num_members);

                for (int j = 0; j < num_members; j++) {
                    const char *name = read_string(idx);

                    ApiVariantMember member;
                    member.name = name;
                    member.type = read_uenum<GDExtensionVariantType>(idx);

                    StringName member_name = name;
                    member.getter = internal::gde_interface->variant_get_ptr_getter(new_class.type, &member_name);

                    new_class.members.insert(member.name, member);
                }

                new_class.newindex_debug_name = read_string(idx);
                new_class.index_debug_name = read_string(idx);

                // Methods
                uint64_t num_instance_methods = read<uint64_t>(idx);
                new_class.methods.reserve(num_instance_methods);

                for (int j = 0; j < num_instance_methods; j++) {
                    ApiVariantMethod method = read_builtin_method(new_class.type, idx);
                    new_class.methods.insert(method.name, method);
                }

                new_class.namecall_debug_name = read_string(idx);

                uint64_t num_static_methods = read<uint64_t>(idx);
                new_class.static_methods.resize(num_static_methods);

                ApiVariantMethod *static_methods = new_class.static_methods.ptrw();

                for (int j = 0; j < num_static_methods; j++)
                    static_methods[j] = read_builtin_method(new_class.type, idx);

                // Operators
                uint64_t num_operator_types = read<uint64_t>(idx);
                new_class.operators.reserve(num_operator_types);
                new_class.operator_debug_names.reserve(num_operator_types);

                for (int j = 0; j < num_operator_types; j++) {
                    GDExtensionVariantOperator op = read_uenum<GDExtensionVariantOperator>(idx);

                    Vector<ApiVariantOperator> operators;
                    uint64_t num_operators = read<uint64_t>(idx);
                    operators.resize(num_operators);

                    ApiVariantOperator *ops_ptr = operators.ptrw();

                    for (int k = 0; k < num_operators; k++) {
                        ops_ptr[k].right_type = read_uenum<GDExtensionVariantType>(idx);
                        ops_ptr[k].return_type = read_uenum<GDExtensionVariantType>(idx);

                        if (op == GDEXTENSION_VARIANT_OP_MAX) {
                            // Special array __len
                            ops_ptr[k].eval = get_len_evaluator(new_class.type);
                        } else {
                            ops_ptr[k].eval = internal::gde_interface->variant_get_ptr_operator_evaluator(op, new_class.type, ops_ptr[k].right_type);
                        }
                    }

                    new_class.operators.insert(op, operators);
                    new_class.operator_debug_names.insert(op, read_string(idx));
                }
            }
        }

        LOG_PROGRESS;

        // Classes
        {
            uint64_t num_classes = read<uint64_t>(idx);
            LOG_LEN(num_classes, "class(es)");
            extension_api.classes.resize(num_classes);

            ApiClass *classes = extension_api.classes.ptrw();

            for (int i = 0; i < num_classes; i++) {
                ApiClass &new_class = classes[i];

                new_class.name = read_string(idx);
                new_class.metatable_name = read_string(idx);
                new_class.parent_idx = read<int32_t>(idx);
                new_class.default_permissions = read_enum<ThreadPermissions>(idx);

                // Enums
                uint64_t num_enums = read<uint64_t>(idx);
                new_class.enums.resize(num_enums);

                ApiEnum *enums = new_class.enums.ptrw();

                for (int j = 0; j < num_enums; j++)
                    enums[j] = read_api_enum(idx);

                // Constants
                uint64_t num_constants = read<uint64_t>(idx);
                new_class.constants.resize(num_constants);

                ApiConstant *constants = new_class.constants.ptrw();

                for (int j = 0; j < num_constants; j++)
                    constants[j] = read_api_constant(idx);

                // Constructor
                new_class.is_instantiable = read<uint8_t>(idx);
                new_class.constructor_debug_name = read_string(idx);

                // Methods
                uint64_t num_methods = read<uint64_t>(idx);
                new_class.methods.reserve(num_methods);

                for (int j = 0; j < num_methods; j++) {
                    ApiClassMethod method = read_class_method(idx, new_class.name);
                    new_class.methods.insert(method.name, method);
                }

                if (num_methods > 0)
                    new_class.namecall_debug_name = read_string(idx);

                uint64_t num_static_methods = read<uint64_t>(idx);
                new_class.static_methods.resize(num_static_methods);

                ApiClassMethod *static_methods = new_class.static_methods.ptrw();

                for (int j = 0; j < num_static_methods; j++)
                    static_methods[j] = read_class_method(idx, new_class.name);

                // Signals
                uint64_t num_signals = read<uint64_t>(idx);
                new_class.signals.reserve(num_signals);

                for (int j = 0; j < num_signals; j++) {
                    ApiClassSignal signal;

                    signal.name = read_string(idx);
                    signal.gd_name = read_string(idx);

                    uint64_t num_args = read<uint64_t>(idx);
                    signal.arguments.resize(num_args);

                    ApiClassArgument *args = signal.arguments.ptrw();

                    for (int k = 0; k < num_args; k++)
                        args[k] = read_class_arg(idx);

                    new_class.signals.insert(signal.name, signal);
                }

                // Properties
                uint64_t num_properties = read<uint64_t>(idx);
                new_class.properties.reserve(num_properties);

                for (int j = 0; j < num_properties; j++) {
                    ApiClassProperty prop;

                    prop.name = read_string(idx);

                    // Types
                    uint64_t num_types = read<uint64_t>(idx);
                    prop.type.resize(num_types);

                    ApiClassType *types = prop.type.ptrw();

                    for (int k = 0; k < num_types; k++)
                        types[k] = read_class_type(idx);

                    // Set/get
                    prop.setter = read_string(idx);
                    prop.getter = read_string(idx);

                    prop.index = read<int32_t>(idx);

                    new_class.properties.insert(prop.name, prop);
                }

                new_class.newindex_debug_name = read_string(idx);
                new_class.index_debug_name = read_string(idx);

                // Singleton
                new_class.singleton_name = read_string(idx);
                new_class.singleton_getter_debug_name = read_string(idx);
            }
        }

        LOG_PROGRESS;
        LOG("done!");

        did_init = true;
    }

    return extension_api;
}