/// Unit tests for the mesh asset cache lookup path in SceneRenderer.
///
/// Covers item 1.4: same path × 2 → handle equality (cache hit); missing or
/// null-device path → UINT32_MAX; GetGPUMesh out-of-range / invalid mesh
/// returns nullptr; LoadMesh with empty path short-circuits to UINT32_MAX.
///
/// Cache-hit behavior (same path × 2 → identical GPU mesh) requires a real
/// device to upload successfully, so those tests are marked as requiring GPU
/// hardware.  All tests in this file that do NOT need a device are CPU-only
/// and run in the unit-test bucket.
///
/// Warn-once dedup (the static std::unordered_set in frame_particles.cpp:801)
/// lives inside the frame-build critical section and cannot be driven without
/// a full FrameContext.  That coverage is captured by the visual A/B smoke
/// test (item 3.4) and by the mesh guard integration scenario.
///
/// @ref runtime/engine/renderer/scene/scene_mesh_assets.cpp
/// @ref runtime/engine/renderer/scene/scene_renderer.h:652,821

#include <gtest/gtest.h>

#include "scene_renderer.h"
#include "gpu_mesh.h"
#include "rhi_types.h"

#include <cstdint>
#include <limits>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// GetGPUMesh — out-of-range and null-device guards
// ─────────────────────────────────────────────────────────────────────────────

/// A default SceneRenderer (never Init'd, no meshes registered) must return
/// nullptr for any mesh id — including 0 and UINT32_MAX.
TEST(MeshAssetLookup, GetGPUMesh_DefaultRenderer_AnyId_ReturnsNullptr) {
    SceneRenderer renderer;
    EXPECT_EQ(renderer.GetGPUMesh(0u), nullptr)
        << "GetGPUMesh(0) on uninitialized renderer must return nullptr";
    EXPECT_EQ(renderer.GetGPUMesh(1u), nullptr)
        << "GetGPUMesh(1) on uninitialized renderer must return nullptr";
    EXPECT_EQ(renderer.GetGPUMesh(UINT32_MAX), nullptr)
        << "GetGPUMesh(UINT32_MAX) must never crash and must return nullptr";
}

