#include "luau_analysis.h"

#include <Luau/Ast.h>
#include <Luau/Lexer.h>
#include <Luau/Location.h>
#include <Luau/ParseResult.h>
#include <gdextension_interface.h>
#include <string.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "error_strings.h"
#include "luagd_permissions.h"
#include "luagd_variant.h"
#include "luau_cache.h"
#include "luau_lib.h"
#include "luau_script.h"
#include "services/sandbox_service.h"
#include "wrapped_no_binding.h"

using namespace godot;

/* COMMON VISITORS */

struct LocalDefinitionFinder : public Luau::AstVisitor {
	Luau::AstLocal *local;

	Luau::AstExpr *definition = nullptr;

	bool visit(Luau::AstStatLocal *p_node) override {
		Luau::AstLocal *const *vars = p_node->vars.begin();
		Luau::AstExpr *const *values = p_node->values.begin();

		for (int i = 0; i < p_node->vars.size && i < p_node->values.size; i++) {
			if (vars[i] == local) {
				definition = values[i];
			}
		}

		return false;
	}

	LocalDefinitionFinder(Luau::AstLocal *p_local) :
			local(p_local) {}
};

struct RequireFinder : public Luau::AstVisitor {
	const char *local_name;
	int line_stop;

	const char *require_path = nullptr;

	bool depth_check = false;

	bool visit(Luau::AstStatBlock *p_block) override {
		// In 99% of cases it makes most sense to only scan locals defined at top level
		if (depth_check)
			return false;

		depth_check = true;
		return true;
	}

	bool visit(Luau::AstStatLocal *p_node) override {
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

	RequireFinder(const char *p_local_name, int p_line_stop) :
			local_name(p_local_name), line_stop(p_line_stop) {}
};

struct ReturnFinder : public Luau::AstVisitor {
	bool return_found = false;

