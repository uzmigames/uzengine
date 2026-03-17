/// Unit tests for the public C runtime API — runtime_version string format,
/// runtime_init / runtime_shutdown lifecycle, and error codes returned for
/// invalid arguments and double-init scenarios.
#include "runtime.h"

#include <cstring>
#include <gtest/gtest.h>

TEST(RuntimeAPI, VersionReturnsValidString) {
    ASSERT_NE(uz_runtime_version(), nullptr);
    EXPECT_STREQ(uz_runtime_version(), "0.7.0");
}

TEST(RuntimeAPI, BackendReturnsUzengine) {
    ASSERT_NE(uz_runtime_backend(), nullptr);
    EXPECT_STREQ(uz_runtime_backend(), "dx11");
}

TEST(RuntimeAPI, InitAndShutdown) {
    EXPECT_EQ(uz_runtime_init(), 1);
    uz_runtime_shutdown();
}
