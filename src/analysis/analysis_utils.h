#pragma once

#include <Luau/Ast.h>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

#include "analysis/analysis.h"

using namespace godot;

struct GDProperty;

Vector<LuauComment> extract_comments(const char *p_src, const Luau::ParseResult &p_parse_result);
void parse_comments(Luau::Location p_location, const Vector<LuauComment> &p_comments, String &r_body, Vector<Annotation> &r_annotations);

bool read_flags(const char *&ptr, const HashMap<String, int> &p_map, int &flags, String &r_error);
String read_hint_list(const char *&ptr);

bool expr_indexes_local(Luau::AstExpr *p_expr, Luau::AstLocal *p_local, String &r_index);
bool type_to_prop(Luau::AstStatBlock *p_root, LuauScript *p_script, Luau::AstType *p_type, GDProperty &r_prop, bool p_nullable_is_variant = true);

struct LocalDefinitionFinder : public Luau::AstVisitor {
	Luau::AstLocal *local;

	Luau::AstExpr *definition = nullptr;

	bool visit(Luau::AstStatLocal *p_node) override;

	LocalDefinitionFinder(Luau::AstLocal *p_local);
};

struct RequireFinder : public Luau::AstVisitor {
	const char *local_name;
	int line_stop;

	const char *require_path = nullptr;

	bool depth_check = false;

	bool visit(Luau::AstStatBlock *p_block) override;
	bool visit(Luau::AstStatLocal *p_node) override;

	RequireFinder(const char *p_local_name, int p_line_stop);
};

struct ReturnFinder : public Luau::AstVisitor {
	bool return_found = false;

	bool visit(Luau::AstStatReturn *p_return) override;
};
