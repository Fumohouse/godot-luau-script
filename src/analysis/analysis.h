#pragma once

#include <Luau/Ast.h>
#include <Luau/Location.h>
#include <Luau/ParseResult.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "scripting/luau_lib.h"

using namespace godot;

class LuauScript;

struct LuauComment {
	enum CommentType {
		COMMENT_SINGLE_LINE,
		COMMENT_SINGLE_LINE_EXCL,
		COMMENT_BLOCK,
	};

	CommentType type;
	Luau::Location location;
	String contents;
};

struct Annotation {
	Luau::Location location;
	StringName name;
	String args;
};

struct AnalysisError {
	Error error;
	String message;
	Luau::Location location;
};

struct ClassReader : public Luau::AstVisitor {
	Luau::AstStatBlock *root;
	LuauScript *script;
	const Vector<LuauComment> *comments;
	Luau::AstLocal *definition;

	Luau::AstStatTypeAlias *class_type = nullptr;
	GDClassDefinition class_definition;

	bool definition_found = false;
	bool type_found = false;

	Vector<AnalysisError> errors;

	bool visit(Luau::AstStatLocal *p_node) override;
	bool visit(Luau::AstStatFunction *p_func) override;
	bool visit(Luau::AstStatTypeAlias *p_type) override;
	bool visit(Luau::AstStatAssign *p_assign) override;

	void error(const String &p_msg, const Luau::Location &p_loc, Error p_error = ERR_COMPILATION_FAILED);

	ClassReader(Luau::AstStatBlock *p_root, LuauScript *p_script, const Vector<LuauComment> *p_comments, Luau::AstLocal *p_definition);

private:
	void handle_definition(Luau::AstStatLocal *p_node);
	void ann_tool(const Annotation &p_annotation);
	void ann_extends(const Annotation &p_annotation);
	void ann_permissions(const Annotation &p_annotation);
	void ann_icon_path(const Annotation &p_annotation);

	void handle_method(Luau::AstStatFunction *p_func, const String &p_name);
	void ann_param(const Annotation &p_annotation, GDMethod &p_method);
	void ann_default_args(const Annotation &p_annotation, GDMethod &p_method);
	void ann_return(const Annotation &p_annotation, GDMethod &p_method);
	void ann_method_flags(const Annotation &p_annotation, GDMethod &p_method);
	void ann_rpc(const Annotation &p_annotation, GDMethod &p_method);

	void handle_table_type(Luau::AstTypeTable *p_table);
	void ann_signal(const Annotation &p_annotation, const Luau::AstTableProp &p_prop);
	void ann_property_group(const Annotation &p_annotation, PropertyUsageFlags p_usage);
	void ann_default_value(const Annotation &p_annotation, GDClassProperty &p_prop);
	void ann_setget(const Annotation &p_annotation, const char *p_name, StringName &r_out);
	void ann_range(const Annotation &p_annotation, GDClassProperty &p_prop);
	void ann_hint_list(const Annotation &p_annotation, const char *p_name, PropertyHint p_hint, GDClassProperty &p_prop);
	void ann_file(const Annotation &p_annotation, GDClassProperty &p_prop);
	void ann_dir(const Annotation &p_annotation, GDClassProperty &p_prop);
	void ann_multiline(const Annotation &p_annotation, GDClassProperty &p_prop);
	void ann_placeholder_text(const Annotation &p_annotation, GDClassProperty &p_prop);
	void ann_exp_easing(const Annotation &p_annotation, GDClassProperty &p_prop);
	void ann_no_alpha(const Annotation &p_annotation, GDClassProperty &p_prop);
	bool ann_layer_flags(const Annotation &p_annotation, GDClassProperty &p_prop);

	void handle_assign(Luau::AstStatAssign *p_assign, const String &p_name);
};

struct LuauScriptAnalysisResult {
	Vector<AnalysisError> errors;

	Luau::AstLocal *definition = nullptr;
	Luau::AstStatTypeAlias *class_type = nullptr;
};

LuauScriptAnalysisResult luascript_analyze(LuauScript *p_script, const char *p_src, const Luau::ParseResult &p_parse_result, GDClassDefinition &r_def);