	bool visit(Luau::AstStatReturn *p_return) override {
		if (p_return->list.size)
			return_found = true;

		return false;
	}
};

/* AST FUNCTIONS */

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

static bool type_to_prop(Luau::AstStatBlock *p_root, LuauScript *p_script, Luau::AstType *p_type, GDProperty &r_prop, bool p_nullable_is_variant = true) {
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

static bool ast_method(Luau::AstStatBlock *p_root, LuauScript *p_script, Luau::AstStatFunction *p_stat_func, GDMethod &r_ret) {
	Luau::AstExprFunction *func = p_stat_func->func;

	if (func->returnAnnotation.has_value()) {
		const Luau::AstArray<Luau::AstType *> &types = func->returnAnnotation->types;
		if (types.size > 1)
			return false;

		GDProperty return_val;

		if (!type_to_prop(p_root, p_script, types.data[0], return_val))
			return false;

		r_ret.return_val = return_val;
	} else {
		ReturnFinder return_finder;
		func->body->visit(&return_finder);

		if (return_finder.return_found)
			r_ret.return_val.set_variant_type();
	}

	if (func->vararg)
		r_ret.flags = r_ret.flags | METHOD_FLAG_VARARG;

	int arg_offset = func->self ? 0 : 1;

	int i = 0;
	r_ret.arguments.resize(func->self ? func->args.size : func->args.size - 1);

	GDProperty *arg_props = r_ret.arguments.ptrw();
	for (Luau::AstLocal *arg : func->args) {
		if (i < arg_offset) {
			i++;
			continue;
		}

		GDProperty arg_prop;

		if (arg->annotation) {
			if (!type_to_prop(p_root, p_script, arg->annotation, arg_prop, false))
				return false;
		} else {
			// Fall back to Variant
			arg_prop.set_variant_type();
		}

		arg_prop.name = arg->name.value;
		arg_props[i - arg_offset] = arg_prop;
		i++;
	}

	return true;
}

/* BASE ANALYSIS */

static Luau::AstLocal *find_script_definition(Luau::AstStatBlock *p_root, Luau::AstStatBlock *p_block = nullptr) {
	if (!p_block)
		p_block = p_root;

	for (Luau::AstStat *stat : p_block->body) {
		if (Luau::AstStatBlock *block = stat->as<Luau::AstStatBlock>()) {
			// Can return from inside a block, for some reason.
			return find_script_definition(p_root, block);
		} else if (Luau::AstStatReturn *ret = stat->as<Luau::AstStatReturn>()) {
			if (ret->list.size == 0)
				return nullptr;

			Luau::AstExpr *expr = *ret->list.begin();
			if (Luau::AstExprLocal *local = expr->as<Luau::AstExprLocal>()) {
				LocalDefinitionFinder def_finder(local->local);
				p_root->visit(&def_finder);

				if (!def_finder.definition)
					return nullptr;

				if (Luau::AstExprCall *call = def_finder.definition->as<Luau::AstExprCall>()) {
					if (Luau::AstExprGlobal *func = call->func->as<Luau::AstExprGlobal>()) {
						if (func->name != "gdclass" || call->args.size == 0)
							return nullptr;

						if (Luau::AstExprLocal *param_local = call->args.data[0]->as<Luau::AstExprLocal>())
							return param_local->local;
					}
				}

				return nullptr;
			}
		}
	}

	return nullptr;
}

static bool is_whitespace(char p_c) {
	return p_c == ' ' || p_c == '\t' || p_c == '\v' || p_c == '\f';
}

static void skip_whitespace(const char *&ptr) {
	while (is_whitespace(*ptr)) {
		ptr++;
	}
}

static String read_until_whitespace(const char *&ptr) {
	String out;

	while (*ptr && !is_whitespace(*ptr)) {
		out += *ptr;
		ptr++;
	}

	skip_whitespace(ptr);
	return out;
}

static String read_until_end(const char *&ptr) {
	String out;

	while (*ptr) {
		out += *ptr;
		ptr++;
	}

	return out;
}

static bool is_comment_prefix(const char *p_ptr) {
	return *p_ptr == '-' && *(p_ptr + 1) == '-';
}

static bool read_flags(const char *&ptr, const HashMap<String, int> &p_map, int &flags, String &r_error) {
	while (*ptr) {
		if (is_comment_prefix(ptr)) {
			return true;
		}

		String flag = read_until_whitespace(ptr);

		HashMap<String, int>::ConstIterator E = p_map.find(flag);
		if (E) {
			flags |= E->value;
		} else {
			r_error = INVALID_FLAG_ERR(flag);
			return false;
		}
	}

	return true;
}

static bool read_one_word_only(const String &p_str, String &r_out) {
	CharString str = p_str.utf8();
	const char *ptr = str.get_data();

	skip_whitespace(ptr);

	String val = read_until_whitespace(ptr);

	if (*ptr)
		return false;

	r_out = val;
	return true;
}

template <typename T>
static bool to_number(const String &p_str, T &r_out);

template <>
bool to_number<int>(const String &p_str, int &r_out) {
	if (!p_str.is_valid_int())
		return false;

	r_out = p_str.to_int();
	return true;
}

template <>
bool to_number<float>(const String &p_str, float &r_out) {
	if (!p_str.is_valid_float())
		return false;

	r_out = p_str.to_float();
	return true;
}

template <typename T>
static bool read_number(const char *&ptr, T &r_out) {
	String str = read_until_whitespace(ptr);
	return to_number<T>(str, r_out);
}

static String read_hint_list(const char *&ptr) {
	String hint_string = read_until_whitespace(ptr);

	while (*ptr) {
		hint_string = hint_string + "," + read_until_whitespace(ptr);
	}

	return hint_string;
}

static void parse_comments(Luau::Location p_location, const Vector<LuauComment> &p_comments, String &r_body, Vector<Annotation> &r_annotations) {
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

	return;
}

static bool expr_indexes_local(Luau::AstExpr *p_expr, Luau::AstLocal *p_local, String &r_index) {
	if (Luau::AstExprIndexName *index = p_expr->as<Luau::AstExprIndexName>()) {
		if (Luau::AstExprLocal *local = index->expr->as<Luau::AstExprLocal>()) {
			r_index = index->index.value;
			return local->local == p_local;
		}
	}

	return false;
}

struct ClassReader : public Luau::AstVisitor {
	Luau::AstStatBlock *root;
	LuauScript *script;
	const Vector<LuauComment> *comments;
	Luau::AstLocal *definition;

	Luau::AstStatTypeAlias *class_type = nullptr;
	GDClassDefinition class_definition;

	bool definition_found = false;
	bool type_found = false;

	Error error = OK;
	String error_msg;
	int error_line = 1;

	void _error(const String &p_msg, const Luau::Location &loc, Error p_error = ERR_COMPILATION_FAILED) {
		error = p_error;
		error_msg = CLASS_PARSE_ERR(p_msg);
		error_line = loc.begin.line + 1;
	}

	bool try_add_dependency(const Ref<LuauScript> &p_script, const Annotation &p_annotation) {
		if (p_script->is_module()) {
			_error(DEP_ADD_FAILED_ERR(p_script->get_path()), p_annotation.location);
			return false;
		}

		if (!script->add_dependency(p_script)) {
			_error(CYCLIC_SCRIPT_REF_ERR(p_script->get_path(), script->get_path()), p_annotation.location, ERR_CYCLIC_LINK);
			return false;
		}

		return true;
	}

	/* ANNOTATION HANDLERS */

	void handle_extends(const Annotation &p_annotation) {
		CharString args = p_annotation.args.utf8();
		const char *ptr = args.get_data();

		String extends = read_until_whitespace(ptr);

		if (*ptr) {
			_error(EXTENDS_ARG_ERR, p_annotation.location);
			return;
		}

		if (nb::ClassDB::get_singleton_nb()->class_exists(extends)) {
			class_definition.extends = extends;
		} else {
			class_definition.extends = "";

			CharString extends_utf8 = extends.utf8();
			RequireFinder require_finder(extends_utf8.get_data(), p_annotation.location.begin.line);
			root->visit(&require_finder);

			if (require_finder.require_path) {
				String path_err;
				String script_path = script->resolve_path(require_finder.require_path + String(".lua"), path_err);
				if (!path_err.is_empty()) {
					_error(path_err, p_annotation.location);
					return;
				}

				Error err = OK;
				Ref<LuauScript> base_script = LuauCache::get_singleton()->get_script(script_path, err, false, LuauScript::LOAD_ANALYZE);

				if (err != OK) {
					_error(EXTENDS_LOAD_ERR(script_path), p_annotation.location, err);
					return;
				}

				if (!try_add_dependency(base_script, p_annotation))
					return;

				class_definition.base_script = base_script.ptr();
			} else {
				_error(EXTENDS_NOT_FOUND_ERR(extends), p_annotation.location);
			}
		}
	}

	void handle_permissions(const Annotation &p_annotation) {
		static HashMap<String, int> permissions_map;
		static bool did_init = false;

		if (!did_init) {
			permissions_map.insert("BASE", PERMISSION_BASE);
			permissions_map.insert("INTERNAL", PERMISSION_INTERNAL);
			permissions_map.insert("OS", PERMISSION_OS);
			permissions_map.insert("FILE", PERMISSION_FILE);
			permissions_map.insert("HTTP", PERMISSION_HTTP);

			did_init = true;
		}

		if (p_annotation.args.is_empty()) {
			_error(PERMISSIONS_ARG_ERR, p_annotation.location);
			return;
		}

		String path = script->get_path();
		if (path.is_empty() || (SandboxService::get_singleton() && !SandboxService::get_singleton()->is_core_script(path))) {
			_error(PERMISSIONS_NON_CORE_ERR, p_annotation.location);
			return;
		}

		CharString args = p_annotation.args.utf8();
		const char *ptr = args.get_data();

		int permissions = PERMISSION_BASE;

		String err;
		if (!read_flags(ptr, permissions_map, permissions, err)) {
			_error(err, p_annotation.location);
			return;
		}

		class_definition.permissions = static_cast<ThreadPermissions>(permissions);
	}

	void handle_param(const Annotation &p_annotation) {
		// TODO: Mostly placeholder for now. Use info here for docs

		if (p_annotation.args.is_empty()) {
			_error(PARAM_ARG_ERR, p_annotation.location);
			return;
		}

		CharString args = p_annotation.args.utf8();
		const char *ptr = args.get_data();

		String arg_name = read_until_whitespace(ptr);
		String comment = read_until_end(ptr);
	}

	void handle_method_flags(const Annotation &p_annotation, GDMethod &r_method) {
		// ! must update with global_constants.hpp
		static HashMap<String, int> flags_map;
		static bool did_init = false;

		if (!did_init) {
			flags_map.insert("NORMAL", METHOD_FLAG_NORMAL);
			flags_map.insert("EDITOR", METHOD_FLAG_EDITOR);
			flags_map.insert("CONST", METHOD_FLAG_CONST);
			flags_map.insert("VIRTUAL", METHOD_FLAG_VIRTUAL);
			flags_map.insert("VARARG", METHOD_FLAG_VARARG);
			flags_map.insert("STATIC", METHOD_FLAG_STATIC);

			did_init = true;
		}

		if (p_annotation.args.is_empty()) {
			_error(FLAGS_ARG_ERR, p_annotation.location);
			return;
		}

		CharString args = p_annotation.args.utf8();
		const char *ptr = args.get_data();

		int flags = 0; // if user defines, give customization from zero (rather than default)

		String err;
		if (!read_flags(ptr, flags_map, flags, err)) {
			_error(err, p_annotation.location);
			return;
		}

		r_method.flags = flags;

		return;
	}

	enum RpcOption {
		RPC_MODE = 1 << 0,
		RPC_TRANSFER_MODE = 1 << 1,
		RPC_CALL_LOCAL = 1 << 2,
		RPC_CHANNEL = 1 << 3
	};

	void handle_rpc(const Annotation &p_annotation, const GDMethod &p_method) {
		// ! must update with relevant features
		static HashMap<String, Pair<RpcOption, int>> options_map;
		static bool did_init = false;

		if (!did_init) {
			options_map.insert("anyPeer", { RPC_MODE, MultiplayerAPI::RPC_MODE_ANY_PEER });
			options_map.insert("authority", { RPC_MODE, MultiplayerAPI::RPC_MODE_AUTHORITY });

			options_map.insert("unreliable", { RPC_TRANSFER_MODE, MultiplayerPeer::TRANSFER_MODE_UNRELIABLE });
			options_map.insert("unreliableOrdered", { RPC_TRANSFER_MODE, MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED });
			options_map.insert("reliable", { RPC_TRANSFER_MODE, MultiplayerPeer::TRANSFER_MODE_RELIABLE });

			options_map.insert("callLocal", { RPC_CALL_LOCAL, 1 });

			did_init = true;
		}

		CharString args = p_annotation.args.utf8();
		const char *ptr = args.get_data();

		GDRpc rpc;
		rpc.name = p_method.name;

		int defined = 0;

		while (*ptr) {
			String option = read_until_whitespace(ptr);

			HashMap<String, Pair<RpcOption, int>>::ConstIterator E = options_map.find(option);

			if (E) {
				if (defined & E->value.first) {
					_error(RPC_OVERRIDE_ERR(option), p_annotation.location);
					return;
				}

				switch (E->value.first) {
					case RPC_MODE:
						rpc.rpc_mode = static_cast<MultiplayerAPI::RPCMode>(E->value.second);
						break;

					case RPC_TRANSFER_MODE:
						rpc.transfer_mode = static_cast<MultiplayerPeer::TransferMode>(E->value.second);
						break;

					case RPC_CALL_LOCAL:
						rpc.call_local = E->value.second;
						break;

					default:
						// should be impossible
						return;
				}

				defined |= E->value.first;
			} else if (option.is_valid_int()) {
				if (defined & RPC_CHANNEL) {
					_error(RPC_OVERRIDE_ERR(option), p_annotation.location);
					return;
				}

				rpc.channel = option.to_int();
				defined |= RPC_CHANNEL;
			} else {
				_error(RPC_ARG_ERR(option), p_annotation.location);
				return;
			}
		}

		class_definition.rpcs.insert(p_method.name, rpc);
	}

	void handle_signal(const Annotation &p_annotation, const Luau::AstTableProp &p_prop) {
		if (!p_annotation.args.is_empty()) {
			_error(ANN_NO_ARGS_ERR("@signal"), p_annotation.location);
			return;
		}

		if (Luau::AstTypeReference *type = p_prop.type->as<Luau::AstTypeReference>()) {
			if (type->name != "Signal" && type->name != "SignalWithArgs") {
				_error(PROPERTY_TYPE_ERR("@signal", "Signal"), p_annotation.location);
				return;
			}

			GDMethod signal;
			signal.name = p_prop.name.value;

			// CONDITION HELL
			if (type->hasParameterList) {
				if (type->parameters.size != 1) {
					_error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
					return;
				}

				Luau::AstTypeOrPack *type_or_pack = &type->parameters.data[0];
				if (!type_or_pack->type) {
					_error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
					return;
				}

				if (Luau::AstTypeFunction *func_type = type_or_pack->type->as<Luau::AstTypeFunction>()) {
					if (func_type->returnTypes.types.size || func_type->returnTypes.tailType ||
							func_type->generics.size || func_type->genericPacks.size) {
						_error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
						return;
					}

					const Luau::AstArray<Luau::AstType *> &arg_types = func_type->argTypes.types;
					signal.arguments.resize(arg_types.size);
					GDProperty *args = signal.arguments.ptrw();

					for (int i = 0; i < arg_types.size; i++) {
						if (!type_to_prop(root, script, arg_types.data[i], args[i])) {
							_error(SIGNAL_PARAM_INVALID_TYPE_ERR, p_annotation.location);
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
								_error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
								return;
							}
						} else {
							_error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
							return;
						}
					}
				} else {
					_error(SIGNAL_TYPE_PARAM_ERR, p_annotation.location);
					return;
				}
			}

			class_definition.signals.insert(p_prop.name.value, signal);
		} else {
			_error(SIGNAL_TYPE_ERR, p_annotation.location);
		}
	}

	void handle_setget(const Annotation &p_annotation, const char *p_name, StringName &r_out) {
		if (p_annotation.args.is_empty()) {
			_error(SETGET_ARG_ERR(String(p_name)), p_annotation.location);
			return;
		}

		String value;

		if (!read_one_word_only(p_annotation.args, value)) {
			_error(SETGET_WHITESPACE_ERR(String(p_name)), p_annotation.location);
			return;
		}

		r_out = value;
	}

	template <typename T>
	void handle_range_internal(const Annotation &p_annotation, GDProperty &property) {
		if (p_annotation.args.is_empty()) {
			_error(RANGE_ARG_ERR, p_annotation.location);
			return;
		}

		CharString args = p_annotation.args.utf8();
		const char *ptr = args.get_data();

		T min;

		if (!read_number<T>(ptr, min)) {
			_error(RANGE_ARG_TYPE_ERR, p_annotation.location);
			return;
		}

		if (!*ptr) {
			_error(RANGE_ARG_ERR, p_annotation.location);
			return;
		}

		T max;

		if (!read_number<T>(ptr, max)) {
			_error(RANGE_ARG_TYPE_ERR, p_annotation.location);
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
				_error(RANGE_SPECIAL_OPT_ERR, p_annotation.location);
				return;
			}
		}

		property.hint = PROPERTY_HINT_RANGE;
		property.hint_string = hint_string;
	}

