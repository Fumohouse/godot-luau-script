#pragma once

// Centralized error strings for ease of repetition and convention enforcement.
// Current conventions:
// - No period necessary unless multiple sentences
// - Luau errors do not begin with a capital (per Lua's own convention)
// - Parse errors begin with a capital

#define FILE_READ_FAILED_ERR(m_path) ("Failed to read file at " + m_path) // String
#define FILE_SAVE_FAILED_ERR(m_path) ("Failed to save file at " + m_path) // String
#define DIR_OPEN_ERR(m_path) ("Failed to open directory at " + m_path) // String
#define DIR_LIST_ERR(m_path) ("Failed to list directory at " + m_path) // String

#define NON_NULL_OBJ_DEFAULT_ARG_ERR "Could not set non-null object argument default value"

#define VAR_LUAU_PUSH_ERR "Pushing a value from Luau back to Luau is unsupported"
#define VAR_TYPE_UNINIT_ERR "Variant type was left uninitialized"

#define LANG_REG_FAILED_ERR "Failed to register LuauLanguage"

/* LUAU ERRORS */

// Relatively generic errors

#define MT_LOCKED_MSG "This metatable is locked."

#define luaGD_objnullerror(L, p_i) luaL_error(L, "argument #%d: Object is null or freed", p_i)
#define luaGD_nonamecallatomerror(L) luaL_error(L, "no namecallatom")
#define luaGD_mtnotfounderror(L, p_name) luaL_error(L, "metatable not found: '%s'", p_name)

#define luaGD_indexerror(L, p_key, p_of) luaL_error(L, "'%s' is not a valid member of %s", p_key, p_of)
#define luaGD_keyindexerror(L, p_type, p_key) luaL_error(L, "this %s does not have key '%s'", p_type, p_key)
#define luaGD_nomethoderror(L, p_key, p_of) luaL_error(L, "'%s' is not a valid method of %s", p_key, p_of)
#define luaGD_valueerror(L, p_key, p_got, p_expected) luaL_error(L, "invalid type for value of key %s: got %s, expected %s", p_key, p_got, p_expected)

#define luaGD_readonlyerror(L, p_type) luaL_error(L, "type '%s' is read-only", p_type)
#define luaGD_propreadonlyerror(L, p_prop) luaL_error(L, "property '%s' is read-only", p_prop)
#define luaGD_propwriteonlyerror(L, p_prop) luaL_error(L, "property '%s' is write-only", p_prop)
#define luaGD_constassignerror(L, p_key) luaL_error(L, "cannot assign to constant '%s'", p_key)

#define luaGD_toomanyargserror(L, p_method, p_max) luaL_error(L, "too many arguments to '%s' (expected at most %d)", p_method, p_max)

// Specific errors

#define DICT_ITER_PREV_KEY_MISSING_ERR "could not find previous key in dictionary: did you erase its value during iteration?"
#define REFCOUNTED_FREE_ERR "cannot free a RefCounted object"
#define CLASS_MT_NOT_FOUND_ERR "metatable not found for class %s" // format
#define TYPED_ARRAY_TYPE_ERR "expected type %s for typed array element, got %s (index %d)" // format
#define PERMISSIONS_ERR "!!! THREAD PERMISSION VIOLATION: attempted to access '%s'. needed permissions: %li, got: %li !!!" // format

#define NO_INDEXED_KEYED_SETTER_ERR "class %s does not have any indexed or keyed setter" // format
#define SETGET_NOT_FOUND_ERR "setter/getter '%s' was not found" // format

#define NO_OP_MATCHED_ERR "no operator matched"
#define OP_NOT_HANDLED_ERR "Variant operator not handled"

#define PROP_GET_FAILED_PREV_ERR "failed to get property '%s'; see previous errors for more information" // format
#define PROP_GET_FAILED_UNK_ERR "failed to get property '%s': unknown error" // format
#define PROP_SET_FAILED_PREV_ERR "failed to set property '%s'; see previous errors for more information" // format
#define PROP_SET_FAILED_UNK_ERR "failed to set property '%s': unknown error" // format

#define SIGNAL_READ_ONLY_ERR "cannot assign to signal '%s'" // format

/* LUAU_LIB ERRORS */

#define GDPROPERTY_TYPE_ERR "expected table type for GDProperty value"
#define NEW_DURING_LOAD_ERR "cannot instantiate: script is loading"
#define GDCLASS_CUSTOM_MT_ERR "custom metatables are not supported on class definitions"
#define SINGLETON_NOT_FOUND_ERR "singleton '%s' was not found" // format

#define REQUIRE_FAILED_ERR "require failed: %s" // format
#define REQUIRE_CURRENT_ERR "cannot require current script"
#define REQUIRE_NOT_FOUND_ERR "could not find module: %s" // format
#define REQUIRE_CYCLIC_ERR "cyclic dependency detected in %s. halting require of %s." // format
#define REQUIRE_INVALID_VM_ERR "could not get class definition: unknown VM"
#define REQUIRE_CLASS_TABLE_GET_ERR(m_path) ("could not get class definition for script at " + m_path) // String

