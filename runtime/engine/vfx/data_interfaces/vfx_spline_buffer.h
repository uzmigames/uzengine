#pragma once

/// VFX Spline Data Interface — GPU upload wrapper for `VfxSpline`.
///
/// Path-following effects sample a spline from the simulation shader. The CPU
/// `VfxSpline` (centripetal Catmull-Rom evaluator) bakes per-control-point
/// data into the GPU `VfxSplinePoint` layout each frame — position, unit
/// tangent, cumulative arc length. The compute shader reads the buffer via
/// the bindless `ResourceDescriptorHeap[]` indirection and looks up samples
/// with the helpers in `runtime/shaders/runtime/vfx/spline_di.hlsli`.
///
/// # Layout
///
/// `VfxSplinePoint` is exactly 32 bytes so a 64-point spline fits in 2 KiB
/// and a 256-point spline in 8 KiB. The packing matches the HLSL struct in
/// `spline_di.hlsli` byte-for-byte; static_asserts guard the size.
///
/// # Per-frame upload
///
/// Authors mutate `VfxSpline::controlPoints` whenever the gameplay layer
/// moves the spline. `RebuildAndUpload(spline, device)` rebuilds the
/// arc-length table, bakes one `VfxSplinePoint` per sample bucket, and
/// memcpy's the result into the upload-heap buffer.
///
/// @ref UE5 UNiagaraDataInterfaceSpline
/// @ref docs/engine/25-vfx/101-spline-data-interface.md

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
struct VfxSpline;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Default sample resolution baked into the GPU buffer. Higher values =
/// smoother sampling at the cost of more GPU memory. Must match the
/// arc-length resolution used by `VfxSpline::BuildArcLengthTable`.
static constexpr uint32_t kVfxSplineDefaultResolution = 64;

/// Hard upper bound on bake resolution. The HLSL helper does a linear
/// search for arc-length lookup; quadratic cost above ~1024 points starts
/// to hurt. Authors who need more should switch to a binary-search
/// variant (`SplineSampleArclen_Binary`) — out of scope for this task.
static constexpr uint32_t kVfxSplineMaxResolution = 1024;

// ─────────────────────────────────────────────────────────────────────────────
// VfxSplinePoint — CPU mirror of the HLSL struct in spline_di.hlsli
// ─────────────────────────────────────────────────────────────────────────────

/// Per-sample data uploaded to the GPU. Field order MUST match the HLSL
/// `VfxSplinePoint` declaration in `spline_di.hlsli`.
struct VfxSplinePoint {
    float positionX  = 0.0f;  ///< World-space sample position X (cm).
    float positionY  = 0.0f;  ///< World-space sample position Y.
    float positionZ  = 0.0f;  ///< World-space sample position Z.
    float arcLength  = 0.0f;  ///< Cumulative arc length from sample[0] (cm).
    float tangentX   = 0.0f;  ///< Unit tangent X.
    float tangentY   = 0.0f;  ///< Unit tangent Y.
    float tangentZ   = 0.0f;  ///< Unit tangent Z.
    float _pad       = 0.0f;  ///< Pads the struct to 32 bytes (HLSL alignment).
};

static_assert(sizeof(VfxSplinePoint) == 32,
              "VfxSplinePoint must be 32 bytes to match HLSL spline_di.hlsli "
              "VfxSplinePoint struct");

// ─────────────────────────────────────────────────────────────────────────────
// VfxSplineBuffer — GPU structured buffer wrapping a baked VfxSpline
// ─────────────────────────────────────────────────────────────────────────────

/// Owns the upload-heap structured buffer that holds `VfxSplinePoint`
/// samples. The `Spawn` / `Update` compute shaders read this buffer via
/// the bindless heap using the index returned by `GetBindlessSrvIndex`.
///
/// # Lifecycle
/// 1. `Create(device, capacity)` — allocates a `capacity * 32` byte
///    structured buffer in the Upload heap.
/// 2. `RebuildAndUpload(spline, device)` once per frame after the source
///    `VfxSpline::controlPoints` mutates.
/// 3. `Destroy(device)` at module shutdown.
///
/// # Thread safety
/// Not thread-safe. Lives next to one or more `Spline*Module` instances on
/// a single emitter and is touched from the render thread only.
struct VfxSplineBuffer {
    /// GPU `StructuredBuffer<VfxSplinePoint>[capacity]` in Upload memory.
    RHIBufferHandle buffer = RHIBufferHandle::Invalid();

    /// Number of slots allocated in `buffer`. Set by `Create()`.
    uint32_t capacity = 0;

    /// Number of slots actually written by the most recent
    /// `RebuildAndUpload()`. The HLSL helpers consume this so they know
    /// the valid sample range; capacity may be larger than count when the
    /// spline shrinks between frames.
    uint32_t pointCount = 0;

    /// Total arc length of the most recent baked spline. Mirrors
    /// `VfxSpline::totalLength` after `RebuildAndUpload` so the simulation
    /// shaders can convert arc-length to parametric `u`.
    float totalLength = 0.0f;

    // ── Lifecycle ────────────────────────────────────────────────────────

    /// Allocates the structured buffer with `capacity` slots.
    ///
    /// # Parameters
    /// - `device`:   RHI device. Must not be null.
    /// - `capacity`: Number of `VfxSplinePoint` slots. Must be in
    ///               `[2, kVfxSplineMaxResolution]`.
    ///
    /// # Returns
    /// `true` on success; `false` when the device rejects the allocation.
    [[nodiscard]] bool Create(IRHIDevice* device, uint32_t capacity);

    /// Releases the GPU buffer and resets handles. Safe to call on a
    /// never-created instance (no-op).
    void Destroy(IRHIDevice* device);

    /// Returns true once `Create()` has produced a valid handle.
    [[nodiscard]] bool IsValid() const noexcept;

    /// Returns the bindless SRV descriptor index assigned by the device,
    /// or `0xFFFFFFFFu` when the buffer has not been created.
    [[nodiscard]] uint32_t GetBindlessSrvIndex(IRHIDevice* device) const noexcept;

    // ── Per-frame upload ─────────────────────────────────────────────────

    /// Rebuilds the spline's arc-length table on the CPU, bakes one
    /// `VfxSplinePoint` per uniform sample bucket, and uploads the result
    /// to the GPU via MapBuffer / memcpy / UnmapBuffer.
    ///
    /// # Parameters
    /// - `spline`: Source CPU spline. Must have at least 2 control points
    ///             for any work to happen; otherwise `pointCount = 0` and
    ///             the function returns `true` (legal "empty" spline).
    /// - `device`: RHI device for the map/unmap pair.
    ///
    /// # Returns
    /// `true` on success; `false` if the buffer is not created or the
    /// device fails to map the upload heap.
    [[nodiscard]] bool RebuildAndUpload(const VfxSpline& spline,
                                         IRHIDevice* device);
};

} // namespace uz
