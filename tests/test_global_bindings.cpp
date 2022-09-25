#include <catch_amalgamated.hpp>

#include "test_utils.h"

TEST_CASE_METHOD(LuauFixture, "globals: enums")
{
	ASSERT_EVAL_EQ(L, "return Enum.VariantOperator.OP_BIT_NEGATE", int, 19)
}

TEST_CASE_METHOD(LuauFixture, "globals: utility functions")
{
    ASSERT_EVAL_EQ(L, "return lerp(0.0, 1.0, 0.75)", float, 0.75);

    SECTION("vararg")
    {
        ASSERT_EVAL_OK(L, "print_rich(1, 2, false, \"[color=red]hey![/color]\")");
    }
}
