#include "analysis/analysis.h"

#include <Luau/Ast.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "analysis/analysis_utils.h"
#include "core/variant.h"
#include "scripting/luau_lib.h"
#include "utils/parsing.h"

using namespace godot;

void ClassReader::ann_signal(const Annotation &p_annotation, const Luau::AstTableProp &p_prop) {
#define SIGNAL_TYPE_PARAM_ERR "Signal type only takes 0 or 1 void function type parameters. Return types and generics are not supported."

	if (!p_annotation.args.is_empty()) {
		error("@signal takes no arguments", p_annotation.location);
	}

	Luau::AstTypeReference *type = p_prop.type->as<Luau::AstTypeReference>();
	if (!type || type->name != "Signal" && type->name != "SignalWithArgs") {
		error("@signal requires a property of type Signal or SignalWithArgs<void function type>", p_annotation.location);
		return;
	}

	GDMethod signal;
	signal.name = p_prop.name.value;

	// CONDITION HELL
	if (type->hasParameterList) {
		if (type->parameters.size != 1) {
			error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
			return;
		}

		Luau::AstTypeOrPack *type_or_pack = &type->parameters.data[0];
		if (!type_or_pack->type) {
			error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
			return;
		}

		if (Luau::AstTypeFunction *func_type = type_or_pack->type->as<Luau::AstTypeFunction>()) {
			if (func_type->returnTypes.types.size || func_type->returnTypes.tailType ||
					func_type->generics.size || func_type->genericPacks.size) {
				error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
				return;
			}

			const Luau::AstArray<Luau::AstType *> &arg_types = func_type->argTypes.types;
			signal.arguments.resize(arg_types.size);
			GDProperty *args = signal.arguments.ptrw();

			for (int i = 0; i < arg_types.size; i++) {
				if (!type_to_prop(root, script, arg_types.data[i], args[i])) {
					error("Signal parameter type is invalid; ensure a Godot-compatible type is used", p_annotation.location);
					return;
				}

				if (i >= func_type->argNames.size || !func_type->argNames.data[i].has_value()) {
					args[i].name = String("arg") + String::num_int64(i + 1);
					continue;
				}

				args[i].name = func_type->argNames.data[i]->first.value; // NOLINT(bugprone-unchecked-optional-access): wrong
			}

			if (Luau::AstTypePack *arg_tail = func_type->argTypes.tailType) {
				if (Luau::AstTypePackVariadic *arg_tail_var = arg_tail->as<Luau::AstTypePackVariadic>()) {
					if (Luau::AstTypeReference *arg_tail_type = arg_tail_var->variadicType->as<Luau::AstTypeReference>()) {
						signal.flags = signal.flags | METHOD_FLAG_VARARG;
					} else {
						error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
						return;
					}
				} else {
					error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
					return;
				}
			}
		} else {
			error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
			return;
		}
	}

	class_definition.signals.insert(p_prop.name.value, signal);
}

void ClassReader::ann_property_group(const Annotation &p_annotation, PropertyUsageFlags p_usage) {
	GDClassProperty prop;
	prop.property.name = p_annotation.args;
	prop.property.usage = p_usage;

	class_definition.set_prop(p_annotation.args, prop);
}

void ClassReader::ann_default_value(const Annotation &p_annotation, GDClassProperty &p_prop) {
	if (p_annotation.args.is_empty()) {
		error("@default takes one argument for the default property value", p_annotation.location);
		return;
	}

	Variant value = UtilityFunctions::str_to_var(p_annotation.args);

	// Unideal error detection but probably the best that can be done
	if (value == Variant() && p_annotation.args.strip_edges() != "null") {
		error("Failed to parse default value; ensure it is compatible with `str_to_var`. For a null value, use `null`.", p_annotation.location);
		return;
	}

	if (value.get_type() != Variant::Type(p_prop.property.type) &&
			p_prop.property.usage != PROPERTY_USAGE_NIL_IS_VARIANT) {
		error("Default value has the incorrect type; expected " +
						Variant::get_type_name(Variant::Type(p_prop.property.type)) +
						", got " + Variant::get_type_name(value.get_type()),
				p_annotation.location);

		return;
	}

	p_prop.default_value = value;
}

