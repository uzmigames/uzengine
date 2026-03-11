#include <cstdio>
#include <gtest/gtest.h>
#include "uz_log.h"

int main(int argc, char** argv) {
    uz::LogInit("uzengine.log");
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    // Cleanup generated log files
    std::remove("test_output.log");
    std::remove("test_categories.log");
    std::remove("test_levels.log");
    std::remove("uzengine.log");

    return result;
}