	void handle_range(const Annotation &p_annotation, GDProperty &property) {
		if (property.type == GDEXTENSION_VARIANT_TYPE_FLOAT) {
			handle_range_internal<float>(p_annotation, property);
		} else if (property.type == GDEXTENSION_VARIANT_TYPE_INT) {
			handle_range_internal<int>(p_annotation, property);
		} else {
			_error(PROPERTY_TYPE_ERR("@range", "integer or float"), p_annotation.location);
		}
	}

	void handle_pure_hint_list(const Annotation &p_annotation, const char *p_name, PropertyHint p_hint, GDProperty &r_property) {
		if (p_annotation.args.is_empty()) {
			_error(ANN_AT_LEAST_ONE_ARG_ERR(String(p_name)), p_annotation.location);
			return;
		}

		CharString args = p_annotation.args.utf8();
		const char *ptr = args.get_data();

		r_property.hint = p_hint;
		r_property.hint_string = read_hint_list(ptr);
	}

	/* AST HANDLERS */

#define PARSE_COMMENTS(m_loc)       \
	String body;                    \
	Vector<Annotation> annotations; \
	parse_comments(m_loc, *comments, body, annotations);

	void handle_definition(Luau::AstStatLocal *p_node) {
		PARSE_COMMENTS(p_node->location)

		bool class_found = false;

		for (const Annotation &annotation : annotations) {
			if (annotation.name == StringName("class")) {
				if (!annotation.args.is_empty()) {
					CharString args = annotation.args.utf8();
					const char *ptr = args.get_data();

					String class_name = read_until_whitespace(ptr);

					if (*ptr) {
						_error(CLASS_ARG_ERR, annotation.location);
						return;
					}

					class_definition.name = class_name;
				}

				class_found = true;
				break;
			}
		}

		if (!class_found)
			return;

		for (const Annotation &annotation : annotations) {
			if (annotation.name == StringName("tool")) {
				if (!annotation.args.is_empty()) {
					_error(ANN_NO_ARGS_ERR("@tool"), annotation.location);
					return;
				}

				class_definition.is_tool = true;
			} else if (annotation.name == StringName("extends")) {
				handle_extends(annotation);
			} else if (annotation.name == StringName("permissions")) {
				handle_permissions(annotation);
			} else if (annotation.name == StringName("iconPath")) {
				if (annotation.args.is_empty() || !FileAccess::file_exists(annotation.args)) {
					_error(ICONPATH_PATH_ERR, annotation.location);
					return;
				}

				class_definition.icon_path = annotation.args;
			} else if (annotation.name != StringName("class")) {
				_error(INVALID_ANNOTATION_ERR(annotation.name), annotation.location);
				return;
			}

			if (error != OK)
				return;
		}
	}