void ClassReader::ann_setget(const Annotation &p_annotation, const char *p_name, StringName &r_out) {
	if (p_annotation.args.is_empty()) {
		error(String(p_name) + " takes one argument for the method", p_annotation.location);
		return;
	}

	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();
	skip_whitespace(ptr);

	String value = read_until_whitespace(ptr);

	if (*ptr) {
		error(String(p_name) + " method name cannot contain whitespaces", p_annotation.location);
		return;
	}

	r_out = value;
}

template <typename T>
static void handle_range_internal(const Annotation &p_annotation, GDClassProperty &p_prop, ClassReader &p_reader) {
#define RANGE_ARG_ERR "@range requires at least two arguments for min and max values"
#define RANGE_ARG_TYPE_ERR "@range requires arguments to be of correct type (float for float properties, int for int properties)"

	if (p_annotation.args.is_empty()) {
		p_reader.error(RANGE_ARG_ERR, p_annotation.location);
		return;
	}

	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();

	T min;

	if (!read_number<T>(ptr, min)) {
		p_reader.error(RANGE_ARG_TYPE_ERR, p_annotation.location);
		return;
	}

	if (!*ptr) {
		p_reader.error(RANGE_ARG_ERR, p_annotation.location);
		return;
	}

	T max;

	if (!read_number<T>(ptr, max)) {
		p_reader.error(RANGE_ARG_TYPE_ERR, p_annotation.location);
		return;
	}

	T step = 1;
	const char *ptr_back = ptr;

	if (*ptr && !read_number<T>(ptr, step)) {
		ptr = ptr_back;
	}

	Array hint_values;
	hint_values.resize(3);
	hint_values[0] = min;
	hint_values[1] = max;
	hint_values[2] = step;

	String hint_string = String("{0},{1},{2}").format(hint_values);

	while (*ptr) {
		String option = read_until_whitespace(ptr);

		if (option == "orGreater") {
			hint_string += ",or_greater";
		} else if (option == "orLess") {
			hint_string += ",or_less";
		} else if (option == "hideSlider") {
			hint_string += ",hide_slider";
		} else if (option == "radians") {
			hint_string += ",radians";
		} else if (option == "degrees") {
			hint_string += ",degrees";
		} else if (option == "radiansAsDegrees") {
			hint_string += ",radians_as_degrees";
		} else if (option == "exp") {
			hint_string += ",exp";
		} else if (option.begins_with("suffix:")) {
			hint_string += "," + option;
		} else {
			p_reader.error("Invalid option for @range; expected one of 'orGreater', 'orLess', 'hideSlider', 'radians', 'degrees', 'exp', or 'suffix:<keyword>'", p_annotation.location);
		}
	}

	p_prop.property.hint = PROPERTY_HINT_RANGE;
	p_prop.property.hint_string = hint_string;
}

void ClassReader::ann_range(const Annotation &p_annotation, GDClassProperty &p_prop) {
	if (p_prop.property.type == GDEXTENSION_VARIANT_TYPE_FLOAT) {
		handle_range_internal<float>(p_annotation, p_prop, *this);
	} else if (p_prop.property.type == GDEXTENSION_VARIANT_TYPE_INT) {
		handle_range_internal<int>(p_annotation, p_prop, *this);
	} else {
		error("@range requires a property of type integer or float", p_annotation.location);
	}
}

void ClassReader::ann_hint_list(const Annotation &p_annotation, const char *p_name, PropertyHint p_hint, GDClassProperty &p_prop) {
	if (p_annotation.args.is_empty()) {
		error(String(p_name) + " requires at least one argument", p_annotation.location);
		return;
	}

	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();

	p_prop.property.hint = p_hint;
	p_prop.property.hint_string = read_hint_list(ptr);
}

