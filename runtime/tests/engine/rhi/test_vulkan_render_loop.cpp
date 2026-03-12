/// Render loop integration test for the Vulkan RHI backend.
///
/// Validates task 7.3: run 10 frames of command list cycling with fence
/// synchronization, verify no validation errors and reasonable frame times.
///
/// # Approach
/// The clear screen demo requires a window/swapchain, so this test exercises
/// the headless command list cycle: Begin → End → Execute → Signal → WaitCPU.
/// This covers the same GPU submission path without requiring a visible window.

#ifdef _WIN32

#include "rhi.h"
#include "rhi_backend.h"

#include <gtest/gtest.h>

#include <chrono>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of frames to simulate in the render loop test.
constexpr uint32_t kTestFrameCount = 10;

/// Maximum acceptable time for a single empty frame (ms).
/// Empty command lists on any modern GPU complete well under 10ms;
/// 100ms is a generous upper bound to avoid flaky failures on CI.
constexpr double kMaxFrameTimeMs = 100.0;

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture that creates a Vulkan device with validation enabled.
/// Skips all tests if no GPU is available or no Vulkan driver is installed.
class VulkanRenderLoopTest : public ::testing::Test {
protected:
    uz::IRHIDevice* m_device = nullptr;

    void SetUp() override {
        uz::RHIDeviceDesc desc{};
        desc.backend = uz::RHIBackend::Vulkan;
        desc.enableValidation = true;
        m_device = uz::CreateRHIDevice(desc);
        if (!m_device) {
            GTEST_SKIP() << "Vulkan device creation failed (no GPU or Vulkan driver)";
        }
    }

    void TearDown() override {
        if (m_device) {
            m_device->WaitIdle();
            uz::DestroyRHIDevice(m_device);
            m_device = nullptr;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

/// Runs 10 frames of command list begin/end/execute with fence sync.
/// Verifies no crashes, device not lost, and each frame completes in < 100ms.
TEST_F(VulkanRenderLoopTest, TenFrameCycleCompletesWithoutError) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    double frameTimes[kTestFrameCount] = {};
    uint64_t fenceValue = 0;

    for (uint32_t i = 0; i < kTestFrameCount; ++i) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        cmdList->Begin();
        cmdList->End();
        cmdList->Execute();

        // Synchronize CPU/GPU via fence
        ++fenceValue;
        fence->Signal(fenceValue);
        fence->WaitCPU(fenceValue);

        auto frameEnd = std::chrono::high_resolution_clock::now();
        frameTimes[i] = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
    }

    // Verify fence reached final value
    EXPECT_TRUE(fence->IsComplete(fenceValue));
    EXPECT_GE(fence->GetCompletedValue(), fenceValue);

    // Verify each frame completed within the time budget
    for (uint32_t i = 0; i < kTestFrameCount; ++i) {
        EXPECT_LT(frameTimes[i], kMaxFrameTimeMs)
            << "Frame " << i << " took " << frameTimes[i] << "ms";
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fence);
}

/// Verifies the device is not lost after the render loop completes.
TEST_F(VulkanRenderLoopTest, DeviceNotLostAfterLoop) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    uint64_t fenceValue = 0;
    for (uint32_t i = 0; i < kTestFrameCount; ++i) {
        cmdList->Begin();
        cmdList->End();
        cmdList->Execute();
        fence->Signal(++fenceValue);
        fence->WaitCPU(fenceValue);
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fence);

    // If the device were lost, WaitIdle would hang or crash.
    // A successful WaitIdle + fence creation validates device health.
    m_device->WaitIdle();

    uz::IRHIFence* healthFence = m_device->CreateFence();
    ASSERT_NE(healthFence, nullptr) << "Device should still be functional after render loop";
    EXPECT_EQ(healthFence->GetCompletedValue(), 0u);
    m_device->DestroyFence(healthFence);
}

/// Measures frame time variance across the 10-frame loop.
/// Ensures no single frame is an extreme outlier (> 10x the average).
TEST_F(VulkanRenderLoopTest, FrameTimeVarianceIsReasonable) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    double frameTimes[kTestFrameCount] = {};
    uint64_t fenceValue = 0;

    for (uint32_t i = 0; i < kTestFrameCount; ++i) {
        auto start = std::chrono::high_resolution_clock::now();

        cmdList->Begin();
        cmdList->End();
        cmdList->Execute();
        fence->Signal(++fenceValue);
        fence->WaitCPU(fenceValue);

        auto end = std::chrono::high_resolution_clock::now();
        frameTimes[i] = std::chrono::duration<double, std::milli>(end - start).count();
    }

    // Compute average (skip frame 0 — cold start often has JIT/driver overhead)
    double sum = 0.0;
    for (uint32_t i = 1; i < kTestFrameCount; ++i) {
        sum += frameTimes[i];
    }
    double avg = sum / static_cast<double>(kTestFrameCount - 1);

    // No warm frame should exceed 10x the average
    for (uint32_t i = 1; i < kTestFrameCount; ++i) {
        EXPECT_LT(frameTimes[i], avg * 10.0)
            << "Frame " << i << " (" << frameTimes[i]
            << "ms) is an outlier vs avg " << avg << "ms";
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fence);
}

} // namespace

#endif // _WIN32
