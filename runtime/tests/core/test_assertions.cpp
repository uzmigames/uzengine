#include "engine_assert.h"

#include <gtest/gtest.h>

TEST(Assertions, EnsureReturnsTrueOnPass) {
    EXPECT_TRUE(UZ_ENSURE(true));
    EXPECT_TRUE(UZ_ENSURE(1 == 1));
}

TEST(Assertions, VerifyPassesWithoutCrash) {
    int x = 42;
    UZ_VERIFY(x == 42);
    EXPECT_EQ(x, 42);
}

TEST(Assertions, CheckPassesWithValidConditions) {
    int* ptr = new int(10);
    UZ_CHECK(ptr != nullptr);
    UZ_CHECKF(*ptr == 10, "Expected 10, got %d", *ptr);
    delete ptr;
}