#define CLASS_GLOBAL_EXPECTED_ERR "expected a class global (i.e. metatable containing the " MT_CLASS_GLOBAL " field)"
#define SCRIPT_UNNAMED_ERR "could not determine script name from script at %s; did you name it?" // format

/* SCRIPT ERRORS */

#define EXEC_TIME_EXCEEDED_ERR(m_max_time) "thread exceeded maximum execution time (" m_max_time " seconds)"
#define CONST_NOT_VARIANT_ERR(m_key) ("Registered constant '" + m_key + "' is not a Variant") // String
#define RESOLVE_PATH_PATH_EMPTY_ERR "Failed to resolve path: Script path is empty"
#define LUA_ERROR_PARSE_ERR(m_msg) ("Failed to parse Lua error: " + m_msg) // String
#define THREAD_VM_INVALID_ERR "Thread has an unknown VM type"
#define EXPECTED_RET_ERR(m_name, m_type) "Expected " m_name " to return a " m_type
#define NON_VOID_YIELD_ERR "Non-void method yielded unexpectedly"
#define NON_CORE_PERM_DECL_ERR "!!! Non-core script declared permissions !!!"

#define DEP_ADD_FAILED_ERR(m_path) ("Cannot add module at " + m_path + " as dependency") // String
#define CYCLIC_SCRIPT_REF_ERR(m_dep, m_to) ("Cyclic reference detected; cannot add script at " + m_dep + " as dependency for script at " + m_to) // String

#define COMPILE_ERR "Compilation failed"
#define ALREADY_LOADING_ERR "Cyclic dependency detected: Requested script load when it was already loading"
#define YIELD_DURING_LOAD_ERR "Script unexpectedly yielded during table load"
#define INVALID_CLASS_TABLE_ERR "Script did not return a valid class table. Did you return a table processed by `gdclass`?"

#define MODULE_YIELD_ERR "module unexpectedly yielded during load"
#define MODULE_RET_ERR "module must return a function or table"

#define SETTER_NOT_FOUND_ERR(m_name) ("Setter for '" + m_name + "' not found") // String
#define GETTER_NOT_FOUND_ERR(m_name) ("Getter for '" + m_name + "' not found") // String
#define GET_TABLE_TYPE_ERR(m_name) ("Table entry for '" + m_name + "' is the wrong type") // String
#define GETTER_RET_ERR(m_name) ("Getter for '" + m_name + "' returned the wrong type")
#define GETTER_YIELD_ERR(m_name) ("Getter for '" + m_name + "' yielded unexpectedly") // String

#define METHOD_LOAD_ERR(m_path) ("Couldn't load script methods for " + m_path) // String
#define INIT_YIELD_ERR "_Init yielded unexpectedly"
#define INIT_ERR(m_err) ("_Init failed: " + m_err) // String

/* ANNOTATION/ANALYSIS ERRORS */

#define CLASS_PARSE_ERR(m_msg) ("Failed to parse class: " + m_msg) // String
#define NO_CLASS_TABLE_ERR CLASS_PARSE_ERR(String("Could not find class table")) // kinda yuck

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
#define SIGNAL_PARAM_INVALID_TYPE_ERR "Signal parameter type is invalid. Ensure a Godot-compatible type is used."

// Properties
#define PROP_SIGNAL_ME_ERR "@property and @signal can only be declared once and are mutually exclusive"
#define PROP_INVALID_TYPE_ERR "Property type is invalid. Ensure a Godot-compatible type is used."

#define DEFAULT_ARG_ERR "@default takes one argument for the default property value"
#define DEFAULT_ARG_PARSE_ERR "Failed to parse default value; ensure it is compatible with `str_to_var`. For a null value, use `null`."
#define DEFAULT_ARG_TYPE_ERR(m_expected, m_got) ("Default value has the incorrect type; expected " + m_expected + ", got " + m_got) // String

#define SETGET_ARG_ERR(m_annotation) (m_annotation + " takes one argument for the method") // String
#define SETGET_WHITESPACE_ERR(m_annotation) (m_annotation + " method name cannot contain whitespaces") // String

#define RANGE_ARG_ERR "@range requires at least two arguments for min and max values"
#define RANGE_ARG_TYPE_ERR "@range requires arguments to be of correct type (float for float properties, int for int properties)"
#define RANGE_SPECIAL_OPT_ERR "Invalid option for @range; expected one of 'orGreater', 'orLess', 'hideSlider', 'radians', 'degrees', 'exp', or 'suffix:<keyword>'"

#define FILE_ARG_ERR "Arguments to @file should either be 'global' or an extension in the format *.ext"
#define DIR_ARG_ERR "Only acceptable argument to @dir is 'global'"

#define PLACEHOLDER_ARG_ERR "@placeholderText requires one argument for the placeholder text"

#define EXPEASING_ARG_ERR "@expEasing expects a value of either 'attenuation' or 'positiveOnly'"
