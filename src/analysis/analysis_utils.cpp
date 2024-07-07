#include "analysis/analysis_utils.h"

#include <Luau/Ast.h>
#include <string.h>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

#include "analysis/analysis.h"
#include "scripting/luau_cache.h"
#include "scripting/luau_script.h"
#include "utils/parsing.h"

using namespace godot;

Vector<LuauComment> extract_comments(const char *p_src, const Luau::ParseResult &p_parse_result) {
	LocalVector<const char *> line_offsets;
	line_offsets.push_back(p_src);
	{
		const char *ptr = p_src;

		while (*ptr) {
			if (*ptr == '\n') {
				line_offsets.push_back(ptr + 1);
			}

			ptr++;
		}
	}

	Vector<LuauComment> comments;

	for (const Luau::Comment &comment : p_parse_result.commentLocations) {
		if (comment.type == Luau::Lexeme::BrokenComment) {
			continue;
		}

		const Luau::Location &loc = comment.location;
		const char *start_line_ptr = line_offsets[loc.begin.line];

		LuauComment parsed_comment;

		if (comment.type == Luau::Lexeme::BlockComment) {
			parsed_comment.type = LuauComment::COMMENT_BLOCK;
		} else {
			// Search from start of line. If there is nothing other than whitespace before the comment, count it as "exclusive".
			const char *ptr = start_line_ptr;

			while (*ptr) {
				if (is_comment_prefix(ptr)) {
					parsed_comment.type = LuauComment::COMMENT_SINGLE_LINE_EXCL;
					break;
				} else if (!is_whitespace(*ptr)) {
					parsed_comment.type = LuauComment::COMMENT_SINGLE_LINE;
					break;
				}

				ptr++;
			}
		}

		parsed_comment.location = loc;

		const char *start_ptr = start_line_ptr + loc.begin.column;
		const char *end_ptr = line_offsets[loc.end.line] + loc.end.column; // not inclusive
		parsed_comment.contents = String::utf8(start_ptr, end_ptr - start_ptr);

		comments.push_back(parsed_comment);
	}

	return comments;
}

void parse_comments(Luau::Location p_location, const Vector<LuauComment> &p_comments, String &r_body, Vector<Annotation> &r_annotations) {
	uint32_t check_line = p_location.begin.line;
	if (check_line == 0)
		return;

	// Comments are processed in reverse
	bool last_line_empty = false;
	for (int i = p_comments.size() - 1; i >= 0; i--) {
		const LuauComment &comment = p_comments[i];
		if (comment.type != LuauComment::COMMENT_SINGLE_LINE_EXCL)
			continue;

		uint32_t comment_line = comment.location.end.line;
		if (comment_line != check_line - 1) {
			// We've gone past the last comment in the chain, exit
			if (comment_line < check_line)
				break;

			// We haven't found a candidate comment yet, continue
			continue;
		}

		CharString contents = comment.contents.utf8();
		const char *ptr = contents.get_data() + 2; // first 2 bytes should always be "--"
		// Require third '-' to minimize ambiguity - similar to LuaLS style
		if (*(ptr++) != '-')
			break;

		skip_whitespace(ptr);

		if (*ptr == 0) {
			last_line_empty = true;
		} else if (*(ptr++) == '@') {
			String annotation_name = read_until_whitespace(ptr);
			String annotation_args = read_until_end(ptr); // left to individual processing

			r_annotations.push_back({ comment.location, annotation_name, annotation_args });

			last_line_empty = false;
		} else {
			String body_line = read_until_end(ptr);

			if (r_body.is_empty()) {
				r_body = body_line;
			} else if (last_line_empty) {
				r_body = body_line + '\n' + r_body;
			} else {
				r_body = body_line + ' ' + r_body;
			}

			last_line_empty = false;
		}

		check_line = comment_line;
	}

	r_annotations.reverse(); // To be in top -> bottom order
}

bool read_flags(const char *&ptr, const HashMap<String, int> &p_map, int &flags, String &r_error) {
	while (*ptr) {
		if (is_comment_prefix(ptr)) {
			return true;
		}

		String flag = read_until_whitespace(ptr);

		HashMap<String, int>::ConstIterator E = p_map.find(flag);
		if (E) {
			flags |= E->value;
		} else {
			r_error = flag + " is not a valid flag";
			return false;
		}
	}

	return true;
}

String read_hint_list(const char *&ptr) {
	String hint_string = read_until_whitespace(ptr);

	while (*ptr) {
		hint_string = hint_string + "," + read_until_whitespace(ptr);
	}

	return hint_string;
}

