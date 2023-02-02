#include <catch_amalgamated.hpp>

#include "luagd_lib.h"
#include "test_utils.h"

TEST_CASE_METHOD(LuauFixture, "luau lib: string extension") {
    luaGD_openlibs(L);

    // Handling for length mismatch is the same between both methods
    SECTION("comparison length mismatch") {
        ASSERT_EVAL_EQ(L, "return strext.startswith(\"longer\", \"short\")", bool, false)
    }

    SECTION("startswith") {
        SECTION("true"){
            ASSERT_EVAL_EQ(L, "return strext.startswith(\"startswith\", \"starts\")", bool, true)
        }

        SECTION("false"){
            ASSERT_EVAL_EQ(L, "return strext.startswith(\"startswith\", \"ends\")", bool, false)
        }
    }

    SECTION("endswith") {
        SECTION("true") {
            ASSERT_EVAL_EQ(L, "return strext.endswith(\"withends\", \"ends\")", bool, true)
        }

        SECTION("false") {
            ASSERT_EVAL_EQ(L, "return strext.endswith(\"withends\", \"starts\")", bool, false)
        }
    }
}
