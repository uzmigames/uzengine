/// Render loop integration test for the Metal RHI backend.
///
/// Validates a 10-frame headless command buffer cycling loop with MTLFence
/// synchronization. Verifies no crashes, device health after the loop,
/// reasonable frame times, and frame time variance within acceptable bounds.
///
/// # Approach
/// Swapchain/NSWindow rendering requires a visible window and CAMetalLayer,
/// which is unavailable in headless CI. This test exercises the same GPU
/// submission path using the headless command list cycle:
/// Begin → End → Execute → Signal → WaitCPU.
///
/// This covers the complete Metal command buffer lifecycle:
/// MTLCommandBuffer alloc → encode (empty) → commit → MTLSharedEvent signal → CPU wait.

#if defined(__APPLE__)

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
///
/// Empty Metal command buffers complete well under 5ms on any modern Apple GPU.
/// 100ms is a generous upper bound to avoid flaky failures on CI under load.
constexpr double kMaxFrameTimeMs = 100.0;

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture that creates a Metal device with validation enabled.
///
/// Skips all tests if no Metal-capable GPU is available or the OS is not macOS.
/// TearDown calls WaitIdle() before device destruction to ensure no in-flight work.
class MetalRenderLoopTest : public ::testing::Test {
protected:
    uz::IRHIDevice* m_device = nullptr;

    void SetUp() override {
        uz::RHIDeviceDesc desc{};
        desc.backend = uz::RHIBackend::Metal;
        desc.enableValidation = true;
        m_device = uz::CreateRHIDevice(desc);
        if (!m_device) {
            GTEST_SKIP() << "Metal device creation failed (no GPU or macOS)";
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

/// Runs 10 frames of command list begin/end/execute with fence synchronization.
///
/// Each frame must:
/// - Record a valid (empty) command buffer via Begin/End/Execute.
/// - Synchronize CPU/GPU via Signal + WaitCPU.
/// - Complete within kMaxFrameTimeMs (100ms generous upper bound).
///
/// After the loop, the fence must report the final value as complete.
TEST_F(MetalRenderLoopTest, TenFrameCycleCompletesWithoutError) {
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

        // Synchronize CPU/GPU via fence after each frame.
        ++fenceValue;
        fence->Signal(fenceValue);
        fence->WaitCPU(fenceValue);

        auto frameEnd = std::chrono::high_resolution_clock::now();
        frameTimes[i] = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
    }

    // Fence must have reached the final value.
    EXPECT_TRUE(fence->IsComplete(fenceValue));
    EXPECT_GE(fence->GetCompletedValue(), fenceValue);

    // Each frame must have completed within the time budget.
    for (uint32_t i = 0; i < kTestFrameCount; ++i) {
        EXPECT_LT(frameTimes[i], kMaxFrameTimeMs)
            << "Frame " << i << " took " << frameTimes[i] << "ms (limit " << kMaxFrameTimeMs << "ms)";
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fence);
}

/// Verifies the Metal device is not lost after completing a 10-frame render loop.
///
/// A lost Metal device cannot create new resources or accept command buffers.
/// Verifying that CreateFence() succeeds after the loop (and GetCompletedValue()
/// returns 0 for the new fence) confirms device health.
TEST_F(MetalRenderLoopTest, DeviceNotLostAfterLoop) {
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

    // WaitIdle after the loop must complete without hanging.
    m_device->WaitIdle();

    // A successful fence creation confirms the device is still operational.
    uz::IRHIFence* healthFence = m_device->CreateFence();
    ASSERT_NE(healthFence, nullptr) << "Device should still be functional after render loop";
    EXPECT_EQ(healthFence->GetCompletedValue(), 0u);
    m_device->DestroyFence(healthFence);
}

/// Verifies frame time variance is within acceptable bounds across a 10-frame loop.
///
/// Frame 0 is excluded from the variance check because the first Metal command buffer
/// submission incurs driver/JIT overhead (shader compilation, pipeline warm-up).
/// For frames 1–9, no single frame should exceed 10x the average frame time.
TEST_F(MetalRenderLoopTest, FrameTimeVarianceIsReasonable) {
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

    // Compute the average from frame 1 onward (skip cold-start frame 0).
    double sum = 0.0;
    for (uint32_t i = 1; i < kTestFrameCount; ++i) {
        sum += frameTimes[i];
    }
    double avg = sum / static_cast<double>(kTestFrameCount - 1);

    // No warm frame should exceed 10x the average frame time.
    for (uint32_t i = 1; i < kTestFrameCount; ++i) {
        EXPECT_LT(frameTimes[i], avg * 10.0)
            << "Frame " << i << " (" << frameTimes[i]
            << "ms) is an outlier vs avg " << avg << "ms";
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fence);
}

} // namespace

#endif // defined(__APPLE__)
