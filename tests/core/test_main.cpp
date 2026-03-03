// test_main.cpp — custom main so we can call engine_arenas_init() before any test runs.
// GTest::gtest_main already provides a main(), but we need to inject arena init.
// We link against GTest::gtest (not GTest::gtest_main) and provide our own main here.

#include <gtest/gtest.h>
#include "engine/core/arena.h"

int main(int argc, char** argv) {
    uz::engine_arenas_init();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
