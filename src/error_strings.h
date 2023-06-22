#pragma once

#define ENSURE_GODOT_COMPAT_ERR "Ensure a Godot-compatible type is used."

#define DEP_ADD_FAILED_ERR(m_path) ("Cannot add module at " + m_path + " as dependency") // String
#define CYCLIC_SCRIPT_REF_ERR(m_dep, m_to) ("Cyclic reference detected; cannot add script at " + m_dep + " as dependency for script at " + m_to) // String

/* ANNOTATION ERRORS */

// Common
#define INVALID_ANNOTATION_ERR(m_name) ("@" + m_name + " is not valid for this node") // String
#define PROPERTY_TYPE_ERR(m_annotation, m_type) m_annotation " requires a property of type " m_type
#define ANN_NO_ARGS_ERR(m_annotation) m_annotation " takes no arguments"
#define ANN_AT_LEAST_ONE_ARG_ERR(m_annotation) (m_annotation + " requires at least one argument") // String

#define INVALID_FLAG_ERR(m_name) (m_name + " is not a valid flag") // String

// Definition
#define CLASS_ARG_ERR "@class only takes one optional argument for its global name"
#define CLASSTYPE_ARG_ERR "@classType requires one argument for class name"

#define EXTENDS_ARG_ERR "@extends only takes one argument for the Godot base type or the name of the required base class local"
#define EXTENDS_LOAD_ERR(m_path) ("Failed to load base script at " + m_path) // String
#define EXTENDS_NOT_FOUND_ERR(m_extends) ("Could not find base class '" + m_extends + "'; ensure it is a valid Godot type or a Luau class required and assigned to a local before this annotation") // String

#define PERMISSIONS_ARG_ERR "@permissions requires one or more permission flags"
#define PERMISSIONS_NON_CORE_ERR "!!! Cannot set permissions on a non-core script !!!"

#define ICONPATH_PATH_ERR "@iconPath path is invalid or missing"

// Methods
#define AST_FAILED_ERR "Failed to register method with AST - check that you have met necessary conventions"

#define PARAM_ARG_ERR "@param requires at least one argument for the parameter name"

#define FLAGS_ARG_ERR "@flags requires one or more method flags"

#define RPC_ARG_ERR(m_option) ("Invalid RPC option '" + m_option + "'") // String
#define RPC_OVERRIDE_ERR(m_option) ("RPC option '" + m_option + "' will override a previously provided option") // String

#define DEFAULTARGS_ARG_ERR "@defaultArgs requires an Array of default values"
#define DEFAULTARGS_ARG_PARSE_ERR "Failed to parse argument array; ensure it is compatible with `str_to_var` and of the correct type"

// Signals
#define SIGNAL_TYPE_ERR "@signal property type is not a plain type reference (Signal or Signal<void function type>)"
#define SIGNAL_TYPE_PARAM_ERR "Signal type only takes 0 or 1 void function type parameters. Return types and generics are not supported."
#define SIGNAL_PARAM_INVALID_TYPE_ERR "Signal parameter type is invalid. " ENSURE_GODOT_COMPAT_ERR

// Properties
#define PROP_SIGNAL_ME_ERR "@property and @signal can only be declared once and are mutually exclusive"
#define PROP_INVALID_TYPE_ERR "Property type is invalid. " ENSURE_GODOT_COMPAT_ERR

#define DEFAULT_ARG_ERR "@default takes one argument for the default property value"
#define DEFAULT_ARG_PARSE_ERR "Failed to parse default value; ensure it is compatible with `str_to_var`. For a null value, use `null`."
#define DEFAULT_ARG_TYPE_ERR(m_expected, m_got) ("Default value has the incorrect type; expected " + m_expected + ", got " + m_got) // String

#define SETGET_ARG_ERR(m_annotation) (m_annotation + " takes one argument for the method") // String
#define SETGET_WHITESPACE_ERR(m_annotation) (m_annotation + " method name cannot contain whitespaces") // String

#define RANGE_ARG_ERR "@range requires at least two arguments for min and max values"
#define RANGE_ARG_TYPE_ERR "@range requires arguments to be of correct type (float for float properties, int for int properties)"

#define FILE_ARG_ERR "Arguments to @file should either be 'global' or an extension in the format *.ext"
#define DIR_ARG_ERR "Only acceptable argument to @dir is 'global'"

#define PLACEHOLDER_ARG_ERR "@placeholderText requires one argument for the placeholder text"

#define EXPEASING_ARG_ERR "@expEasing expects a value of either 'attenuation' or 'positiveOnly'"