bool expr_indexes_local(Luau::AstExpr *p_expr, Luau::AstLocal *p_local, String &r_index) {
	if (Luau::AstExprIndexName *index = p_expr->as<Luau::AstExprIndexName>()) {
		if (Luau::AstExprLocal *local = index->expr->as<Luau::AstExprLocal>()) {
			r_index = index->index.value;
			return local->local == p_local;
		}
	}

	return false;
}

static bool get_godot_type(const String &p_type_name, GDProperty &r_prop) {
	static HashMap<String, GDExtensionVariantType> variant_types;
	static bool did_init = false;

	if (!did_init) {
		// Special cases
		variant_types.insert("nil", GDEXTENSION_VARIANT_TYPE_NIL);
		variant_types.insert("boolean", GDEXTENSION_VARIANT_TYPE_BOOL);
		variant_types.insert("integer", GDEXTENSION_VARIANT_TYPE_INT);
		variant_types.insert("number", GDEXTENSION_VARIANT_TYPE_FLOAT);
		variant_types.insert("string", GDEXTENSION_VARIANT_TYPE_STRING);
		variant_types.insert("StringNameN", GDEXTENSION_VARIANT_TYPE_STRING_NAME);
		variant_types.insert("NodePathN", GDEXTENSION_VARIANT_TYPE_NODE_PATH);

		for (int i = GDEXTENSION_VARIANT_TYPE_VECTOR2; i < GDEXTENSION_VARIANT_TYPE_VARIANT_MAX; i++) {
			variant_types.insert(Variant::get_type_name(Variant::Type(i)), GDExtensionVariantType(i));
		}

		did_init = true;
	}

	// Special case
	if (p_type_name == "Variant") {
		r_prop.set_variant_type();
		return true;
	}

	HashMap<String, GDExtensionVariantType>::ConstIterator E = variant_types.find(p_type_name);

	if (E) {
		// Variant type
		r_prop.type = E->value;
	} else if (nb::ClassDB::get_singleton_nb()->class_exists(p_type_name)) {
		r_prop.set_object_type(p_type_name);
	} else {
		return false;
	}

	return true;
}

static bool get_module_type(Luau::AstStatBlock *p_root, LuauScript *p_script, Luau::AstTypeReference *p_type, GDProperty &r_prop) {
	if (!p_type->prefix.has_value())
		return false;

	const char *prefix = p_type->prefix->value;

	RequireFinder require_finder(prefix, p_type->location.end.line);
	p_root->visit(&require_finder);
	if (!require_finder.require_path)
		return false;

	String resolve_err;
	String full_require_path = p_script->resolve_path(require_finder.require_path + String(".lua"), resolve_err);
	if (!resolve_err.is_empty())
		return false;

	Error err = OK;
	Ref<LuauScript> required_script = LuauCache::get_singleton()->get_script(full_require_path, err, LuauScript::LOAD_ANALYZE);
	if (err != OK || required_script->is_module())
		return false;

	if (!p_script->add_dependency(required_script))
		return false;

	if (Luau::AstStatTypeAlias *class_type = required_script->get_luau_data().analysis_result.class_type) {
		if (class_type->name != prefix)
			return false;

		const String &global_name = required_script->get_definition().name;
		StringName instance_type = required_script->_get_instance_base_type();
		if (instance_type.is_empty())
			return false;

		String actual_type = global_name.is_empty() ? String(instance_type) : global_name;
		r_prop.set_object_type(actual_type, instance_type);

		return true;
	}

	return false;
}

static Luau::AstTypeReference *get_suitable_type_ref(Luau::AstType *p_type, bool &r_was_conditional) {
	if (Luau::AstTypeReference *ref = p_type->as<Luau::AstTypeReference>()) {
		r_was_conditional = false;
		return ref;
	}

	// Union with nil -> T?
	if (Luau::AstTypeUnion *uni = p_type->as<Luau::AstTypeUnion>()) {
		if (uni->types.size != 2)
			return nullptr;

		if (Luau::AstTypeReference *ref1 = uni->types.data[0]->as<Luau::AstTypeReference>()) {
			if (Luau::AstTypeReference *ref2 = uni->types.data[1]->as<Luau::AstTypeReference>()) {
				if (ref1->name == "nil") {
					r_was_conditional = true;
					return ref2;
				} else if (ref2->name == "nil") {
					r_was_conditional = true;
					return ref1;
				}
			}
		}
	}

	return nullptr;
}

