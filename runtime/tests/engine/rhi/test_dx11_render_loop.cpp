/// Render loop integration tests for the DX11 RHI backend.
///
/// Validates multi-frame command list cycling with fence synchronization.
/// The DX11 fence is emulated via ID3D11Query (D3D11_QUERY_EVENT), so this
/// test also exercises the polling behavior of the emulated fence.
///
/// # Approach
/// DX11 has no explicit command queues or swapchain-free headless mode, so
/// tests use the immediate context path: Begin → End → Execute → Signal →
/// WaitCPU. This exercises the full GPU submission path without needing a
/// visible window.
///
/// # What is tested
/// - 10-frame command list cycle: Begin/End/Execute with fence sync each frame
/// - Device health after loop (fence creation succeeds post-loop)
/// - Frame time variance (no outliers > 10x average after warm-up)
/// - Monotonic fence value progression across frames

#ifdef _WIN32

#include "rhi.h"
#include "rhi_backend.h"

#include <gtest/gtest.h>

#include <chrono>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of frames simulated per render loop test.
constexpr uint32_t kTestFrameCount = 10;

/// Per-frame time budget in milliseconds.
/// Empty command lists on any modern GPU/driver complete well under 10ms.
/// 100ms is a generous upper bound to avoid flaky CI failures.
constexpr double kMaxFrameTimeMs = 100.0;

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture that creates a DX11 device with validation enabled.
///
/// All tests are skipped if no GPU is available. TearDown calls WaitIdle()
/// to ensure the GPU is quiescent before destroying the device.
class DX11RenderLoopTest : public ::testing::Test {
protected:
    uz::IRHIDevice* m_device = nullptr;

    void SetUp() override {
        uz::RHIDeviceDesc desc{};
        desc.backend          = uz::RHIBackend::DX11;
        desc.enableValidation = true;
        m_device = uz::CreateRHIDevice(desc);
        if (!m_device) {
            GTEST_SKIP() << "DX11 device creation failed (no GPU or driver support)";
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

/// Runs 10 frames of Begin/End/Execute + fence Signal/WaitCPU.
///
/// Verifies:
/// - No crashes across 10 frames
/// - Fence reaches the final value after the loop
/// - Each frame completes within kMaxFrameTimeMs
TEST_F(DX11RenderLoopTest, TenFrameCycleCompletesWithoutError) {
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

        ++fenceValue;
        fence->Signal(fenceValue);
        fence->WaitCPU(fenceValue);

        auto frameEnd = std::chrono::high_resolution_clock::now();
        frameTimes[i] = std::chrono::duration<double, std::milli>(
            frameEnd - frameStart).count();
    }

    EXPECT_TRUE(fence->IsComplete(fenceValue))
        << "Fence must be complete at the final value " << fenceValue;
    EXPECT_GE(fence->GetCompletedValue(), fenceValue)
        << "GetCompletedValue must be >= " << fenceValue;

    for (uint32_t i = 0; i < kTestFrameCount; ++i) {
        EXPECT_LT(frameTimes[i], kMaxFrameTimeMs)
            << "Frame " << i << " took " << frameTimes[i] << "ms (limit: "
            << kMaxFrameTimeMs << "ms)";
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fence);
}

/// Verifies the device is not lost after the render loop completes.
///
/// If the device were lost, CreateFence() would return nullptr after WaitIdle().
/// A healthy device must still accept resource creation after 10 frame cycles.
TEST_F(DX11RenderLoopTest, DeviceNotLostAfterLoop) {
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
    m_device->WaitIdle();

    // Device health check: create and immediately destroy a new fence.
    uz::IRHIFence* healthFence = m_device->CreateFence();
    ASSERT_NE(healthFence, nullptr)
        << "Device must remain functional (CreateFence must succeed) after render loop";
    EXPECT_EQ(healthFence->GetCompletedValue(), 0u)
        << "New fence initial value must be 0";
    m_device->DestroyFence(healthFence);
}

/// Measures frame time variance across the 10-frame loop.
///
/// After the first frame (cold-start / JIT overhead excluded), no warm frame
/// may be more than 10x slower than the average warm frame. This catches
/// pathological stalls (e.g., the emulated fence accidentally busy-spinning
/// without yielding, or a driver sync point serializing every frame).
TEST_F(DX11RenderLoopTest, FrameTimeVarianceIsReasonable) {
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

    // Compute average over warm frames (skip frame 0 — cold-start overhead)
    double sum = 0.0;
    for (uint32_t i = 1; i < kTestFrameCount; ++i) {
        sum += frameTimes[i];
    }
    const double avg = sum / static_cast<double>(kTestFrameCount - 1);

    // No warm frame may exceed 10x the average
    for (uint32_t i = 1; i < kTestFrameCount; ++i) {
        EXPECT_LT(frameTimes[i], avg * 10.0)
            << "Frame " << i << " (" << frameTimes[i]
            << "ms) is an outlier vs average " << avg << "ms";
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fence);
}

/// Verifies that fence values increase monotonically across frames.
///
/// Each frame's GetCompletedValue() must be >= the fence value signaled
/// in that frame. This is trivially true for a single-threaded loop but
/// verifies the emulated DX11 fence does not underflow or wrap.
TEST_F(DX11RenderLoopTest, FenceValuesAreMonotonicallyIncreasing) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    uint64_t prevCompleted = 0;

    for (uint32_t i = 0; i < kTestFrameCount; ++i) {
        const uint64_t frameValue = static_cast<uint64_t>(i + 1);

        cmdList->Begin();
        cmdList->End();
        cmdList->Execute();
        fence->Signal(frameValue);
        fence->WaitCPU(frameValue);

        const uint64_t completed = fence->GetCompletedValue();
        EXPECT_GE(completed, frameValue)
            << "Frame " << i << ": GetCompletedValue (" << completed
            << ") must be >= signaled value (" << frameValue << ")";
        EXPECT_GE(completed, prevCompleted)
            << "Frame " << i << ": fence value must not decrease";
        prevCompleted = completed;
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fence);
}

/// Verifies that two independent fences can be used in interleaved frames
/// without interference. This exercises the per-fence state isolation.
TEST_F(DX11RenderLoopTest, TwoFences_InterleavedSignaling_NoInterference) {
    uz::IRHIFence* fenceA = m_device->CreateFence();
    uz::IRHIFence* fenceB = m_device->CreateFence();
    ASSERT_NE(fenceA, nullptr);
    ASSERT_NE(fenceB, nullptr);

    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    for (uint32_t i = 0; i < kTestFrameCount; ++i) {
        const uint64_t v = static_cast<uint64_t>(i + 1);

        cmdList->Begin();
        cmdList->End();
        cmdList->Execute();

        // Alternate signaling: even frames use fenceA, odd frames use fenceB
        uz::IRHIFence* activeFence = (i % 2 == 0) ? fenceA : fenceB;
        activeFence->Signal(v);
        activeFence->WaitCPU(v);

        EXPECT_TRUE(activeFence->IsComplete(v))
            << "Frame " << i << ": active fence must be complete at value " << v;
    }

    m_device->DestroyCommandList(cmdList);
    m_device->DestroyFence(fenceB);
    m_device->DestroyFence(fenceA);
}

} // namespace

#endif // _WIN32
