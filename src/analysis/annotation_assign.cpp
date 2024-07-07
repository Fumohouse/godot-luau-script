#include "analysis/analysis.h"

#include <Luau/Ast.h>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

#include "analysis/analysis_utils.h"

using namespace godot;

void ClassReader::handle_assign(Luau::AstStatAssign *p_assign, const String &p_name) {
	String body;
	Vector<Annotation> annotations;
	parse_comments(p_assign->location, *comments, body, annotations);

	for (const Annotation &annotation : annotations) {
		if (annotation.name == StringName("registerConstant")) {
			if (!annotation.args.is_empty()) {
				error("@registerConstant takes no arguments", annotation.location);
				return;
			}

			class_definition.constants.insert(p_name, p_assign->location.begin.line + 1);
		} else {
			error("@" + annotation.name + " is not valid for assignments", annotation.location);
		}
	}
}

bool ClassReader::visit(Luau::AstStatAssign *p_assign) {
	if (p_assign->vars.size > 1)
		return false;

	Luau::AstExpr *var = p_assign->vars.data[0];
	String name;
	if (expr_indexes_local(var, definition, name))
		handle_assign(p_assign, name);

	return false;
}
