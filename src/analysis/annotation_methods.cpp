#include "analysis/analysis.h"

#include <Luau/Ast.h>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "analysis/analysis_utils.h"
#include "scripting/luau_lib.h"
#include "utils/parsing.h"

using namespace godot;

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

void ClassReader::ann_param(const Annotation &p_annotation, GDMethod &p_method) {
	// TODO: Mostly placeholder for now. Use info here for docs

	if (p_annotation.args.is_empty()) {
		error("@param requires at least one argument for the parameter name", p_annotation.location);
		return;
	}

	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();

	String arg_name = read_until_whitespace(ptr);
	String comment = read_until_end(ptr);
}

void ClassReader::ann_default_args(const Annotation &p_annotation, GDMethod &p_method) {
	if (p_annotation.args.is_empty()) {
		error("@defaultArgs requires an Array of default values", p_annotation.location);
		return;
	}

	Variant val = UtilityFunctions::str_to_var(p_annotation.args);

	if (val == Variant() || val.get_type() != Variant::ARRAY) {
		error("Failed to parse argument array; ensure it is compatible with `str_to_var` and of the correct type", p_annotation.location);
		return;
	}

	Array arr = val.operator Array();
	Vector<Variant> default_args;

	for (int i = 0; i < arr.size(); i++) {
		default_args.push_back(arr[i]);
	}

	p_method.default_arguments = default_args;
}

void ClassReader::ann_return(const Annotation &p_annotation, GDMethod &p_method) {
	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();

	String comment = read_until_end(ptr);
}

void ClassReader::ann_method_flags(const Annotation &p_annotation, GDMethod &p_method) {
	// ! SYNC WITH global_constants.hpp
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
		error("@flags requires one or more method flags", p_annotation.location);
		return;
	}

	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();

	int flags = 0; // if user defines, give customization from zero (rather than default)

	String err;
	if (!read_flags(ptr, flags_map, flags, err)) {
		error(err, p_annotation.location);
		return;
	}

	p_method.flags = flags;
}

enum RpcOption {
	RPC_MODE = 1 << 0,
	RPC_TRANSFER_MODE = 1 << 1,
	RPC_CALL_LOCAL = 1 << 2,
	RPC_CHANNEL = 1 << 3
};

void ClassReader::ann_rpc(const Annotation &p_annotation, GDMethod &p_method) {
	// ! SYNC WITH Relevant RPC features
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
				error("RPC option '" + option + "' will override a previously provided option", p_annotation.location);
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
				error("RPC option '" + option + "' will override a previously defined channel", p_annotation.location);
				return;
			}

			rpc.channel = option.to_int();
			defined |= RPC_CHANNEL;
		} else {
			error("Invalid RPC option '" + option + "'", p_annotation.location);
		}
	}

	class_definition.rpcs.insert(p_method.name, rpc);
}

void ClassReader::handle_method(Luau::AstStatFunction *p_func, const String &p_name) {
	String body;
	Vector<Annotation> annotations;
	parse_comments(p_func->location, *comments, body, annotations);

	bool method_found = false;
	GDMethod method;

	for (const Annotation &annotation : annotations) {
		if (annotation.name == StringName("registerMethod")) {
			if (!annotation.args.is_empty()) {
				error("@registerMethod takes no arguments", annotation.location);
			}

			if (!ast_method(root, script, p_func, method)) {
				error("Failed to register method with AST - check that you have met necessary conventions", annotation.location);
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
			ann_param(annotation, method);
		} else if (annotation.name == StringName("defaultArgs")) {
			ann_default_args(annotation, method);
		} else if (annotation.name == StringName("return")) {
			ann_return(annotation, method);
		} else if (annotation.name == StringName("flags")) {
			ann_method_flags(annotation, method);
		} else if (annotation.name == StringName("rpc")) {
			ann_rpc(annotation, method);
		} else if (annotation.name != StringName("registerMethod")) {
			error("@" + annotation.name + " is not valid for methods", annotation.location);
		}
	}

	class_definition.methods.insert(p_name, method);
}

bool ClassReader::visit(Luau::AstStatFunction *p_func) {
	String name;
	if (expr_indexes_local(p_func->name, definition, name))
		handle_method(p_func, name);

	return false;
}