void ClassReader::ann_file(const Annotation &p_annotation, GDClassProperty &p_prop) {
	if (p_prop.property.type != GDEXTENSION_VARIANT_TYPE_STRING) {
		error("@file requires a property of type string", p_annotation.location);
		return;
	}

	if (p_annotation.args.is_empty()) {
		p_prop.property.hint = PROPERTY_HINT_FILE;
		p_prop.property.hint_string = String();
		return;
	}

	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();

	bool global = false;
	String hint_string;

	while (*ptr) {
		String arg = read_until_whitespace(ptr);

		if (arg == "global") {
			global = true;
		} else if (arg.begins_with("*")) {
			hint_string = hint_string + "," + arg;
		} else {
			error("Arguments to @file should either be 'global' or an extension in the format *.ext", p_annotation.location);
			return;
		}
	}

	p_prop.property.hint = global ? PROPERTY_HINT_GLOBAL_FILE : PROPERTY_HINT_FILE;
	p_prop.property.hint_string = hint_string;
}

void ClassReader::ann_dir(const Annotation &p_annotation, GDClassProperty &p_prop) {
	if (p_prop.property.type != GDEXTENSION_VARIANT_TYPE_STRING) {
		error("@dir requires a property of type string", p_annotation.location);
		return;
	}

	if (p_annotation.args.is_empty()) {
		p_prop.property.hint = PROPERTY_HINT_DIR;
		p_prop.property.hint_string = String();
	} else if (p_annotation.args.strip_edges() == "global") {
		p_prop.property.hint = PROPERTY_HINT_GLOBAL_DIR;
		p_prop.property.hint_string = String();
	} else {
		error("Only acceptable argument to @dir is 'global'", p_annotation.location);
	}
}

void ClassReader::ann_multiline(const Annotation &p_annotation, GDClassProperty &p_prop) {
	if (p_prop.property.type != GDEXTENSION_VARIANT_TYPE_STRING) {
		error("@multiline requires a property of type string", p_annotation.location);
		return;
	}

	p_prop.property.hint = PROPERTY_HINT_MULTILINE_TEXT;
	p_prop.property.hint_string = String();
}

void ClassReader::ann_placeholder_text(const Annotation &p_annotation, GDClassProperty &p_prop) {
	if (p_prop.property.type != GDEXTENSION_VARIANT_TYPE_STRING) {
		error("@placeholderText requires a property of type string", p_annotation.location);
		return;
	}

	if (p_annotation.args.is_empty()) {
		error("@placeholderText requires an argument for the placeholder text", p_annotation.location);
		return;
	}

	p_prop.property.hint = PROPERTY_HINT_PLACEHOLDER_TEXT;
	p_prop.property.hint_string = p_annotation.args;
}

void ClassReader::ann_exp_easing(const Annotation &p_annotation, GDClassProperty &p_prop) {
	if (p_prop.property.type != GDEXTENSION_VARIANT_TYPE_FLOAT) {
		error("@expEasing requires a property of type float", p_annotation.location);
		return;
	}

	String arg = p_annotation.args.strip_edges();
	String hint_string;

	if (arg == "attenuation") {
		hint_string = "attenuation";
	} else if (arg == "positiveOnly") {
		hint_string = "positive_only";
	} else if (!arg.is_empty()) {
		error("@expEasing expects a value of either 'attenuation' or 'positiveOnly'", p_annotation.location);
		return;
	}

	p_prop.property.hint = PROPERTY_HINT_EXP_EASING;
	p_prop.property.hint_string = hint_string;
}

void ClassReader::ann_no_alpha(const Annotation &p_annotation, GDClassProperty &p_prop) {
	if (p_prop.property.type != GDEXTENSION_VARIANT_TYPE_COLOR) {
		error("@noAlpha requires a property of type Color", p_annotation.location);
		return;
	}

	p_prop.property.hint = PROPERTY_HINT_COLOR_NO_ALPHA;
	p_prop.property.hint_string = String();
}

