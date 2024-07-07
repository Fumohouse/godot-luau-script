#include "analysis/analysis.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>

#include "analysis/analysis_utils.h"
#include "scripting/luau_cache.h"
#include "scripting/luau_script.h"
#include "services/sandbox_service.h"
#include "utils/parsing.h"
#include "utils/wrapped_no_binding.h"

using namespace godot;

void ClassReader::ann_tool(const Annotation &p_annotation) {
	if (!p_annotation.args.is_empty()) {
		error("@tool takes no arguments", p_annotation.location);
	}

	class_definition.is_tool = true;
}

void ClassReader::ann_extends(const Annotation &p_annotation) {
	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();

	String extends = read_until_whitespace(ptr);

	if (*ptr) {
		error("@extends only takes one argument for the Godot base type or the name of the required base class local", p_annotation.location);
	}

	if (nb::ClassDB::get_singleton_nb()->class_exists(extends)) {
		class_definition.extends = extends;
	} else {
		class_definition.extends = "";

		CharString extends_utf8 = extends.utf8();
		RequireFinder require_finder(extends_utf8.get_data(), p_annotation.location.begin.line);
		root->visit(&require_finder);

		if (!require_finder.require_path) {
			error("Could not find base class '" + extends + "'; ensure it is a valid Godot type or a Luau class required and assigned to a local before this annotation", p_annotation.location);
			return;
		}

		String path_err;
		String script_path = script->resolve_path(require_finder.require_path + String(".lua"), path_err);
		if (!path_err.is_empty()) {
			error(path_err, p_annotation.location);
			return;
		}

		Error err = OK;
		Ref<LuauScript> base_script = LuauCache::get_singleton()->get_script(script_path, err, false, LuauScript::LOAD_ANALYZE);

		if (err != OK) {
			error("Failed to load base script at " + script_path, p_annotation.location, err);
			return;
		}

		if (base_script->is_module()) {
			error("Cannot add module at " + base_script->get_path() + " as dependency", p_annotation.location);
			return;
		}

		if (!script->add_dependency(base_script)) {
			error("Cyclic reference detected; cannot add script at " + base_script->get_path() + " as dependency for script at " + script->get_path(), p_annotation.location, ERR_CYCLIC_LINK);
			return;
		}

		class_definition.base_script = base_script.ptr();
	}
}

void ClassReader::ann_permissions(const Annotation &p_annotation) {
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
		error("@permissions requires one or more permission flags", p_annotation.location);
		return;
	}

	String path = script->get_path();
	if (path.is_empty() || (SandboxService::get_singleton() && !SandboxService::get_singleton()->is_core_script(path))) {
		error("!!! Cannot set permissions on a non-core script !!!", p_annotation.location);
		return;
	}

	CharString args = p_annotation.args.utf8();
	const char *ptr = args.get_data();

	int permissions = PERMISSION_BASE;

	String err;
	if (!read_flags(ptr, permissions_map, permissions, err)) {
		error(err, p_annotation.location);
	}

	class_definition.permissions = static_cast<ThreadPermissions>(permissions);
}

void ClassReader::ann_icon_path(const Annotation &p_annotation) {
	if (p_annotation.args.is_empty() || !FileAccess::file_exists(p_annotation.args)) {
		error("@iconPath path is invalid or missing", p_annotation.location);
		return;
	}

	class_definition.icon_path = p_annotation.args;
}

void ClassReader::handle_definition(Luau::AstStatLocal *p_node) {
	String body;
	Vector<Annotation> annotations;
	parse_comments(p_node->location, *comments, body, annotations);

	bool class_found = false;

	for (const Annotation &annotation : annotations) {
		if (annotation.name == StringName("class")) {
			if (!annotation.args.is_empty()) {
				CharString args = annotation.args.utf8();
				const char *ptr = args.get_data();

				String class_name = read_until_whitespace(ptr);

				if (*ptr) {
					error("@class only takes one optional argument for its global name", annotation.location);
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
			ann_tool(annotation);
		} else if (annotation.name == StringName("extends")) {
			ann_extends(annotation);
		} else if (annotation.name == StringName("permissions")) {
			ann_permissions(annotation);
		} else if (annotation.name == StringName("iconPath")) {
			ann_icon_path(annotation);
		} else if (annotation.name != StringName("class")) {
			error("@" + annotation.name + " is not valid for class definitions", annotation.location);
		}
	}
}

bool ClassReader::visit(Luau::AstStatLocal *p_node) {
	if (definition_found)
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
