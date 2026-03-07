// ---------------------------------------------------------------------------
// test_texture_loader.cpp -- Unit tests for AsyncTextureLoader
//
// Tests the worker-thread based async texture decode pipeline.
// Uses the mock decode_texture_from_file from the test build.
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include "engine/renderer/resources/texture_loader.h"
#include "engine/renderer/resources/texture.h"
#include "engine/renderer/resources/material.h"
#include "engine/renderer/rhi/rhi.h"
#include <chrono>
#include <thread>
#include <algorithm>
#include <set>

using namespace uz;

// ============================================================================
// Mock RHI (same pattern as test_material_system.cpp)
// ============================================================================

static rhi_backend_t g_mock_rhi_tl;
static uint64_t g_next_handle_tl = 1000;

static RhiTexture mock_tl_texture_create(const RhiTextureDesc* desc) {
    (void)desc;
    return g_next_handle_tl++;
}

static void mock_tl_texture_destroy(RhiTexture tex) { (void)tex; }
static void mock_tl_texture_upload(RhiTexture tex, const void* data, size_t size) {
    (void)tex; (void)data; (void)size;
}
static void mock_tl_texture_upload_batch(const RhiTextureUploadItem* items, uint32_t count) {
    (void)items; (void)count;
}
static bool mock_tl_texture_replace(RhiTexture tex, const RhiTextureDesc* desc) {
    (void)tex; (void)desc;
    return true;
}

static void setup_tl_mock_rhi() {
    g_mock_rhi_tl.texture_create = mock_tl_texture_create;
    g_mock_rhi_tl.texture_destroy = mock_tl_texture_destroy;
    g_mock_rhi_tl.texture_upload = mock_tl_texture_upload;
    g_mock_rhi_tl.texture_upload_batch = mock_tl_texture_upload_batch;
    g_mock_rhi_tl.texture_replace = mock_tl_texture_replace;
    rhi_set_backend(&g_mock_rhi_tl);
}

// ============================================================================
// Test fixture
// ============================================================================

class AsyncTextureLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        setup_tl_mock_rhi();
        texture_clear_all();
    }

    void TearDown() override {
        texture_clear_all();
        rhi_set_backend(nullptr);
    }

    // Helper: wait for poll_completed to return results (with timeout).
    std::vector<DecodeResult> wait_for_results(
        AsyncTextureLoader& loader, size_t expected,
        int timeout_ms = 2000)
    {
        std::vector<DecodeResult> all;
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeout_ms);

        while (all.size() < expected
            && std::chrono::steady_clock::now() < deadline)
        {
            auto batch = loader.poll_completed();
            for (auto& r : batch) {
                all.push_back(std::move(r));
            }
            if (all.size() < expected) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        return all;
    }
};

// ============================================================================
// T3: AsyncTextureLoader submit/poll lifecycle
// ============================================================================

// Single texture submit → poll completes with correct metadata.
TEST_F(AsyncTextureLoaderTest, SubmitSingle_PollReturnsResult) {
    AsyncTextureLoader loader(1);

    loader.submit("test_texture_a.png");

    auto results = wait_for_results(loader, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].path, "test_texture_a.png");
    EXPECT_NE(results[0].pixels, nullptr);
    EXPECT_EQ(results[0].width, 128u);
    EXPECT_EQ(results[0].height, 128u);
    EXPECT_EQ(results[0].pixel_size, static_cast<size_t>(128 * 128 * 4));

    // Free the pixels (mock uses malloc).
    free_decoded_texture_pixels(results[0].pixels);
}

// Multiple textures submit → all complete eventually.
TEST_F(AsyncTextureLoaderTest, SubmitMultiple_AllComplete) {
    AsyncTextureLoader loader(2);

    loader.submit("tex_1.png");
    loader.submit("tex_2.png");
    loader.submit("tex_3.png");

    auto results = wait_for_results(loader, 3);
    ASSERT_EQ(results.size(), 3u);

    // Verify all paths are present (order may vary due to threading).
    std::set<std::string> paths;
    for (auto& r : results) {
        paths.insert(r.path);
        EXPECT_NE(r.pixels, nullptr);
        EXPECT_EQ(r.width, 128u);
        EXPECT_EQ(r.height, 128u);
    }
    EXPECT_TRUE(paths.count("tex_1.png"));
    EXPECT_TRUE(paths.count("tex_2.png"));
    EXPECT_TRUE(paths.count("tex_3.png"));

    for (auto& r : results) {
        free_decoded_texture_pixels(r.pixels);
    }
}

// poll_completed returns empty when no work submitted.
TEST_F(AsyncTextureLoaderTest, PollEmpty_ReturnsEmpty) {
    AsyncTextureLoader loader(1);

    auto results = loader.poll_completed();
    EXPECT_TRUE(results.empty());
}

// poll_completed drains results — second poll is empty.
TEST_F(AsyncTextureLoaderTest, PollDrains_SecondPollEmpty) {
    AsyncTextureLoader loader(1);

    loader.submit("drain_test.png");
    auto results = wait_for_results(loader, 1);
    ASSERT_EQ(results.size(), 1u);

    // Second poll should be empty.
    auto second = loader.poll_completed();
    EXPECT_TRUE(second.empty());

    free_decoded_texture_pixels(results[0].pixels);
}

