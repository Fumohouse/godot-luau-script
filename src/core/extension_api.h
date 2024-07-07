#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "core/permissions.h"
#include "core/variant.h"

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

	GDExtensionVariantType get_arg_type() const { return type; }
	String get_arg_type_name() const { return ""; }
};

struct ApiArgumentNoDefault {
	const char *name;
	GDExtensionVariantType type;

	GDExtensionVariantType get_arg_type() const { return type; }
	String get_arg_type_name() const { return ""; }
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

	bool is_method_static() const { return true; }
	bool is_method_vararg() const { return is_vararg; }
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

	bool is_method_static() const { return is_static; }
	bool is_method_vararg() const { return is_vararg; }
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

	GDExtensionVariantType get_arg_type() const { return GDExtensionVariantType(type.type); }
	const String &get_arg_type_name() const { return type.type_name; }
};

struct ApiClassMethod {
	const char *name;
	const char *gd_name;
	const char *debug_name;

	ThreadPermissions permissions = PERMISSION_INHERIT;

	bool is_const;
	bool is_static;
	bool is_vararg;

	GDExtensionMethodBindPtr bind = nullptr;
	Vector<ApiClassArgument> arguments;
	ApiClassType return_type;

	bool is_method_static() const { return is_static; }
	bool is_method_vararg() const { return is_vararg; }
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

	GDExtensionObjectPtr singleton = nullptr;
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

const ExtensionApi &get_extension_api();

// Corresponding definitions are generated.
extern const Variant &get_variant_value(int p_idx);
extern const uint8_t api_bin[];
extern const uint64_t api_bin_length;
