#include "runtime.h"

#include <cstring>
#include <gtest/gtest.h>

TEST(RuntimeAPI, VersionReturnsValidString) {
    ASSERT_NE(uz_runtime_version(), nullptr);
    EXPECT_STREQ(uz_runtime_version(), "0.2.0");
}

TEST(RuntimeAPI, BackendReturnsUzengine) {
    ASSERT_NE(uz_runtime_backend(), nullptr);
    EXPECT_STREQ(uz_runtime_backend(), "uzengine");
}

TEST(RuntimeAPI, InitAndShutdown) {
    EXPECT_EQ(uz_runtime_init(), 1);
    uz_runtime_shutdown();
}
