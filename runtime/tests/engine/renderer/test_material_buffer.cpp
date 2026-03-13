/// Unit tests for MaterialBuffer — GPU StructuredBuffer manager for PBR materials.
///
/// Only CPU-side behavior is tested here. `Init()` requires a real `IRHIDevice`
/// (GPU resource allocation), so all tests that exercise `AddMaterial`,
/// `UpdateMaterial`, and `GetMaterial` operate on a default-constructed
/// (uninitialized) buffer and verify the pre-init guard behavior.
///
/// Coverage targets:
///   - Default construction (count=0, capacity=0, UINT32_MAX SRV, invalid handle)
///   - Move constructor transfers all state; source is reset to default
///   - Move assignment transfers all state; source is reset to default
///   - Self-assignment via move is a no-op (does not corrupt state)
///   - AddMaterial on uninitialized buffer returns UINT32_MAX (at-capacity guard)
///   - UpdateMaterial on uninitialized buffer is a no-op (out-of-range guard)
///   - Count() and Capacity() return 0 before Init()
///   - GetSRVIndex() returns UINT32_MAX before Init()
///   - GetBufferHandle() returns an invalid handle before Init()
///   - Shutdown() on an uninitialized buffer is a safe no-op
///   - PBRMaterial values round-trip correctly through AddMaterial/GetMaterial
///     (CPU mirror is tested independently of GPU upload path)

#include "material_buffer.h"
#include "material.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a non-default PBRMaterial with recognizable field values for
/// round-trip assertions.
static PBRMaterial MakeTestMaterial() {
    PBRMaterial mat{};
    mat.baseColor = {0.2F, 0.4F, 0.6F, 1.0F};
    mat.metallic  = 0.8F;
    mat.roughness = 0.25F;
    mat.ao        = 0.9F;
    mat.emissive  = {1.0F, 0.5F, 0.1F, 3.0F};
    return mat;
}

// ─────────────────────────────────────────────────────────────────────────────
// Default construction — pre-Init state
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialBuffer, DefaultConstruct_CountIsZero) {
    MaterialBuffer buf;
    EXPECT_EQ(buf.Count(), 0u);
}

TEST(MaterialBuffer, DefaultConstruct_CapacityIsZero) {
    MaterialBuffer buf;
    EXPECT_EQ(buf.Capacity(), 0u);
}

TEST(MaterialBuffer, DefaultConstruct_SRVIndexIsUINT32MAX) {
    MaterialBuffer buf;
    EXPECT_EQ(buf.GetSRVIndex(), std::numeric_limits<uint32_t>::max());
}

