#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/defs.hpp> // TODO: 4.0-beta10: pair.hpp does not include, causes errors.
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_permissions.h"
#include "luagd_variant.h"

using namespace godot;

// ! sync with core/extension/extension_api_dump.cpp

///////////////////
// Generic stuff //
///////////////////

struct ApiArgument {
    const char *name;
    GDExtensionVariantType type;
    bool has_default_value = false;
    LuauVariant default_value;
};

struct ApiArgumentNoDefault {
    const char *name;
    GDExtensionVariantType type;
};

struct ApiEnum {
    const char *name;
    bool is_bitfield;
    Vector<Pair<String, int32_t>> values;
};

struct ApiConstant {
    const char *name;
    int64_t value;
};

///////////////////////
// Utility functions //
///////////////////////

struct ApiUtilityFunction {
    const char *name;
    const char *debug_name;

    bool is_vararg;
    bool is_print_func;

    GDExtensionPtrUtilityFunction func;
    Vector<ApiArgumentNoDefault> arguments;
    int32_t return_type; // GDNativeVariantType or -1 if none; NIL -> Variant
};

//////////////
// Builtins //
//////////////

struct ApiVariantOperator {
    GDExtensionVariantType right_type;
    GDExtensionPtrOperatorEvaluator eval;
    GDExtensionVariantType return_type;
};

struct ApiVariantMember {
    const char *name;
    GDExtensionVariantType type;

    // builtin type members are read-only in luau
    // GDExtensionPtrSetter setter;
    GDExtensionPtrGetter getter;
};

struct ApiVariantConstant {
    const char *name;
    Variant value;
};

struct ApiVariantConstructor {
    GDExtensionPtrConstructor func;
    Vector<ApiArgumentNoDefault> arguments;
};

struct ApiVariantMethod {
    const char *name;
    StringName gd_name;
    const char *debug_name;

    bool is_vararg;
    bool is_static;
    bool is_const;

    GDExtensionPtrBuiltInMethod func;
    Vector<ApiArgument> arguments;
    int32_t return_type; // GDExtensionVariantType or -1 if none; NIL -> Variant
};

struct ApiBuiltinClass {
    const char *name;
    const char *metatable_name;

    GDExtensionVariantType type;

    GDExtensionPtrKeyedSetter keyed_setter = nullptr;
    GDExtensionPtrKeyedGetter keyed_getter = nullptr;
    GDExtensionPtrKeyedChecker keyed_checker = nullptr; // `has` which returns a uint32_t for some reason

    GDExtensionPtrIndexedSetter indexed_setter = nullptr;
    GDExtensionPtrIndexedGetter indexed_getter = nullptr;
    GDExtensionVariantType indexing_return_type; // if no indexer, the set/get will be nullptr

    Vector<ApiEnum> enums;
    Vector<ApiVariantConstant> constants;

    Vector<ApiVariantConstructor> constructors;
    const char *constructor_debug_name;
    const char *constructor_error_string;

    HashMap<String, ApiVariantMember> members;
    const char *newindex_debug_name;
    const char *index_debug_name;

    HashMap<String, ApiVariantMethod> methods;
    const char *namecall_debug_name;

    Vector<ApiVariantMethod> static_methods;

    HashMap<GDExtensionVariantOperator, Vector<ApiVariantOperator>> operators;
    HashMap<GDExtensionVariantOperator, const char *> operator_debug_names;
};

/////////////
// Classes //
/////////////

struct ApiClassType {
    int32_t type = -1; // GDExtensionVariantType or -1 if none; NIL -> Variant
    String type_name; // if OBJECT, need to check on set for properties; if bitfield/enum then it's indicated here

    bool is_enum;
    bool is_bitfield;
};

struct ApiClassArgument {
    const char *name;

    ApiClassType type;

    bool has_default_value;
    LuauVariant default_value;
};

struct ApiClassMethod {
    GDExtensionMethodBindPtr method = nullptr;

public:
    const char *class_name;

    const char *name;
    const char *gd_name;
    const char *debug_name;

    ThreadPermissions permissions = PERMISSION_INHERIT;

    bool is_const;
    bool is_static;
    bool is_vararg;

    uint32_t hash;
    Vector<ApiClassArgument> arguments;
    ApiClassType return_type;

    // avoid issues with getting this before method binds are initialized
    _FORCE_INLINE_ GDExtensionMethodBindPtr try_get_method_bind() {
        if (method)
            return method;

        StringName class_sn = class_name;
        StringName gd_sn = gd_name;
        method = internal::gde_interface->classdb_get_method_bind(&class_sn, &gd_sn, hash);
        return method;
    }
};

struct ApiClassSignal {
    const char *name;
    StringName gd_name;
    Vector<ApiClassArgument> arguments;
};

struct ApiClassProperty {
    const char *name;

    Vector<ApiClassType> type;

    String setter;
    String getter;

    // added in https://github.com/godotengine/godot/pull/10117#issuecomment-320532035
    // pass to set/get as first argument if present
    int32_t index = -1;
};

struct ApiClass {
    GDExtensionObjectPtr singleton = nullptr;

public:
    const char *name;
    const char *metatable_name;
    int32_t parent_idx = -1;

    ThreadPermissions default_permissions = PERMISSION_INTERNAL;

    Vector<ApiEnum> enums;
    Vector<ApiConstant> constants;

    bool is_instantiable;
    const char *constructor_debug_name;

    HashMap<String, ApiClassMethod> methods;
    const char *namecall_debug_name;

    Vector<ApiClassMethod> static_methods;

    HashMap<String, ApiClassSignal> signals;
    HashMap<String, ApiClassProperty> properties;
    const char *newindex_debug_name;
    const char *index_debug_name;

    StringName singleton_name;
    const char *singleton_getter_debug_name;

    // avoid issues with getting singleton before they are initialized
    _FORCE_INLINE_ GDExtensionObjectPtr try_get_singleton() {
        if (!singleton)
            singleton = internal::gde_interface->global_get_singleton(&singleton_name);

        return singleton;
    }
};

/////////////////////
// Full definition //
/////////////////////

struct ExtensionApi {
    Vector<ApiEnum> global_enums;
    Vector<ApiConstant> global_constants;
    Vector<ApiUtilityFunction> utility_functions;
    Vector<ApiBuiltinClass> builtin_classes;
    Vector<ApiClass> classes;
};

ExtensionApi &get_extension_api();

// Corresponding definitions are generated.
extern const Variant &get_variant_value(int idx);
extern const uint8_t api_bin[];
extern const uint64_t api_bin_length;