bool ClassReader::ann_layer_flags(const Annotation &p_annotation, GDClassProperty &p_prop) {
	GDExtensionVariantType type = p_prop.property.type;

	if (p_annotation.name == StringName("flags2DRenderLayers")) {
		if (type != GDEXTENSION_VARIANT_TYPE_INT) {
			error("@flags2DRenderLayers requires a property of type integer", p_annotation.location);
			return true;
		}

		p_prop.property.hint = PROPERTY_HINT_LAYERS_2D_RENDER;
		p_prop.property.hint_string = String();
	} else if (p_annotation.name == StringName("flags2DPhysicsLayers")) {
		if (type != GDEXTENSION_VARIANT_TYPE_INT) {
			error("@flags2DPhysicsLayers requires a property of type integer", p_annotation.location);
			return true;
		}

		p_prop.property.hint = PROPERTY_HINT_LAYERS_2D_PHYSICS;
		p_prop.property.hint_string = String();
	} else if (p_annotation.name == StringName("flags2DNavigationLayers")) {
		if (type != GDEXTENSION_VARIANT_TYPE_INT) {
			error("@flags2DNavigationLayers requires a property of type integer", p_annotation.location);
			return true;
		}

		p_prop.property.hint = PROPERTY_HINT_LAYERS_2D_NAVIGATION;
		p_prop.property.hint_string = String();
	} else if (p_annotation.name == StringName("flags3DRenderLayers")) {
		if (type != GDEXTENSION_VARIANT_TYPE_INT) {
			error("@flags3DRenderLayers requires a property of type integer", p_annotation.location);
			return true;
		}

		p_prop.property.hint = PROPERTY_HINT_LAYERS_3D_RENDER;
		p_prop.property.hint_string = String();
	} else if (p_annotation.name == StringName("flags3DPhysicsLayers")) {
		if (type != GDEXTENSION_VARIANT_TYPE_INT) {
			error("@flags3DPhysicsLayers requires a property of type integer", p_annotation.location);
			return true;
		}

		p_prop.property.hint = PROPERTY_HINT_LAYERS_3D_PHYSICS;
		p_prop.property.hint_string = String();
	} else if (p_annotation.name == StringName("flags3DNavigationLayers")) {
		if (type != GDEXTENSION_VARIANT_TYPE_INT) {
			error("@flags3DNavigationLayers requires a property of type integer", p_annotation.location);
			return true;
		}

		p_prop.property.hint = PROPERTY_HINT_LAYERS_3D_NAVIGATION;
		p_prop.property.hint_string = String();
	} else {
		return false;
	}

	return true;
}

