#include <catch_amalgamated.hpp>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luau_script.h"
#include "gd_luau.h"

TEST_CASE("luau script: script load")
{
    // TODO: singleton is not available during test runs.
    // this will construct the singleton then destroy it at the end of the scope.
    GDLuau gd_luau;

    Ref<LuauScript> script;
    script.instantiate();

    script->set_source_code(R"ASDF(
        local TestClass = gdclass("TestClass")

        TestClass:Tool(true)

        TestClass:RegisterMethod("TestMethod", function() end)
        TestClass:RegisterProperty(gdproperty({ name = "testProperty", type = Enum.VariantType.FLOAT }), "GetTestProperty", "SetTestProperty", 5.5)

        return TestClass
    )ASDF");

    script->reload();

    REQUIRE(script->_is_valid());

    SECTION("method methods")
    {
        REQUIRE(script->is_tool());
        REQUIRE(script->get_script_method_list().size() == 1);
        REQUIRE(script->_has_method("TestMethod"));
        REQUIRE(script->_get_method_info("TestMethod") == Dictionary());
    }

    SECTION("property methods")
    {
        REQUIRE(script->get_script_property_list().size() == 1);
        // test fails without cast. don't know why
        REQUIRE(script->_get_members()[0] == StringName("testProperty"));
        REQUIRE(script->_has_property_default_value("testProperty"));
        REQUIRE(script->_get_property_default_value("testProperty") == Variant(5.5));
    }
}