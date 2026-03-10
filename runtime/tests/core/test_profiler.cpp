#include "uz_profiler.h"

#include <gtest/gtest.h>

UZ_DEFINE_STAT(Rendering, TestDrawCalls);

TEST(Profiler, StatCounterIncrementAndReset) {
    Stat_Rendering_TestDrawCalls.Reset();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 0);

    Stat_Rendering_TestDrawCalls.Increment();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 1);

    Stat_Rendering_TestDrawCalls.Increment(9);
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 10);

    Stat_Rendering_TestDrawCalls.Reset();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 0);
}

TEST(Profiler, MacrosCompileAndRunWithoutCrash) {
    UZ_PROFILE_SCOPE(TestScope);
    UZ_PROFILE_FUNCTION();
    UZ_PROFILE_FRAME_MARK();
}
