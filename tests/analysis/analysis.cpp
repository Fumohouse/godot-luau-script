#include <catch_amalgamated.hpp>

#include <gdextension_interface.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "core/permissions.h"
#include "core/runtime.h"
#include "scripting/luau_cache.h"
#include "scripting/luau_lib.h"
#include "test_utils.h"

TEST_CASE("luau analysis") {
	LuauRuntime gd_luau;
	LuauCache luau_cache;

	SECTION("normal") {
		LOAD_SCRIPT_FILE(script, "analysis/TestClass.lua")

		const LuauScriptAnalysisResult &res = script->get_luau_data().analysis_result;
		REQUIRE(res.definition);
		REQUIRE(res.class_type);

		const GDClassDefinition &def = script->get_definition();

		SECTION("base definition") {
			REQUIRE(def.name == "TestClass");
			REQUIRE(def.is_tool);
			REQUIRE(def.permissions == (PERMISSION_INTERNAL | PERMISSION_OS));
			REQUIRE(def.base_script);
			REQUIRE(def.base_script->get_path() == "res://test_scripts/analysis/Base.lua");
		}

		SECTION("method registration") {
			REQUIRE(def.methods.has("TestMethod"));
			const GDMethod &method = def.methods["TestMethod"];

			REQUIRE(method.arguments.size() == 5);

			REQUIRE(method.arguments[0].name == "p1");
			REQUIRE(method.arguments[0].type == GDEXTENSION_VARIANT_TYPE_BOOL);

			REQUIRE(method.arguments[1].name == "p2");
			REQUIRE(method.arguments[1].type == GDEXTENSION_VARIANT_TYPE_OBJECT);
			REQUIRE(method.arguments[1].hint == PROPERTY_HINT_NODE_TYPE);
			REQUIRE(method.arguments[1].class_name == StringName("Node3D"));

			REQUIRE(method.arguments[2].name == "p3");
			REQUIRE(method.arguments[2].type == GDEXTENSION_VARIANT_TYPE_NIL);
			REQUIRE(method.arguments[2].usage == (PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT));

			REQUIRE(method.arguments[3].name == "p4");
			REQUIRE(method.arguments[3].type == GDEXTENSION_VARIANT_TYPE_OBJECT);
			REQUIRE(method.arguments[3].class_name == StringName("Base"));

			REQUIRE(method.arguments[4].name == "p5");
			REQUIRE(method.arguments[4].type == GDEXTENSION_VARIANT_TYPE_ARRAY);
			REQUIRE(method.arguments[4].hint == PROPERTY_HINT_ARRAY_TYPE);
			REQUIRE(method.arguments[4].hint_string == StringName("Base"));

			REQUIRE(method.return_val.type == GDEXTENSION_VARIANT_TYPE_NIL);
			REQUIRE(method.return_val.usage == (PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT));

			REQUIRE(method.flags == (METHOD_FLAGS_DEFAULT | METHOD_FLAG_VARARG));
		}

		SECTION("no annotation method registration") {
			REQUIRE(def.methods.has("TestMethodNoAnnotation"));
			const GDMethod &method = def.methods["TestMethodNoAnnotation"];

			REQUIRE(method.arguments.size() == 1);

			REQUIRE(method.arguments[0].name == "p1");
			REQUIRE(method.arguments[0].type == GDEXTENSION_VARIANT_TYPE_NIL);
			REQUIRE(method.arguments[0].usage == (PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT));

			REQUIRE(method.return_val.type == GDEXTENSION_VARIANT_TYPE_NIL);
			REQUIRE(method.return_val.usage == (PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT));
		}

		SECTION("nullable object return value") {
			REQUIRE(def.methods.has("TestMethodNullableObjectReturn"));
			const GDMethod &method = def.methods["TestMethodNullableObjectReturn"];

			REQUIRE(method.return_val.type == GDEXTENSION_VARIANT_TYPE_OBJECT);
		}

		SECTION("rpc registration") {
			REQUIRE(def.rpcs.has("TestMethod"));
			const GDRpc &rpc = def.rpcs["TestMethod"];

			REQUIRE(rpc.name == "TestMethod");
			REQUIRE(rpc.rpc_mode == MultiplayerAPI::RPC_MODE_AUTHORITY);
			REQUIRE(rpc.transfer_mode == MultiplayerPeer::TRANSFER_MODE_RELIABLE);
			REQUIRE(rpc.call_local);
			REQUIRE(rpc.channel == 3);
		}

		SECTION("constant registration") {
			REQUIRE(def.constants.has("TEST_CONSTANT"));
		}

		SECTION("signal registration") {
			REQUIRE(def.signals.has("testSignal1"));
			const GDMethod &signal1 = def.signals["testSignal1"];

			REQUIRE(signal1.name == "testSignal1");
			REQUIRE(signal1.arguments.size() == 0);

			REQUIRE(def.signals.has("testSignal2"));
			const GDMethod &signal2 = def.signals["testSignal2"];

			REQUIRE(signal2.name == "testSignal2");
			REQUIRE(signal2.arguments.size() == 2);
			REQUIRE(signal2.arguments[0].name == "argName1");
			REQUIRE(signal2.arguments[0].type == GDEXTENSION_VARIANT_TYPE_FLOAT);
			REQUIRE(signal2.arguments[1].name == "arg2");
			REQUIRE(signal2.arguments[1].type == GDEXTENSION_VARIANT_TYPE_VECTOR2);

			REQUIRE(signal2.flags == (METHOD_FLAGS_DEFAULT | METHOD_FLAG_VARARG));
		}

		SECTION("property registration") {
			REQUIRE(def.properties.size() == 3);

			REQUIRE(def.properties[0].property.name == "TestGroup");
			REQUIRE(def.properties[0].property.usage == PROPERTY_USAGE_GROUP);

			REQUIRE(def.properties[1].property.name == "testProperty1");
			REQUIRE(def.properties[1].property.type == GDEXTENSION_VARIANT_TYPE_NODE_PATH);
			REQUIRE(def.properties[1].property.hint == PROPERTY_HINT_NODE_PATH_VALID_TYPES);
			REQUIRE(def.properties[1].property.hint_string == "Camera3D,Camera2D");
			REQUIRE(def.properties[1].default_value == NodePath("Node3D/"));
			REQUIRE(def.properties[1].setter == StringName("setTestProperty1"));
			REQUIRE(def.properties[1].getter == StringName("getTestProperty1"));

			REQUIRE(def.properties[2].property.name == "testProperty2");
			REQUIRE(def.properties[2].property.type == GDEXTENSION_VARIANT_TYPE_FLOAT);
			REQUIRE(def.properties[2].property.hint == PROPERTY_HINT_RANGE);
			REQUIRE(def.properties[2].property.hint_string == "0.0,100.0,3.0,degrees");
		}
	}

	SECTION("odd") {
		LOAD_SCRIPT_FILE(script, "analysis/Odd.lua")

		const LuauScriptAnalysisResult &res = script->get_luau_data().analysis_result;
		REQUIRE(res.definition);
	}
}
