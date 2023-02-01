#include <catch_amalgamated.hpp>

#include <gdextension_interface.h>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "godot_cpp/classes/object.hpp"
#include "luagd_bindings_stack.gen.h" // IWYU pragma: keep
#include "luagd_stack.h"
#include "luagd_variant.h"
#include "test_utils.h"

/* PUSH */
template <typename T>
static void push_value(lua_State *L, const T &value) { LuaStackOp<T>::push(L, value); }

template <>
void push_value<StringName>(lua_State *L, const StringName &value) { LuaStackOp<StringName>::push(L, value, true); }

/* PUSH COERCED */
template <typename T>
static void push_coerced_test(lua_State *L, const T &value) {}

template <>
void push_coerced_test<StringName>(lua_State *L, const StringName &value) {
    LuaStackOp<String>::push(L, String(value));
}

/* COMPARISON */
template <typename T>
static bool cmp_eq(const LuauVariant &variant, const T &value) { return *variant.get_ptr<T>() == value; }

template <>
bool cmp_eq<Object *>(const LuauVariant &variant, Object *const &value) {
    return *variant.get_ptr<GDExtensionObjectPtr>() == value->_owner;
}

/* MAIN */
template <typename T>
static void variant_test(lua_State *L, GDExtensionVariantType type, const T &value, bool is_from_luau, bool is_coerced = false) {
    SECTION(Variant::get_type_name(Variant::Type(type)).utf8().get_data()) {
        LuauVariant variant;

        SECTION("initialize and assign") {
            variant.initialize(type);
            variant.assign_variant(value);

            REQUIRE(variant.get_type() == type);
            REQUIRE(cmp_eq(variant, value));

            SECTION("copy") {
                LuauVariant copy = variant;
                REQUIRE(copy.get_type() == type);
                REQUIRE(cmp_eq(variant, value));
            }

            SECTION("to variant") {
                REQUIRE(variant.to_variant() == Variant(value));
            }
        }

        SECTION("stack") {
            push_value(L, value);

            REQUIRE(LuauVariant::lua_is(L, -1, type));
            variant.lua_check(L, -1, type);

            REQUIRE(variant.get_type() == type);
            REQUIRE(variant.is_from_luau() == is_from_luau);
            REQUIRE(cmp_eq(variant, value));

            SECTION("copy") {
                LuauVariant copy = variant;
                REQUIRE(copy.get_type() == type);
                REQUIRE(copy.is_from_luau() == is_from_luau);
                REQUIRE(cmp_eq(variant, value));
            }
        }

        if (is_coerced) {
            SECTION("coerced") {
                push_coerced_test(L, value);

                REQUIRE(LuauVariant::lua_is(L, -1, type));
                variant.lua_check(L, -1, type);

                REQUIRE(variant.get_type() == type);
                REQUIRE(!variant.is_from_luau());
                REQUIRE(cmp_eq(variant, value));
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
    Object obj;
    variant_test(L, GDEXTENSION_VARIANT_TYPE_OBJECT, &obj, false);
}
