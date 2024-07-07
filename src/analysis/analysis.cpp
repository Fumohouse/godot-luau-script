#include "analysis/analysis.h"

#include <Luau/Ast.h>
#include <Luau/Location.h>
#include <Luau/ParseResult.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

#include "analysis/analysis_utils.h"
#include "scripting/luau_lib.h"

using namespace godot;

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

ClassReader::ClassReader(Luau::AstStatBlock *p_root, LuauScript *p_script, const Vector<LuauComment> *p_comments, Luau::AstLocal *p_definition) :
		root(p_root), script(p_script), comments(p_comments), definition(p_definition) {}

void ClassReader::error(const String &p_msg, const Luau::Location &p_loc, Error p_error) {
	errors.push_back({ p_error, p_msg, p_loc });
}

LuauScriptAnalysisResult luascript_analyze(LuauScript *p_script, const char *p_src, const Luau::ParseResult &p_parse_result, GDClassDefinition &r_def) {
	LuauScriptAnalysisResult result;
	Vector<LuauComment> comments = extract_comments(p_src, p_parse_result);

	// Scan root return value for definition expression
	result.definition = find_script_definition(p_parse_result.root);
	if (!result.definition) {
		result.errors.push_back({ ERR_COMPILATION_FAILED, "Failed to parse class: Could not find class table" });
		return result;
	}

	// Read comments/annotations and generate class definition
	ClassReader class_reader(p_parse_result.root, p_script, &comments, result.definition);
	p_parse_result.root->visit(&class_reader);

	result.class_type = class_reader.class_type;
	result.errors = class_reader.errors;
	r_def = class_reader.class_definition;

	return result;
}
