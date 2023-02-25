#include <catch_amalgamated.hpp>

#include <gdextension_interface.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "gd_luau.h"
#include "luau_analysis.h"
#include "luau_cache.h"
#include "luau_lib.h"
#include "test_utils.h"
#include "utils.h"

TEST_CASE("luau analysis: base analysis") {
    GDLuau gd_luau;
    LuauCache luau_cache;

    SECTION("idiomatic") {
        LOAD_SCRIPT_FILE(script, "analysis/Idiomatic.lua")

        const LuauScriptAnalysisResult &res = script->get_luau_data().analysis_result;

        REQUIRE(res.definition);
        REQUIRE(res.definition != res.impl);

        REQUIRE(res.methods.has("TestMethod"));
        REQUIRE(res.types.has("TypeAlias"));
    }

    SECTION("odd") {
        LOAD_SCRIPT_FILE(script, "analysis/Odd.lua")

        const LuauScriptAnalysisResult &res = script->get_luau_data().analysis_result;

        REQUIRE(res.definition);
        REQUIRE(res.definition != res.impl);

        REQUIRE(res.methods.has("TestMethod"));
    }
}

TEST_CASE("luau analysis: method registration") {
    GDLuau gd_luau;
    LuauCache luau_cache;

    LOAD_SCRIPT_FILE(script, "analysis/Methods.lua")

    const GDClassDefinition &def = script->get_definition();

    SECTION("with self; basic definition") {
        const GDMethod &method = def.methods["WithSelf"];

        REQUIRE(method.return_val.type == GDEXTENSION_VARIANT_TYPE_STRING);

        REQUIRE(method.arguments.size() == 1);
        REQUIRE(method.arguments[0].name == "arg1");
        REQUIRE(method.arguments[0].type == GDEXTENSION_VARIANT_TYPE_FLOAT);
    }

    SECTION("without self") {
        const GDMethod &method = def.methods["WithoutSelf"];

        REQUIRE(method.arguments.size() == 1);
        REQUIRE(method.arguments[0].name == "arg1");
        REQUIRE(method.arguments[0].type == GDEXTENSION_VARIANT_TYPE_FLOAT);
    }

    SECTION("special arguments") {
        const GDMethod &method = def.methods["SpecialArg"];

        REQUIRE(method.arguments.size() == 4);

        REQUIRE(method.arguments[0].type == GDEXTENSION_VARIANT_TYPE_OBJECT);
        REQUIRE(method.arguments[0].class_name == StringName("Node3D"));

        REQUIRE(method.arguments[1].type == GDEXTENSION_VARIANT_TYPE_OBJECT);
        REQUIRE(method.arguments[1].hint == PROPERTY_HINT_RESOURCE_TYPE);
        REQUIRE(method.arguments[1].hint_string == "Texture2D");

        REQUIRE(method.arguments[2].type == GDEXTENSION_VARIANT_TYPE_ARRAY);
        REQUIRE(method.arguments[2].hint == PROPERTY_HINT_ARRAY_TYPE);
        REQUIRE(method.arguments[2].hint_string == Utils::resource_type_hint("Texture2D"));

        REQUIRE(method.arguments[3].type == GDEXTENSION_VARIANT_TYPE_COLOR);
    }

    SECTION("variant handling") {
        const GDMethod &method = def.methods["Variant"];

        REQUIRE(method.return_val.type == GDEXTENSION_VARIANT_TYPE_NIL);
        REQUIRE(method.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT);

        REQUIRE(method.arguments.size() == 1);
        REQUIRE(method.arguments[0].type == GDEXTENSION_VARIANT_TYPE_NIL);
        REQUIRE(method.arguments[0].usage & PROPERTY_USAGE_NIL_IS_VARIANT);
    }

    SECTION("vararg") {
        const GDMethod &method = def.methods["Vararg"];
        REQUIRE(method.arguments.size() == 1);
        REQUIRE(method.flags & METHOD_FLAG_VARARG);
    }
}
