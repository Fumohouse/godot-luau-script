#include <catch_amalgamated.hpp>

#include <gdextension_interface.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_bindings_stack.gen.h" // IWYU pragma: keep
#include "luagd_stack.h"
#include "luagd_variant.h"
#include "test_utils.h"

/* PUSH */
template <typename T>
static void push_value(lua_State *L, const T &p_value) { LuaStackOp<T>::push(L, p_value); }

template <>
void push_value<Object *>(lua_State *L, Object *const &p_value) { LuaStackOp<Object *>::push(L, p_value->_owner); }

template <>
void push_value<StringName>(lua_State *L, const StringName &p_value) { LuaStackOp<StringName>::push(L, p_value, true); }

/* PUSH COERCED */
template <typename T>
static void push_coerced_test(lua_State *L, const T &p_value) {}

template <>
void push_coerced_test<StringName>(lua_State *L, const StringName &p_value) {
	LuaStackOp<String>::push(L, String(p_value));
}

/* COMPARISON */
template <typename T>
static bool cmp_eq(const LuauVariant &p_variant, const T &p_value) { return *p_variant.get_ptr<T>() == p_value; }

template <>
bool cmp_eq<Object *>(const LuauVariant &p_variant, Object *const &p_value) {
	return *p_variant.get_ptr<GDExtensionObjectPtr>() == p_value->_owner;
}

/* MAIN */
template <typename T>
static void variant_test(lua_State *L, GDExtensionVariantType p_type, const T &p_value, bool p_is_from_luau, bool p_is_coerced = false) {
	SECTION(Variant::get_type_name(Variant::Type(p_type)).utf8().get_data()) {
		LuauVariant variant;

		SECTION("initialize and assign") {
			variant.initialize(p_type);
			variant.assign_variant(p_value);

			REQUIRE(variant.get_type() == p_type);
			REQUIRE(cmp_eq(variant, p_value));

			SECTION("copy") {
				LuauVariant copy = variant; // NOLINT(performance-unnecessary-copy-initialization)
				REQUIRE(copy.get_type() == p_type);
				REQUIRE(cmp_eq(variant, p_value));
			}

			SECTION("to variant") {
				REQUIRE(variant.to_variant() == Variant(p_value));
			}
		}

		SECTION("stack") {
			push_value(L, p_value);

			REQUIRE(LuauVariant::lua_is(L, -1, p_type));
			variant.lua_check(L, -1, p_type);

			REQUIRE(variant.get_type() == p_type);
			REQUIRE(variant.is_from_luau() == p_is_from_luau);
			REQUIRE(cmp_eq(variant, p_value));

			SECTION("copy") {
				LuauVariant copy = variant; // NOLINT(performance-unnecessary-copy-initialization)
				REQUIRE(copy.get_type() == p_type);
				REQUIRE(copy.is_from_luau() == p_is_from_luau);
				REQUIRE(cmp_eq(variant, p_value));
			}
		}

		if (p_is_coerced) {
			SECTION("coerced") {
				push_coerced_test(L, p_value);

				REQUIRE(LuauVariant::lua_is(L, -1, p_type));
				variant.lua_check(L, -1, p_type);

				REQUIRE(variant.get_type() == p_type);
				REQUIRE(!variant.is_from_luau());
				REQUIRE(cmp_eq(variant, p_value));
			}
		}
	}
}

TEST_CASE_METHOD(LuauFixture, "luau variant") {
	// Assign
	variant_test(L, GDEXTENSION_VARIANT_TYPE_BOOL, true, false);

	// AssignDtor
	variant_test(L, GDEXTENSION_VARIANT_TYPE_STRING, String("hello world"), false);

	// Userdata
	variant_test(L, GDEXTENSION_VARIANT_TYPE_VECTOR2, Vector2(1, 2), true);

	// Coerced
	variant_test(L, GDEXTENSION_VARIANT_TYPE_STRING_NAME, StringName("hi"), true, true);

	// Ptr
	variant_test(L, GDEXTENSION_VARIANT_TYPE_TRANSFORM3D, Transform3D().translated(Vector3(1, 2, 3)), true);

	// Variant
	variant_test(L, GDEXTENSION_VARIANT_TYPE_NIL, Variant(Vector3(1, 1, 1)), false);

	// Object
	Object *obj = memnew(Object);
	variant_test(L, GDEXTENSION_VARIANT_TYPE_OBJECT, obj, false);
	memdelete(obj);
}
