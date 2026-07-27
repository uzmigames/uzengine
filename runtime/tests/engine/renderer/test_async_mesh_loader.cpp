/// Tests for AsyncMeshLoader — background .uzmesh read + parse worker pool.
///
/// All tests are CPU-only — no GPU device is created. They verify:
/// - A valid .uzmesh file on disk round-trips through Submit → PollCompleted
///   with correct vertex/index counts and the resultKey override applied
/// - A missing file completes with an invalid mesh (failure signal, no hang)
/// - PendingCount drains to zero and Shutdown is idempotent

#include <gtest/gtest.h>

#include "async_mesh_loader.h"
#include "mesh.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace uz;

namespace {

/// Polls the loader until `expected` results arrive or `timeoutMs` elapses.
/// Returns everything collected. Sleeps 1 ms between polls to avoid spinning.
std::vector<MeshLoadResultCPU> CollectResults(AsyncMeshLoader& loader,
                                              size_t expected,
                                              int timeoutMs = 5000) {
    std::vector<MeshLoadResultCPU> collected;
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    while (collected.size() < expected
           && std::chrono::steady_clock::now() < deadline) {
        auto batch = loader.PollCompleted();
        for (auto& r : batch) {
            collected.push_back(std::move(r));
        }
        if (collected.size() < expected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return collected;
}

/// Writes a small valid cube .uzmesh into the system temp directory and
/// returns its path. Returns an empty string on failure.
std::string WriteTempCubeMesh(const char* fileName) {
    const Mesh cube = MeshBuilder::CreateCube(0.5f, 1);
    if (!cube.IsValid()) return {};

    const auto path =
        (std::filesystem::temp_directory_path() / fileName).string();
    if (!SaveMesh(path, cube)) return {};
    return path;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Round-trip: valid file → parsed mesh
// ─────────────────────────────────────────────────────────────────────────────

/// A cube saved via SaveMesh must come back from the worker with identical
/// vertex/index counts, and result.path must be the resultKey when provided.
TEST(AsyncMeshLoader, ValidFile_RoundTripsWithResultKey) {
    const std::string path = WriteTempCubeMesh("uz_test_async_mesh_cube.uzmesh");
    ASSERT_FALSE(path.empty()) << "failed to write temp .uzmesh fixture";

    const Mesh expected = MeshBuilder::CreateCube(0.5f, 1);

    AsyncMeshLoader loader(2);
    loader.Submit(path, "meshes/logical_key.uzmesh");

    auto results = CollectResults(loader, 1);
    ASSERT_EQ(results.size(), 1u) << "worker did not complete within timeout";

    EXPECT_EQ(results[0].path, "meshes/logical_key.uzmesh")
        << "resultKey must override the filesystem path in the result";
    ASSERT_TRUE(results[0].mesh.IsValid());
    EXPECT_EQ(results[0].mesh.GetVertexCount(), expected.GetVertexCount());
    EXPECT_EQ(results[0].mesh.GetIndexCount(), expected.GetIndexCount());

    std::remove(path.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Failure: missing file → invalid mesh result (no hang, no crash)
// ─────────────────────────────────────────────────────────────────────────────

/// A nonexistent path must still produce a completed result whose mesh is
/// invalid, so callers can release the pending slot instead of waiting forever.
TEST(AsyncMeshLoader, MissingFile_CompletesWithInvalidMesh) {
    AsyncMeshLoader loader(1);
    loader.Submit("uz_test_async_mesh_does_not_exist.uzmesh");

    auto results = CollectResults(loader, 1);
    ASSERT_EQ(results.size(), 1u) << "failure must still produce a result";
    EXPECT_EQ(results[0].path, "uz_test_async_mesh_does_not_exist.uzmesh");
    EXPECT_FALSE(results[0].mesh.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle: PendingCount drains, Shutdown is idempotent
// ─────────────────────────────────────────────────────────────────────────────

/// After all submissions complete, PendingCount must reach zero; calling
/// Shutdown twice (plus once more via the destructor) must not crash.
TEST(AsyncMeshLoader, PendingCountDrains_ShutdownIdempotent) {
    const std::string path = WriteTempCubeMesh("uz_test_async_mesh_drain.uzmesh");
    ASSERT_FALSE(path.empty()) << "failed to write temp .uzmesh fixture";

    AsyncMeshLoader loader(2);
    for (int i = 0; i < 4; ++i) {
        loader.Submit(path);
    }

    auto results = CollectResults(loader, 4);
    EXPECT_EQ(results.size(), 4u);
    EXPECT_EQ(loader.PendingCount(), 0u);

    loader.Shutdown();
    loader.Shutdown();  // second call must be a no-op

    std::remove(path.c_str());
}
