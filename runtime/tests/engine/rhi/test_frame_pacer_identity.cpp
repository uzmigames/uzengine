#include "engine/renderer/core/frame_pacer.h"
#include "engine/rhi/optimization/rhi_frame_pacer.h"

#include <gtest/gtest.h>
#include <type_traits>

// Both headers must coexist. Previously they declared different layouts under
// the same names, and the static librarian silently selected duplicate methods.
static_assert(!std::is_same_v<uz::FramePacer, uz::AdaptiveFramePacer>);
static_assert(!std::is_same_v<uz::FramePacerConfig, uz::AdaptiveFramePacerConfig>);

TEST(FramePacerIdentity, IndependentConfigurationsAndLifetimes) {
    uz::FramePacer display;
    uz::AdaptiveFramePacer adaptive;
    uz::AdaptiveFramePacerConfig config;
    config.minSwapChainBuffers = 3;
    config.maxSwapChainBuffers = 4;
    ASSERT_TRUE(adaptive.Init(config));
    EXPECT_EQ(adaptive.GetRecommendedBufferCount(), 3u);
    EXPECT_FALSE(display.Init(nullptr));
    EXPECT_FALSE(display.IsInitialized());

    adaptive.BeginFrame();
    adaptive.EndFrame();
    EXPECT_GE(adaptive.GetLastFrameTimeMs(), 0.0f);
    EXPECT_EQ(display.GetStats().totalFrames, 0u);
    display.Shutdown();
    EXPECT_EQ(adaptive.GetRecommendedBufferCount(), 3u);
    adaptive.Shutdown();
    EXPECT_TRUE(adaptive.Init(config));
    adaptive.Shutdown();
}
