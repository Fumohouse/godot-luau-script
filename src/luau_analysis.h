#pragma once

#include <Luau/Ast.h>
#include <Luau/Lexer.h>
#include <Luau/Location.h>
#include <Luau/ParseResult.h>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "luau_lib.h"

using namespace godot;

struct LuauComment {
    enum CommentType {
        SINGLE_LINE,
        SINGLE_LINE_EXCL,
        BLOCK,
    };

    CommentType type;
    Luau::Location location;
    String contents;
};

struct LuauScriptAnalysisResult {
    Vector<LuauComment> comments;

    Luau::AstLocal *definition = nullptr;
    Luau::AstLocal *impl = nullptr;

    HashMap<StringName, Luau::AstStatFunction *> methods;
};

bool luascript_analyze(const char *p_src, const Luau::ParseResult &p_parse_result, LuauScriptAnalysisResult &r_result);

bool luascript_ast_method(const LuauScriptAnalysisResult &p_analysis, const StringName &p_method, GDMethod &r_ret);