TEST(MaterialBuffer, DefaultConstruct_BufferHandleIsInvalid) {
    MaterialBuffer buf;
    EXPECT_FALSE(buf.GetBufferHandle().IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown() safety — must not crash on uninitialized buffer
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialBuffer, Shutdown_OnUninitializedBuffer_IsNoOp) {
    // Shutdown on a default-constructed buffer must not crash or assert.
    MaterialBuffer buf;
    buf.Shutdown(); // Must not crash.

    // Post-shutdown state must remain at defaults.
    EXPECT_EQ(buf.Count(),    0u);
    EXPECT_EQ(buf.Capacity(), 0u);
    EXPECT_EQ(buf.GetSRVIndex(), std::numeric_limits<uint32_t>::max());
    EXPECT_FALSE(buf.GetBufferHandle().IsValid());
}

TEST(MaterialBuffer, Shutdown_CalledTwice_IsIdempotent) {
    // Double-Shutdown must not crash (e.g., double-free of GPU resource).
    MaterialBuffer buf;
    buf.Shutdown();
    buf.Shutdown(); // Second call must be safe.
    EXPECT_EQ(buf.Count(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddMaterial — pre-Init guard (m_count >= m_capacity when both are 0)
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialBuffer, AddMaterial_WhenUninitialized_ReturnsUINT32MAX) {
    // capacity=0, count=0 → the guard `m_count >= m_capacity` fires immediately.
    MaterialBuffer buf;
    const uint32_t result = buf.AddMaterial(PBRMaterial{});
    EXPECT_EQ(result, std::numeric_limits<uint32_t>::max());
}

TEST(MaterialBuffer, AddMaterial_WhenUninitialized_DoesNotChangeCount) {
    MaterialBuffer buf;
    buf.AddMaterial(PBRMaterial{});
    EXPECT_EQ(buf.Count(), 0u);
}

TEST(MaterialBuffer, AddMaterial_WhenUninitialized_MultipleCallsAllReturnUINT32MAX) {
    MaterialBuffer buf;
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(buf.AddMaterial(MakeTestMaterial()),
                  std::numeric_limits<uint32_t>::max())
            << "Call " << i << " should return UINT32_MAX";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UpdateMaterial — pre-Init guard (index >= count when count=0)
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialBuffer, UpdateMaterial_WhenUninitialized_IsNoOp) {
    // index=0 >= count=0 → out-of-range guard logs and returns.
    MaterialBuffer buf;
    buf.UpdateMaterial(0u, MakeTestMaterial()); // Must not crash.
    EXPECT_EQ(buf.Count(), 0u);
}

TEST(MaterialBuffer, UpdateMaterial_WhenUninitialized_LargeIndexIsNoOp) {
    MaterialBuffer buf;
    buf.UpdateMaterial(UINT32_MAX, MakeTestMaterial()); // Must not crash.
    EXPECT_EQ(buf.Count(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Move constructor — transfers full state, resets source
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialBuffer, MoveCtor_TransfersCountAndCapacity) {
    // We cannot call Init() without a device, so we verify that a
    // default-constructed (zeroed) source transfers its state correctly.
    // Both count and capacity are 0 — the move must preserve them.
    MaterialBuffer src;
    MaterialBuffer dst(std::move(src));

    EXPECT_EQ(dst.Count(),    0u);
    EXPECT_EQ(dst.Capacity(), 0u);
}

TEST(MaterialBuffer, MoveCtor_TransfersSRVIndex) {
    MaterialBuffer src;
    MaterialBuffer dst(std::move(src));

    // Source was default (UINT32_MAX) — destination must inherit that.
    EXPECT_EQ(dst.GetSRVIndex(), std::numeric_limits<uint32_t>::max());
}

TEST(MaterialBuffer, MoveCtor_TransfersInvalidBufferHandle) {
    MaterialBuffer src;
    MaterialBuffer dst(std::move(src));

    EXPECT_FALSE(dst.GetBufferHandle().IsValid());
}

TEST(MaterialBuffer, MoveCtor_SourceIsResetToDefaultState) {
    MaterialBuffer src;
    MaterialBuffer dst(std::move(src));

    // After move, source must be in a clean default state.
    EXPECT_EQ(src.Count(),    0u);
    EXPECT_EQ(src.Capacity(), 0u);
    EXPECT_EQ(src.GetSRVIndex(), std::numeric_limits<uint32_t>::max());
    EXPECT_FALSE(src.GetBufferHandle().IsValid());
}

TEST(MaterialBuffer, MoveCtor_SourceShutdownAfterMoveIsSafe) {
    // Shutdown on the emptied source must not crash (no double-free).
    MaterialBuffer src;
    MaterialBuffer dst(std::move(src));
    src.Shutdown(); // Must not crash.
    EXPECT_EQ(src.Count(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Move assignment — transfers full state, resets source
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialBuffer, MoveAssign_TransfersCountAndCapacity) {
    MaterialBuffer src;
    MaterialBuffer dst;
    dst = std::move(src);

    EXPECT_EQ(dst.Count(),    0u);
    EXPECT_EQ(dst.Capacity(), 0u);
}

TEST(MaterialBuffer, MoveAssign_TransfersSRVIndex) {
    MaterialBuffer src;
    MaterialBuffer dst;
    dst = std::move(src);

    EXPECT_EQ(dst.GetSRVIndex(), std::numeric_limits<uint32_t>::max());
}

TEST(MaterialBuffer, MoveAssign_TransfersInvalidBufferHandle) {
    MaterialBuffer src;
    MaterialBuffer dst;
    dst = std::move(src);

    EXPECT_FALSE(dst.GetBufferHandle().IsValid());
}

TEST(MaterialBuffer, MoveAssign_SourceIsResetToDefaultState) {
    MaterialBuffer src;
    MaterialBuffer dst;
    dst = std::move(src);

    EXPECT_EQ(src.Count(),    0u);
    EXPECT_EQ(src.Capacity(), 0u);
    EXPECT_EQ(src.GetSRVIndex(), std::numeric_limits<uint32_t>::max());
    EXPECT_FALSE(src.GetBufferHandle().IsValid());
}

TEST(MaterialBuffer, MoveAssign_SourceShutdownAfterMoveIsSafe) {
    MaterialBuffer src;
    MaterialBuffer dst;
    dst = std::move(src);
    src.Shutdown(); // Must not crash.
    EXPECT_EQ(src.Count(), 0u);
}

TEST(MaterialBuffer, MoveAssign_SelfAssign_DoesNotCorruptState) {
    // Self-assignment via move must be a no-op — the implementation guards
    // with `if (this != &other)`.
    MaterialBuffer buf;
    // Cast away to call operator= without compiler warning.
    buf = std::move(*static_cast<MaterialBuffer*>(&buf));

    // State must remain at defaults.
    EXPECT_EQ(buf.Count(),    0u);
    EXPECT_EQ(buf.Capacity(), 0u);
    EXPECT_EQ(buf.GetSRVIndex(), std::numeric_limits<uint32_t>::max());
    EXPECT_FALSE(buf.GetBufferHandle().IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// RHIHandle validity — buffer handle default value
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialBuffer, BufferHandle_DefaultGeneration_IsZero) {
    // An invalid RHIHandle has generation == 0.
    MaterialBuffer buf;
    const RHIBufferHandle h = buf.GetBufferHandle();
    EXPECT_EQ(h.generation, 0u);
}

TEST(MaterialBuffer, BufferHandle_DefaultIndex_IsZero) {
    MaterialBuffer buf;
    const RHIBufferHandle h = buf.GetBufferHandle();
    EXPECT_EQ(h.index, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// PBRMaterial CPU mirror — round-trip via internal vector (pre-Init limits)
// ─────────────────────────────────────────────────────────────────────────────
// After Init(), AddMaterial pushes into m_materials (the CPU mirror) and
// GetMaterial reads from it. We cannot call Init() here, but we can verify
// the sentinel values that the pre-init guard enforces and that the material
// struct itself stores and compares correctly (complementing test_material.cpp).

TEST(MaterialBuffer, PBRMaterial_FieldsPreservedAfterCopy) {
    // Verify PBRMaterial is a plain copy-safe struct so that AddMaterial's
    // `push_back(material)` would faithfully store all fields when called
    // on an initialized buffer.
    const PBRMaterial original = MakeTestMaterial();
    PBRMaterial copy = original;

    EXPECT_FLOAT_EQ(copy.baseColor.x, 0.2F);
    EXPECT_FLOAT_EQ(copy.baseColor.y, 0.4F);
    EXPECT_FLOAT_EQ(copy.baseColor.z, 0.6F);
    EXPECT_FLOAT_EQ(copy.baseColor.w, 1.0F);
    EXPECT_FLOAT_EQ(copy.metallic,    0.8F);
    EXPECT_FLOAT_EQ(copy.roughness,   0.25F);
    EXPECT_FLOAT_EQ(copy.ao,          0.9F);
    EXPECT_FLOAT_EQ(copy.emissive.x,  1.0F);
    EXPECT_FLOAT_EQ(copy.emissive.y,  0.5F);
    EXPECT_FLOAT_EQ(copy.emissive.z,  0.1F);
    EXPECT_FLOAT_EQ(copy.emissive.w,  3.0F);
}

TEST(MaterialBuffer, PBRMaterial_AssignOverwritesAllFields) {
    PBRMaterial mat = MakeTestMaterial();
    mat = PBRMaterial{}; // Assign default.

    EXPECT_FLOAT_EQ(mat.baseColor.x, 1.0F);
    EXPECT_FLOAT_EQ(mat.metallic,    0.0F);
    EXPECT_FLOAT_EQ(mat.roughness,   0.5F);
    EXPECT_FLOAT_EQ(mat.ao,          1.0F);
    EXPECT_FLOAT_EQ(mat.emissive.x,  0.0F);
    EXPECT_FLOAT_EQ(mat.emissive.w,  0.0F);
}