void ClassReader::handle_table_type(Luau::AstTypeTable *p_table) {
#define PROP_SIGNAL_ME_ERR "@property and @signal can only be declared once and are mutually exclusive"

	for (const Luau::AstTableProp &prop : p_table->props) {
		String body;
		Vector<Annotation> annotations;
		parse_comments(prop.location, *comments, body, annotations);

		GDClassProperty *class_prop = nullptr;
		bool signal_found = false;

		for (const Annotation &annotation : annotations) {
			if (annotation.name == StringName("property")) {
				if (class_prop || signal_found) {
					error(PROP_SIGNAL_ME_ERR, annotation.location);
					return;
				}

				if (!annotation.args.is_empty()) {
					error("@property takes no arguments", annotation.location);
				}

				GDClassProperty new_prop;

				if (!type_to_prop(root, script, prop.type, new_prop.property)) {
					error("Property type is invalid; ensure a Godot-compatible type is used", annotation.location);
					return;
				}

				new_prop.property.name = prop.name.value;

				// Godot will not provide a sensible default value by default.
				new_prop.default_value = LuauVariant::default_variant(new_prop.property.type);

				int idx = class_definition.set_prop(prop.name.value, new_prop);
				class_prop = &class_definition.properties.ptrw()[idx];
			} else if (annotation.name == StringName("signal")) {
				if (class_prop || signal_found) {
					error(PROP_SIGNAL_ME_ERR, annotation.location);
					return;
				}

				ann_signal(annotation, prop);
				signal_found = true;
			} else if (annotation.name == StringName("propertyGroup")) {
				ann_property_group(annotation, PROPERTY_USAGE_GROUP);
			} else if (annotation.name == StringName("propertySubgroup")) {
				ann_property_group(annotation, PROPERTY_USAGE_SUBGROUP);
			} else if (annotation.name == StringName("propertyCategory")) {
				ann_property_group(annotation, PROPERTY_USAGE_CATEGORY);
			}
		}

		if (class_prop) {
			GDProperty &property = class_prop->property;
			GDExtensionVariantType type = property.type;

			for (const Annotation &annotation : annotations) {
				if (annotation.name == StringName("default")) {
					ann_default_value(annotation, *class_prop);
				} else if (annotation.name == StringName("set")) {
					ann_setget(annotation, "@set", class_prop->setter);
				} else if (annotation.name == StringName("get")) {
					ann_setget(annotation, "@get", class_prop->getter);
				} else if (annotation.name == StringName("range")) {
					ann_range(annotation, *class_prop);
				} else if (annotation.name == StringName("enum")) {
					if (type != GDEXTENSION_VARIANT_TYPE_INT && type != GDEXTENSION_VARIANT_TYPE_STRING) {
						error("@enum requires a property of type integer or string", annotation.location);
						return;
					}

					ann_hint_list(annotation, "@enum", PROPERTY_HINT_ENUM, *class_prop);
				} else if (annotation.name == StringName("suggestion")) {
					if (type != GDEXTENSION_VARIANT_TYPE_STRING) {
						error("@suggestion requires a property of type string", annotation.location);
						return;
					}

					ann_hint_list(annotation, "@suggestion", PROPERTY_HINT_ENUM_SUGGESTION, *class_prop);
				} else if (annotation.name == StringName("flags")) {
					if (type != GDEXTENSION_VARIANT_TYPE_INT) {
						error("@flags requires a property of type integer", annotation.location);
						return;
					}

					ann_hint_list(annotation, "@flags", PROPERTY_HINT_FLAGS, *class_prop);
				} else if (annotation.name == StringName("file")) {
					ann_file(annotation, *class_prop);
				} else if (annotation.name == StringName("dir")) {
					ann_dir(annotation, *class_prop);
				} else if (annotation.name == StringName("multiline")) {
					ann_multiline(annotation, *class_prop);
				} else if (annotation.name == StringName("placeholderText")) {
					ann_placeholder_text(annotation, *class_prop);
				} else if (annotation.name == StringName("expEasing")) {
					ann_exp_easing(annotation, *class_prop);
				} else if (annotation.name == StringName("noAlpha")) {
					ann_no_alpha(annotation, *class_prop);
				} else if (!ann_layer_flags(annotation, *class_prop) && annotation.name != StringName("property") && annotation.name != StringName("propertyGroup") &&
						annotation.name != StringName("propertySubgroup") &&
						annotation.name != StringName("propertyCategory")) {
					error("@" + annotation.name + " is not valid for properties", annotation.location);
				}
			}
		} else {
			// Nothing additional supported
			for (const Annotation &annotation : annotations) {
				if (annotation.name != StringName("signal") && annotation.name != StringName("propertyGroup") &&
						annotation.name != StringName("propertySubgroup") &&
						annotation.name != StringName("propertyCategory")) {
					error("@" + annotation.name + " is not valid for signals", annotation.location);
				}
			}
		}
	}
}

bool ClassReader::visit(Luau::AstStatTypeAlias *p_type) {
	if (type_found || p_type->name != definition->name)
		return false;

	type_found = true;
	class_type = p_type;

	if (Luau::AstTypeTable *table = p_type->type->as<Luau::AstTypeTable>()) {
		handle_table_type(table);
	} else if (Luau::AstTypeIntersection *intersection = p_type->type->as<Luau::AstTypeIntersection>()) {
		for (Luau::AstType *type : intersection->types) {
			if (Luau::AstTypeTable *table = type->as<Luau::AstTypeTable>()) {
				handle_table_type(table);
			}
		}
	}

	return false;
}