	void handle_method(Luau::AstStatFunction *p_func, const String &p_name) {
		PARSE_COMMENTS(p_func->location)

		bool method_found = false;
		GDMethod method;

		for (const Annotation &annotation : annotations) {
			if (annotation.name == StringName("registerMethod")) {
				if (!annotation.args.is_empty()) {
					_error(ANN_NO_ARGS_ERR("@registerMethod"), annotation.location);
					return;
				}

				if (!ast_method(root, script, p_func, method)) {
					_error(AST_FAILED_ERR, annotation.location);
					return;
				}

				method_found = true;
				break;
			}
		}

		if (!method_found)
			return;

		method.name = p_name;

		for (const Annotation &annotation : annotations) {
			if (annotation.name == StringName("param")) {
				handle_param(annotation);
			} else if (annotation.name == StringName("defaultArgs")) {
				if (annotation.args.is_empty()) {
					_error(DEFAULTARGS_ARG_ERR, annotation.location);
					return;
				}

				Variant val = UtilityFunctions::str_to_var(annotation.args);

				if (val == Variant() || val.get_type() != Variant::ARRAY) {
					_error(DEFAULTARGS_ARG_PARSE_ERR, annotation.location);
					return;
				}

				Array arr = val.operator Array();
				Vector<Variant> default_args;

				for (int i = 0; i < arr.size(); i++) {
					default_args.push_back(arr[i]);
				}

				method.default_arguments = default_args;
			} else if (annotation.name == StringName("return")) {
				CharString args = annotation.args.utf8();
				const char *ptr = args.get_data();

				String comment = read_until_end(ptr);
			} else if (annotation.name == StringName("flags")) {
				handle_method_flags(annotation, method);
			} else if (annotation.name == StringName("rpc")) {
				handle_rpc(annotation, method);
			} else if (annotation.name != StringName("registerMethod")) {
				_error(INVALID_ANNOTATION_ERR(annotation.name), annotation.location);
				return;
			}

			if (error != OK)
				return;
		}

		class_definition.methods.insert(p_name, method);
	}

