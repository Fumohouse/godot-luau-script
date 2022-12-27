#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp> // TODO: 4.0-beta10: pair.hpp does not include, causes errors.
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_permissions.h"

using namespace godot;

// ! sync with core/extension/extension_api_dump.cpp

///////////////////
// Generic stuff //
///////////////////

struct ApiArgument
{
    String name;
    GDExtensionVariantType type;
    bool has_default_value;
    Variant default_value;
};

struct ApiArgumentNoDefault
{
    String name;
    GDExtensionVariantType type;
};

struct ApiEnum
{
    String name;
    bool is_bitfield;
    Vector<Pair<String, int32_t>> values;
};

struct ApiConstant
{
    String name;
    int64_t value;
};

///////////////////////
// Utility functions //
///////////////////////

struct ApiUtilityFunction
{
    String name;
    const char *debug_name;

    bool is_vararg;

    GDExtensionPtrUtilityFunction func;
    Vector<ApiArgumentNoDefault> arguments;
    GDExtensionVariantType return_type;
};

//////////////
// Builtins //
//////////////

struct ApiVariantOperator
{
    GDExtensionVariantOperator op;
    GDExtensionVariantType right_type;
    GDExtensionVariantType return_type;

    GDExtensionPtrOperatorEvaluator eval;
};

struct ApiVariantMember
{
    String name;
    GDExtensionVariantType type;

    GDExtensionPtrSetter setter;
    GDExtensionPtrGetter getter;
};

struct ApiVariantConstant
{
    String name;
    Variant value;
};

struct ApiVariantMethod
{
    String name;
    const char *debug_name;

    bool is_vararg;
    bool is_static;

    GDExtensionPtrBuiltInMethod func;
    GDExtensionVariantType return_type;
    Vector<ApiArgument> arguments;
};

struct ApiVariantConstructor
{
    GDExtensionPtrConstructor func;
    Vector<ApiArgumentNoDefault> arguments;
};

struct ApiBuiltinClass
{
    String name;

    bool is_keyed;
    String indexing_return_type;

    GDExtensionPtrKeyedSetter keyed_setter;
    GDExtensionPtrKeyedGetter keyed_getter;
    GDExtensionPtrIndexedSetter indexed_setter;
    GDExtensionPtrIndexedGetter indexed_getter;

    Vector<ApiVariantConstructor> constructors;
    Vector<ApiVariantConstant> constants;
    Vector<ApiEnum> enums;
    Vector<ApiVariantMember> members;
    Vector<ApiVariantMethod> methods;
    Vector<ApiVariantOperator> operators;
};

/////////////
// Classes //
/////////////

struct ApiClassType
{
    GDExtensionVariantType type;
    String class_name; // if OBJECT, need to check on set for properties
    PropertyHint hint; // will indicate if enum, bitfield, typedarray
};

struct ApiClassArgument
{
    String name;

    ApiClassType type;

    bool has_default_value;
    Variant default_value;
};

struct ApiClassMethod
{
    String name;
    const char *debug_name;

    ThreadPermissions permissions = PERMISSION_INHERIT;

    bool is_const;
    bool is_static;
    bool is_vararg;
    bool is_virtual;

    GDExtensionMethodBindPtr method;
    GDExtensionVariantType return_type;
    Vector<ApiClassArgument> arguments;
};

struct ApiClassSignal
{
    String name;
    Vector<ApiArgumentNoDefault> arguments;
};

struct ApiClassProperty
{
    String name;

    ApiClassType type;

    String setter;
    String getter;

    // added in https://github.com/godotengine/godot/pull/10117#issuecomment-320532035
    // pass to set/get as first argument if present
    int32_t index = -1;
};

struct ApiClass
{
    String name;
    ThreadPermissions default_permissions = PERMISSION_INTERNAL;

    bool is_instantiable;
    int32_t parent_idx = -1;

    Vector<ApiConstant> constants;
    Vector<ApiEnum> enums;
    Vector<ApiClassMethod> methods;
    Vector<ApiClassSignal> signals;
    Vector<ApiClassProperty> properties;

    Object *singleton;
};

/////////////////////
// Full definition //
/////////////////////

struct ExtensionApi
{
    Vector<ApiEnum> global_enums;
    Vector<ApiConstant> global_constants;
    Vector<ApiUtilityFunction> utility_functions;
    Vector<ApiBuiltinClass> builtin_classes;
    Vector<ApiClass> classes;
};

// The implementation for this method is generated.
const ExtensionApi &get_extension_api();
