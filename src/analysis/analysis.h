#pragma once

#include <Luau/Ast.h>
#include <Luau/Location.h>
#include <Luau/ParseResult.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/char_string.hpp>
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

struct LuauScriptAnalysisResult {
	Error error = OK;
	String error_msg;
	int error_line = 1;

	Luau::AstLocal *definition = nullptr;
	Luau::AstStatTypeAlias *class_type = nullptr;
};

LuauScriptAnalysisResult luascript_analyze(LuauScript *p_script, const char *p_src, const Luau::ParseResult &p_parse_result, GDClassDefinition &r_def);