	void handle_table_type(Luau::AstTypeTable *p_table) {
		for (const Luau::AstTableProp &prop : p_table->props) {
			PARSE_COMMENTS(prop.location)

			GDClassProperty *class_prop = nullptr;
			bool signal_found = false;

			for (const Annotation &annotation : annotations) {
#define CHECK_GROUPS (annotation.name != StringName("propertyGroup") && \
		annotation.name != StringName("propertySubgroup") &&            \
		annotation.name != StringName("propertyCategory"))

#define PROPERTY_HELPER(m_annotation, m_prop_usage) \
	GDClassProperty prop;                           \
	prop.property.name = annotation.args;           \
	prop.property.usage = m_prop_usage;             \
                                                    \
	class_definition.set_prop(annotation.args, prop);

				if (annotation.name == StringName("property")) {
					if (class_prop || signal_found) {
						_error(PROP_SIGNAL_ME_ERR, annotation.location);
						return;
					}

					if (!annotation.args.is_empty()) {
						_error(ANN_NO_ARGS_ERR("@property"), annotation.location);
						return;
					}

					GDClassProperty new_prop;

					if (!type_to_prop(root, script, prop.type, new_prop.property)) {
						_error(PROP_INVALID_TYPE_ERR, annotation.location);
						return;
					}

					new_prop.property.name = prop.name.value;

					// Godot will not provide a sensible default value by default.
					new_prop.default_value = LuauVariant::default_variant(new_prop.property.type);

					int idx = class_definition.set_prop(prop.name.value, new_prop);
					class_prop = &class_definition.properties.ptrw()[idx];
				} else if (annotation.name == StringName("signal")) {
					if (class_prop || signal_found) {
						_error(PROP_SIGNAL_ME_ERR, annotation.location);
						return;
					}

					handle_signal(annotation, prop);
					signal_found = true;
				} else if (annotation.name == StringName("propertyGroup")) {
					PROPERTY_HELPER("@propertyGroup", PROPERTY_USAGE_GROUP)
				} else if (annotation.name == StringName("propertySubgroup")) {
					PROPERTY_HELPER("@propertySubgroup", PROPERTY_USAGE_SUBGROUP)
				} else if (annotation.name == StringName("propertyCategory")) {
					PROPERTY_HELPER("@propertyCategory", PROPERTY_USAGE_CATEGORY)
				}
			}

			if (error != OK)
				return;

			if (class_prop) {
				GDProperty &property = class_prop->property;
				GDExtensionVariantType type = property.type;

				for (const Annotation &annotation : annotations) {
					if (annotation.name == StringName("default")) {
						if (annotation.args.is_empty()) {
							_error(DEFAULT_ARG_ERR, annotation.location);
							return;
						}

						Variant value = UtilityFunctions::str_to_var(annotation.args);

						// Unideal error detection but probably the best that can be done
						if (value == Variant() && annotation.args.strip_edges() != "null") {
							_error(DEFAULT_ARG_PARSE_ERR, annotation.location);
							return;
						}

						if (value.get_type() != Variant::Type(property.type) &&
								property.usage != PROPERTY_USAGE_NIL_IS_VARIANT) {
							_error(DEFAULT_ARG_TYPE_ERR(
										   Variant::get_type_name(Variant::Type(property.type)),
										   Variant::get_type_name(value.get_type())),
									annotation.location);

							return;
						}

						class_prop->default_value = value;
					} else if (annotation.name == StringName("set")) {
						handle_setget(annotation, "@set", class_prop->setter);
					} else if (annotation.name == StringName("get")) {
						handle_setget(annotation, "@get", class_prop->getter);
					} else if (annotation.name == StringName("range")) {
						handle_range(annotation, property);
					} else if (annotation.name == StringName("enum")) {
						if (type != GDEXTENSION_VARIANT_TYPE_INT && type != GDEXTENSION_VARIANT_TYPE_STRING) {
							_error(PROPERTY_TYPE_ERR("@enum", "integer or string"), annotation.location);
							return;
						}

						handle_pure_hint_list(annotation, "@enum", PROPERTY_HINT_ENUM, property);
					} else if (annotation.name == StringName("suggestion")) {
						if (type != GDEXTENSION_VARIANT_TYPE_STRING) {
							_error(PROPERTY_TYPE_ERR("@suggestion", "string"), annotation.location);
							return;
						}

						handle_pure_hint_list(annotation, "@suggestion", PROPERTY_HINT_ENUM_SUGGESTION, property);
					} else if (annotation.name == StringName("flags")) {
						if (type != GDEXTENSION_VARIANT_TYPE_INT) {
							_error(PROPERTY_TYPE_ERR("@flags", "integer"), annotation.location);
							return;
						}

						handle_pure_hint_list(annotation, "@flags", PROPERTY_HINT_FLAGS, property);
					} else if (annotation.name == StringName("file")) {
						if (type != GDEXTENSION_VARIANT_TYPE_STRING) {
							_error(PROPERTY_TYPE_ERR("@file", "string"), annotation.location);
							return;
						}

						if (annotation.args.is_empty()) {
							property.hint = PROPERTY_HINT_FILE;
							property.hint_string = String();
							continue;
						}

						CharString args = annotation.args.utf8();
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
								_error(FILE_ARG_ERR, annotation.location);
								return;
							}
						}

						property.hint = global ? PROPERTY_HINT_GLOBAL_FILE : PROPERTY_HINT_FILE;
						property.hint_string = hint_string;
					} else if (annotation.name == StringName("dir")) {
						if (type != GDEXTENSION_VARIANT_TYPE_STRING) {
							_error(PROPERTY_TYPE_ERR("@dir", "string"), annotation.location);
							return;
						}

						if (annotation.args.is_empty()) {
							property.hint = PROPERTY_HINT_DIR;
							property.hint_string = String();
						} else if (annotation.args.strip_edges() == "global") {
							property.hint = PROPERTY_HINT_GLOBAL_DIR;
							property.hint_string = String();
						} else {
							_error(DIR_ARG_ERR, annotation.location);
						}
					} else if (annotation.name == StringName("multiline")) {
						if (type != GDEXTENSION_VARIANT_TYPE_STRING) {
							_error(PROPERTY_TYPE_ERR("@multiline", "string"), annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_MULTILINE_TEXT;
						property.hint_string = String();
					} else if (annotation.name == StringName("placeholderText")) {
						if (type != GDEXTENSION_VARIANT_TYPE_STRING) {
							_error(PROPERTY_TYPE_ERR("@placeholderText", "string"), annotation.location);
							return;
						}

						if (annotation.args.is_empty()) {
							_error(PLACEHOLDER_ARG_ERR, annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_PLACEHOLDER_TEXT;
						property.hint_string = annotation.args;
					} else if (annotation.name == StringName("flags2DRenderLayers")) {
						if (type != GDEXTENSION_VARIANT_TYPE_INT) {
							_error(PROPERTY_TYPE_ERR("@flags2DRenderLayers", "integer"), annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_LAYERS_2D_RENDER;
						property.hint_string = String();
					} else if (annotation.name == StringName("flags2DPhysicsLayers")) {
						if (type != GDEXTENSION_VARIANT_TYPE_INT) {
							_error(PROPERTY_TYPE_ERR("@flags2DPhysicsLayers", "integer"), annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_LAYERS_2D_PHYSICS;
						property.hint_string = String();
					} else if (annotation.name == StringName("flags2DNavigationLayers")) {
						if (type != GDEXTENSION_VARIANT_TYPE_INT) {
							_error(PROPERTY_TYPE_ERR("@flags2DNavigationLayers", "integer"), annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_LAYERS_2D_NAVIGATION;
						property.hint_string = String();
					} else if (annotation.name == StringName("flags3DRenderLayers")) {
						if (type != GDEXTENSION_VARIANT_TYPE_INT) {
							_error(PROPERTY_TYPE_ERR("@flags3DRenderLayers", "integer"), annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_LAYERS_3D_RENDER;
						property.hint_string = String();
					} else if (annotation.name == StringName("flags3DPhysicsLayers")) {
						if (type != GDEXTENSION_VARIANT_TYPE_INT) {
							_error(PROPERTY_TYPE_ERR("@flags3DPhysicsLayers", "integer"), annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_LAYERS_3D_PHYSICS;
						property.hint_string = String();
					} else if (annotation.name == StringName("flags3DNavigationLayers")) {
						if (type != GDEXTENSION_VARIANT_TYPE_INT) {
							_error(PROPERTY_TYPE_ERR("@flags3DNavigationLayers", "integer"), annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_LAYERS_3D_NAVIGATION;
						property.hint_string = String();
					} else if (annotation.name == StringName("expEasing")) {
						if (type != GDEXTENSION_VARIANT_TYPE_FLOAT) {
							_error(PROPERTY_TYPE_ERR("@expEasing", "float"), annotation.location);
							return;
						}

						String arg = annotation.args.strip_edges();
						String hint_string;

						if (arg == "attenuation") {
							hint_string = "attenuation";
						} else if (arg == "positiveOnly") {
							hint_string = "positive_only";
						} else if (!arg.is_empty()) {
							_error(EXPEASING_ARG_ERR, annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_EXP_EASING;
						property.hint_string = hint_string;
					} else if (annotation.name == StringName("noAlpha")) {
						if (type != GDEXTENSION_VARIANT_TYPE_COLOR) {
							_error(PROPERTY_TYPE_ERR("@noAlpha", "Color"), annotation.location);
							return;
						}

						property.hint = PROPERTY_HINT_COLOR_NO_ALPHA;
						property.hint_string = String();
					} else if (annotation.name != StringName("property") && CHECK_GROUPS) {
						_error(INVALID_ANNOTATION_ERR(annotation.name), annotation.location);
						return;
					}

					if (error != OK)
						return;
				}
			} else {
				// Nothing additional supported
				for (const Annotation &annotation : annotations) {
					if (annotation.name != StringName("signal") && CHECK_GROUPS) {
						_error(INVALID_ANNOTATION_ERR(annotation.name), annotation.location);
						return;
					}
				}
			}
		}
	}

	void handle_type(Luau::AstStatTypeAlias *p_type) {
		if (type_found)
			return;

		PARSE_COMMENTS(p_type->location);

		for (const Annotation &annotation : annotations) {
			if (annotation.name == StringName("classType")) {
				if (annotation.args.is_empty()) {
					_error(CLASSTYPE_ARG_ERR, annotation.location);
					return;
				}

				if (annotation.args == definition->name.value) {
					type_found = true;
					break;
				}
			} else {
				_error(INVALID_ANNOTATION_ERR(annotation.name), annotation.location);
				return;
			}
		}

		if (!type_found)
			return;

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
	}

	void handle_assign(Luau::AstStatAssign *p_assign, const String &p_name) {
		PARSE_COMMENTS(p_assign->location)

		for (const Annotation &annotation : annotations) {
			if (annotation.name == StringName("registerConstant")) {
				if (!annotation.args.is_empty()) {
					_error(ANN_NO_ARGS_ERR("@registerConstant"), annotation.location);
					return;
				}

				class_definition.constants.insert(p_name, p_assign->location.begin.line + 1);
			} else {
				_error(INVALID_ANNOTATION_ERR(annotation.name), annotation.location);
				return;
			}
		}
	}

	/* VISIT METHODS */

	bool visit(Luau::AstStatLocal *p_node) override {
		if (definition_found || error != OK)
			return false;

		Luau::AstLocal *const *vars = p_node->vars.begin();

		for (int i = 0; i < p_node->vars.size; i++) {
			if (vars[i] == definition) {
				handle_definition(p_node);
				definition_found = true;
				break;
			}
		}

		return false;
	}

	bool visit(Luau::AstStatFunction *p_func) override {
		if (error != OK)
			return false;

		String name;
		if (expr_indexes_local(p_func->name, definition, name))
			handle_method(p_func, name);

		return false;
	}

	bool visit(Luau::AstStatTypeAlias *p_type) override {
		if (error != OK)
			return false;

		handle_type(p_type);
		return false;
	}

	bool visit(Luau::AstStatAssign *p_assign) override {
		if (error != OK || p_assign->vars.size > 1)
			return false;

		Luau::AstExpr *var = p_assign->vars.data[0];
		String name;
		if (expr_indexes_local(var, definition, name))
			handle_assign(p_assign, name);

		return false;
	}

	ClassReader(Luau::AstStatBlock *p_root, LuauScript *p_script, const Vector<LuauComment> *p_comments, Luau::AstLocal *p_definition) :
			root(p_root), script(p_script), comments(p_comments), definition(p_definition) {}
};

LuauScriptAnalysisResult luascript_analyze(LuauScript *p_script, const char *p_src, const Luau::ParseResult &p_parse_result, GDClassDefinition &r_def) {
	LuauScriptAnalysisResult result;

	// Step 1: Extract comments
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

	// Step 2: Scan root return value for definition expression.
	result.definition = find_script_definition(p_parse_result.root);
	if (!result.definition) {
		result.error = ERR_COMPILATION_FAILED;
		result.error_msg = NO_CLASS_TABLE_ERR;
		return result;
	}

	// Step 3: Read comments/annotations and generate class definition.
	ClassReader class_reader(p_parse_result.root, p_script, &comments, result.definition);
	p_parse_result.root->visit(&class_reader);

	if (class_reader.error != OK) {
		result.error = class_reader.error;
		result.error_msg = class_reader.error_msg;
		result.error_line = class_reader.error_line;
	}

	result.class_type = class_reader.class_type;
	r_def = class_reader.class_definition;

	return result;
}