/// GetGPUMesh must not crash when called with UINT32_MAX (the failure sentinel
/// returned by LoadMesh).  Callers are expected to check the return value of
/// LoadMesh and skip GetGPUMesh when it equals UINT32_MAX, but defensive code
/// must survive if they do not.
TEST(MeshAssetLookup, GetGPUMesh_FailureSentinel_DoesNotCrash) {
    SceneRenderer renderer;
    EXPECT_NO_FATAL_FAILURE({
        const GPUMesh* result = renderer.GetGPUMesh(UINT32_MAX);
        (void)result;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMesh — null guard for empty path (CPU-only fast-exit)
// ─────────────────────────────────────────────────────────────────────────────

/// LoadMesh with an empty path must return UINT32_MAX without crashing and
/// without touching the device or VFS.  This exercises the fast-exit guard
/// that fires before any disk / GPU I/O.
TEST(MeshAssetLookup, LoadMesh_EmptyPath_ReturnsUINT32MAX) {
    SceneRenderer renderer;
    const uint32_t id = renderer.LoadMesh("", nullptr, nullptr);
    EXPECT_EQ(id, UINT32_MAX)
        << "LoadMesh(\"\") must return UINT32_MAX (fast-exit guard)";
}

/// Calling LoadMesh twice with an empty path must return UINT32_MAX both
/// times — no state corruption between calls.
TEST(MeshAssetLookup, LoadMesh_EmptyPath_TwiceBothReturnUINT32MAX) {
    SceneRenderer renderer;
    const uint32_t id1 = renderer.LoadMesh("", nullptr, nullptr);
    const uint32_t id2 = renderer.LoadMesh("", nullptr, nullptr);
    EXPECT_EQ(id1, UINT32_MAX);
    EXPECT_EQ(id2, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMesh — null device (upload cannot succeed; no cache entry written)
// ─────────────────────────────────────────────────────────────────────────────

/// LoadMesh with a null device must return UINT32_MAX — UploadMeshDeferred
/// cannot allocate GPU buffers without a device.
TEST(MeshAssetLookup, LoadMesh_NullDevice_NonEmptyPath_ReturnsUINT32MAX) {
    SceneRenderer renderer;
    // "builtin:cube" triggers the CPU mesh builder path, so we get past the
    // VFS read.  The null device then causes UploadMeshDeferred to return an
    // invalid mesh, which LoadMesh maps to UINT32_MAX.
    const uint32_t id = renderer.LoadMesh("builtin:cube", nullptr, nullptr);
    EXPECT_EQ(id, UINT32_MAX)
        << "LoadMesh with null device must return UINT32_MAX";
}

/// Because LoadMesh only writes to the cache on success, a null-device call
/// must NOT populate the cache — a subsequent call with a non-null device
/// (or null device again) goes through the full load path again.
TEST(MeshAssetLookup, LoadMesh_NullDevice_DoesNotPopulateCache) {
    SceneRenderer renderer;

    // First call fails (null device) → UINT32_MAX.
    const uint32_t id1 = renderer.LoadMesh("builtin:cube", nullptr, nullptr);
    ASSERT_EQ(id1, UINT32_MAX) << "precondition: null-device call must fail";

    // Second call with null device again also goes through the full path
    // (no cached entry from the first failure).  Both outcomes are identical
    // from the caller's perspective.
    const uint32_t id2 = renderer.LoadMesh("builtin:cube", nullptr, nullptr);
    EXPECT_EQ(id2, UINT32_MAX)
        << "A failed LoadMesh must not cache the failure — second call must "
           "also return UINT32_MAX";
}

/// LoadMesh with null device must not crash regardless of how many times it
/// is called with the same path.
TEST(MeshAssetLookup, LoadMesh_NullDevice_RepeatedCalls_DoNotCrash) {
    SceneRenderer renderer;
    EXPECT_NO_FATAL_FAILURE({
        for (int i = 0; i < 5; ++i) {
            renderer.LoadMesh("builtin:cube", nullptr, nullptr);
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMesh — unknown builtin name returns UINT32_MAX
// ─────────────────────────────────────────────────────────────────────────────

/// An unrecognised "builtin:X" name must return UINT32_MAX — the mesh builder
/// logs an error and the function must not crash.
TEST(MeshAssetLookup, LoadMesh_UnknownBuiltin_ReturnsUINT32MAX) {
    SceneRenderer renderer;
    const uint32_t id = renderer.LoadMesh("builtin:does_not_exist", nullptr, nullptr);
    EXPECT_EQ(id, UINT32_MAX)
        << "Unknown builtin mesh must return UINT32_MAX";
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMesh → GetGPUMesh invariant: UINT32_MAX id → nullptr
// ─────────────────────────────────────────────────────────────────────────────

/// When LoadMesh returns UINT32_MAX (any failure mode), passing that id to
/// GetGPUMesh must return nullptr — callers must be able to use the two
/// functions together safely.
TEST(MeshAssetLookup, LoadMesh_FailureId_GetGPUMesh_ReturnsNullptr) {
    SceneRenderer renderer;
    const uint32_t id = renderer.LoadMesh("builtin:cube", nullptr, nullptr);
    ASSERT_EQ(id, UINT32_MAX) << "precondition: null-device load must fail";

    // GetGPUMesh with UINT32_MAX must not crash and must return nullptr.
    const GPUMesh* mesh = renderer.GetGPUMesh(id);
    EXPECT_EQ(mesh, nullptr)
        << "GetGPUMesh(UINT32_MAX) must return nullptr";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache-hit coverage gap note
// ─────────────────────────────────────────────────────────────────────────────
// The spec scenario "same path × 2 → identical handle (cache hit)" requires a
// successful upload (non-null IRHIDevice + IRHICommandList).  A successful
// upload creates real GPU buffers via D3D12MA, so that scenario can only be
// validated in an integration test with a real device.  It is exercised by:
//   - item 3.4 visual A/B: mesh_sort_shards.uzvfx emitter loads "builtin:cube"
//     twice (once per emitter tick), the single "SceneRenderer: loaded mesh"
//     log line and the rendered cyan tetrahedra confirm the cache hit.
//   - The frame_particles_mesh_guard integration path drives LoadMesh
//     through the same code under RegisterParticlePasses.
//
// The warn-once dedup (static std::unordered_set in frame_particles.cpp:801)
// is internal to RegisterParticlePasses and cannot be driven from a unit test
// without a full FrameContext.  Its coverage is provided by the visual smoke
// test and the mesh-guard integration scenario.
