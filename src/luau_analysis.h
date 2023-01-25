#pragma once

#include "luau_lib.h"
#include <Luau/ParseResult.h>
#include <Luau/Ast.h>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string_name.hpp>

using namespace godot;

struct LuauScriptAnalysisResult {
    Luau::AstLocal *definition = nullptr;
    Luau::AstLocal *impl = nullptr;

    HashMap<StringName, Luau::AstStatFunction *> methods;
    HashMap<StringName, Luau::AstStatTypeAlias *> types;
};

bool luascript_analyze(const Luau::ParseResult &parse_result, LuauScriptAnalysisResult &result);

bool luascript_ast_method(const LuauScriptAnalysisResult &analysis, const StringName &method, GDMethod &ret);