bool type_to_prop(Luau::AstStatBlock *p_root, LuauScript *p_script, Luau::AstType *p_type, GDProperty &r_prop, bool p_nullable_is_variant) {
	bool was_conditional = false;
	Luau::AstTypeReference *type_ref = get_suitable_type_ref(p_type, was_conditional);
	if (!type_ref)
		return false;

	// For Objects, nil is okay without assuming Variant
	if (type_ref->prefix.has_value()) {
		return get_module_type(p_root, p_script, type_ref, r_prop);
	}

	if (!type_ref->hasParameterList) {
		GDProperty godot_type; // Avoid polluting r_prop if was_conditional
		bool godot_type_valid = get_godot_type(type_ref->name.value, godot_type);

		if (!p_nullable_is_variant || !was_conditional || (godot_type_valid && godot_type.type == GDEXTENSION_VARIANT_TYPE_OBJECT)) {
			r_prop = godot_type;
			return godot_type_valid;
		}
	}

	// Otherwise, assume Variant if desired and nil is an accepted type
	if (p_nullable_is_variant && was_conditional) {
		r_prop.set_variant_type();
		return true;
	}

	if (type_ref->name == "TypedArray") {
		Luau::AstType *param = type_ref->parameters.begin()->type;
		if (!param)
			return false;

		if (Luau::AstTypeReference *param_ref = param->as<Luau::AstTypeReference>()) {
			GDProperty type_info;
			if (!type_to_prop(p_root, p_script, param_ref, type_info))
				return false;

			r_prop.set_typed_array_type(type_info);
			return true;
		}
	} else if (type_ref->name == "NodePathConstrained") {
		String hint_string;

		for (const Luau::AstTypeOrPack &type_or_pack : type_ref->parameters) {
			if (!type_or_pack.type)
				return false;

			if (Luau::AstTypeReference *type = type_or_pack.type->as<Luau::AstTypeReference>()) {
				GDProperty type_info;
				if (!type_to_prop(p_root, p_script, type, type_info) || type_info.hint != PROPERTY_HINT_NODE_TYPE)
					return false;

				if (hint_string.is_empty())
					hint_string = type_info.class_name;
				else
					hint_string = hint_string + "," + type_info.class_name;
			}
		}

		r_prop.type = GDEXTENSION_VARIANT_TYPE_NODE_PATH;
		r_prop.hint = PROPERTY_HINT_NODE_PATH_VALID_TYPES;
		r_prop.hint_string = hint_string;
		return true;
	}

	return false;
}

///////////////////////////
// LocalDefinitionFinder //
///////////////////////////

bool LocalDefinitionFinder::visit(Luau::AstStatLocal *p_node) {
	Luau::AstLocal *const *vars = p_node->vars.begin();
	Luau::AstExpr *const *values = p_node->values.begin();

	for (int i = 0; i < p_node->vars.size && i < p_node->values.size; i++) {
		if (vars[i] == local) {
			definition = values[i];
		}
	}

	return false;
}

LocalDefinitionFinder::LocalDefinitionFinder(Luau::AstLocal *p_local) :
		local(p_local) {}

///////////////////
// RequireFinder //
///////////////////

bool RequireFinder::visit(Luau::AstStatBlock *p_block) {
	// In 99% of cases it makes most sense to only scan locals defined at top level
	if (depth_check)
		return false;

	depth_check = true;
	return true;
}

bool RequireFinder::visit(Luau::AstStatLocal *p_node) {
	if (p_node->location.begin.line > line_stop)
		return false;

	Luau::AstLocal *const *vars = p_node->vars.begin();
	Luau::AstExpr *const *values = p_node->values.begin();

	for (int i = 0; i < p_node->vars.size && i < p_node->values.size; i++) {
		if (strcmp(vars[i]->name.value, local_name) != 0) {
			continue;
		}

		if (Luau::AstExprCall *call = values[i]->as<Luau::AstExprCall>()) {
			if (call->args.size == 0)
				continue;

			if (Luau::AstExprGlobal *func = call->func->as<Luau::AstExprGlobal>()) {
				if (func->name != "require")
					continue;

				if (Luau::AstExprConstantString *str = call->args.data[0]->as<Luau::AstExprConstantString>()) {
					require_path = str->value.data;
				}
			}
		}
	}

	return false;
}

RequireFinder::RequireFinder(const char *p_local_name, int p_line_stop) :
		local_name(p_local_name), line_stop(p_line_stop) {}

//////////////////
// ReturnFinder //
//////////////////

bool ReturnFinder::visit(Luau::AstStatReturn *p_return) {
	if (p_return->list.size)
		return_found = true;

	return false;
}
