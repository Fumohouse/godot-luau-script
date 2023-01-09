#include <catch_amalgamated.hpp>

#include "test_utils.h"

TEST_CASE_METHOD(LuauFixture, "globals: enums") {
    SECTION("normal access") {
        ASSERT_EVAL_EQ(L, "return Enum.VariantOperator.BIT_NEGATE", int, 19)
    }

    SECTION("invalid access") {
        SECTION("invalid enum") {
            ASSERT_EVAL_FAIL(L, "return Enum.NonExistentWhat", "exec:1: 'NonExistentWhat' is not a valid member of 'Enum'")
        }

        SECTION("invalid value") {
            ASSERT_EVAL_FAIL(L, "return Enum.VariantOperator.BLAH_BLAH", "exec:1: 'BLAH_BLAH' is not a valid member of 'VariantOperator'")
        }
    }
}

TEST_CASE_METHOD(LuauFixture, "globals: constants") {
    SECTION("invalid access") {
        ASSERT_EVAL_FAIL(L, "return Constants.WHATEVER", "exec:1: 'WHATEVER' is not a valid member of 'Constants'")
    }
}

TEST_CASE_METHOD(LuauFixture, "globals: utility functions") {
    ASSERT_EVAL_EQ(L, "return lerp(0.0, 1.0, 0.75)", float, 0.75)

    SECTION("vararg") {
        ASSERT_EVAL_OK(L, "print_rich(1, 2, false, \"[color=red]hey![/color]\")")
    }
}
