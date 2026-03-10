#include <gtest/gtest.h>
#include <cstdio>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    // Cleanup generated log files
    std::remove("test_output.log");
    std::remove("test_categories.log");
    std::remove("test_levels.log");
    std::remove("uzengine.log");

    return result;
}