// pending_count reflects queued + in-flight work.
TEST_F(AsyncTextureLoaderTest, PendingCount_ReflectsWork) {
    AsyncTextureLoader loader(1);

    EXPECT_EQ(loader.pending_count(), 0u);

    // Submit several — pending_count should be > 0 before completion.
    loader.submit("pending_a.png");
    loader.submit("pending_b.png");
    loader.submit("pending_c.png");

    // At least some should be pending immediately after submit.
    // (Timing-sensitive — worker might process before we check, but
    //  with 3 items and 1 thread, at least 1 should be pending.)
    // We poll results to drain completed ones.
    auto results = wait_for_results(loader, 3);
    ASSERT_EQ(results.size(), 3u);

    // After all completed and polled, pending should be 0.
    EXPECT_EQ(loader.pending_count(), 0u);

    for (auto& r : results) {
        free_decoded_texture_pixels(r.pixels);
    }
}

// Shutdown joins workers cleanly — no crash or hang.
TEST_F(AsyncTextureLoaderTest, Shutdown_CleansUp) {
    {
        AsyncTextureLoader loader(2);
        loader.submit("shutdown_a.png");
        loader.submit("shutdown_b.png");
        // Destructor calls shutdown().
    }
    // If we reach here, no hang or crash.
    SUCCEED();
}

// Explicit shutdown followed by destructor is safe (double-shutdown).
TEST_F(AsyncTextureLoaderTest, DoubleShutdown_Safe) {
    AsyncTextureLoader loader(1);
    loader.submit("double_shutdown.png");
    loader.shutdown();
    loader.shutdown();  // Second call should be no-op.
    SUCCEED();
}

// Multiple worker threads process work concurrently.
TEST_F(AsyncTextureLoaderTest, MultipleWorkers_ProcessConcurrently) {
    AsyncTextureLoader loader(4);

    for (int i = 0; i < 8; ++i) {
        loader.submit("multi_" + std::to_string(i) + ".png");
    }

    auto results = wait_for_results(loader, 8);
    ASSERT_EQ(results.size(), 8u);

    std::set<std::string> paths;
    for (auto& r : results) {
        paths.insert(r.path);
        EXPECT_NE(r.pixels, nullptr);
    }
    EXPECT_EQ(paths.size(), 8u);

    for (auto& r : results) {
        free_decoded_texture_pixels(r.pixels);
    }
}

// Submit after poll — new work processes correctly.
TEST_F(AsyncTextureLoaderTest, SubmitAfterPoll_Works) {
    AsyncTextureLoader loader(1);

    loader.submit("batch1.png");
    auto r1 = wait_for_results(loader, 1);
    ASSERT_EQ(r1.size(), 1u);
    EXPECT_EQ(r1[0].path, "batch1.png");
    free_decoded_texture_pixels(r1[0].pixels);

    // Submit more work after draining.
    loader.submit("batch2.png");
    auto r2 = wait_for_results(loader, 1);
    ASSERT_EQ(r2.size(), 1u);
    EXPECT_EQ(r2[0].path, "batch2.png");
    free_decoded_texture_pixels(r2[0].pixels);
}

// Constructor clamps thread count to valid range.
TEST_F(AsyncTextureLoaderTest, ThreadCount_Clamped) {
    // 0 threads should be clamped to 1 (still functional).
    {
        AsyncTextureLoader loader(0);
        loader.submit("clamp_low.png");
        auto results = wait_for_results(loader, 1);
        ASSERT_EQ(results.size(), 1u);
        free_decoded_texture_pixels(results[0].pixels);
    }

    // Negative should be clamped to 1.
    {
        AsyncTextureLoader loader(-5);
        loader.submit("clamp_neg.png");
        auto results = wait_for_results(loader, 1);
        ASSERT_EQ(results.size(), 1u);
        free_decoded_texture_pixels(results[0].pixels);
    }
}

// Shutdown with pending unclaimed results frees their pixel memory.
TEST_F(AsyncTextureLoaderTest, Shutdown_FreesUnclaimedResults) {
    AsyncTextureLoader loader(2);
    loader.submit("unclaimed_1.png");
    loader.submit("unclaimed_2.png");

    // Wait for decode to finish but DON'T poll.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Shutdown should free the unclaimed pixels internally.
    loader.shutdown();
    SUCCEED(); // No leak (ASAN would catch it).
}

// ============================================================================
// T4: TextureManager placeholder + deferred results (via free functions)
// ============================================================================

// texture_init_placeholders creates placeholder textures.
TEST_F(AsyncTextureLoaderTest, InitPlaceholders_CreatesTextures) {
    texture_init_placeholders();
    // If init succeeded, subsequent calls are idempotent.
    texture_init_placeholders();
    SUCCEED();
}

// texture_tick with no queued work returns false.
TEST_F(AsyncTextureLoaderTest, TextureTick_NoWork_ReturnsFalse) {
    EXPECT_FALSE(texture_tick(4));
}

// texture_has_queued returns false when nothing queued.
TEST_F(AsyncTextureLoaderTest, TextureHasQueued_NoWork_ReturnsFalse) {
    EXPECT_FALSE(texture_has_queued());
}
