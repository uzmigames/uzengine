#pragma once

/// VFX Attractor Buffer — per-frame structured buffer for multiple attractors.
///
/// Manages a GPU structured buffer holding up to kMaxAttractors instances of
/// AttractorData, uploaded once per frame via a CPU-writable Upload heap buffer.
/// The compute shader reads this buffer to apply forces from all active attractors
/// to each particle.
///
/// # Usage Pattern
/// ```cpp
/// AttractorBuffer buf;
/// buf.Create(device);
/// // Each frame:
/// buf.Clear();
/// buf.AddAttractor({ .positionX = 10.f, .strength = 20.f, ... });
/// buf.UploadToGPU(device);
/// // Bind buf.buffer as SRV to the simulation compute shader.
/// ```
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Maximum attractors per emitter. Must match kMaxAttractors in the shader.
static constexpr uint32_t kMaxAttractors = 8;

// ---------------------------------------------------------------------------
// AttractorData — CPU-side mirror of AttractorParams in vfx_attractors.hlsli
// ---------------------------------------------------------------------------

/// Per-attractor parameters uploaded to the GPU each frame.
///
/// Layout is byte-identical to the HLSL struct AttractorParams (32 bytes).
/// Field order and padding MUST NOT be changed without also updating the shader.
///
/// # HLSL Mapping
/// ```hlsl
/// struct AttractorParams {
///     float3 position;        // bytes 0-11
///     float  strength;        // bytes 12-15
///     float  radius;          // bytes 16-19
///     uint   falloffType;     // bytes 20-23  (0=Constant,1=Linear,2=InverseSquare)
///     uint   killOnArrival;   // bytes 24-27  (0=false, 1=true)
///     float  killDistance;    // bytes 28-31
/// };
/// ```
struct AttractorData {
    float    positionX    = 0.0f;  ///< World-space attractor X position (cm).
    float    positionY    = 0.0f;  ///< World-space attractor Y position (cm).
    float    positionZ    = 0.0f;  ///< World-space attractor Z position (cm).
    float    strength     = 10.0f; ///< Peak force magnitude (units/s^2).
    float    radius       = 5.0f;  ///< Linear falloff effective radius (cm).
    uint32_t falloffType  = 1;     ///< Falloff mode: 0=Constant, 1=Linear, 2=InverseSquare.
    uint32_t killOnArrival = 0;    ///< 1 if particles that reach this attractor are expired.
    float    killDistance = 0.05f; ///< Distance threshold below which kill-on-arrival fires.
};

static_assert(sizeof(AttractorData) == 32,
              "AttractorData must be 32 bytes to match HLSL AttractorParams");

// ---------------------------------------------------------------------------
// AttractorBuffer — GPU structured buffer for attractor parameters
// ---------------------------------------------------------------------------

/// Manages a GPU structured buffer containing up to kMaxAttractors attractors.
///
/// The buffer is allocated with Upload memory so that the CPU can write attractor
/// data directly each frame via MapBuffer/memcpy/UnmapBuffer without any staging.
/// The compute shader binds it as a StructuredBuffer<AttractorParams> SRV.
///
/// # Lifetime
/// Call Create() once at emitter construction. Call Destroy() on shutdown.
/// Each frame: Clear() -> AddAttractor() -> UploadToGPU().
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
struct AttractorBuffer {
    /// GPU StructuredBuffer<AttractorParams>[kMaxAttractors] — Upload heap.
    RHIBufferHandle buffer = RHIBufferHandle::Invalid();

    /// Number of attractors currently populated in `attractors[]`.
    uint32_t activeCount = 0;

    /// CPU-side attractor array, copied to the GPU each frame by UploadToGPU().
    AttractorData attractors[kMaxAttractors] = {};

    // -- Lifecycle -----------------------------------------------------------

    /// Allocates the GPU structured buffer (kMaxAttractors * 32 = 256 bytes).
    ///
    /// Uses Upload memory so that the CPU can write each frame without staging.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    ///
    /// # Returns
    /// `true` on success, `false` if buffer allocation fails.
    ///
    /// # Preconditions
    /// - `device` must not be null.
    /// - Must not already be allocated (asserts in debug). Call Destroy() first.
    [[nodiscard]] bool Create(IRHIDevice* device);

    /// Releases the GPU buffer and resets all state.
    ///
    /// Safe to call on a never-created or already-destroyed instance (no-op
    /// when device is null or buffer is invalid).
    ///
    /// # Parameters
    /// - `device`: RHI device for deallocation. May be null (no-op).
    void Destroy(IRHIDevice* device);

    /// Returns true if the GPU buffer has been successfully allocated.
    [[nodiscard]] bool IsValid() const noexcept;

    // -- Per-frame API -------------------------------------------------------

    /// Adds an attractor to the per-frame list.
    ///
    /// Copies `data` into the next free slot in `attractors[]` and increments
    /// `activeCount`. Returns false (no-op) if the list is already at capacity
    /// (kMaxAttractors).
    ///
    /// # Parameters
    /// - `data`: Attractor parameters to add.
    ///
    /// # Returns
    /// `true` if the attractor was added, `false` if the list is full.
    [[nodiscard]] bool AddAttractor(const AttractorData& data);

    /// Resets the active attractor count to zero without freeing any memory.
    ///
    /// Call once at the start of each frame before adding attractors.
    void Clear() noexcept;

    /// Returns the number of active attractors set this frame.
    [[nodiscard]] uint32_t GetActiveCount() const noexcept { return activeCount; }

    /// Uploads the CPU attractor array to the GPU buffer via MapBuffer/memcpy/UnmapBuffer.
    ///
    /// Maps the Upload-heap buffer, writes all activeCount elements (plus zeroes
    /// the remaining slots to avoid stale GPU reads), then unmaps. Must be called
    /// once per frame after all AddAttractor() calls and before the simulation
    /// compute dispatch that reads this buffer.
    ///
    /// # Parameters
    /// - `device`: RHI device providing MapBuffer/UnmapBuffer. Must not be null.
    ///
    /// # Preconditions
    /// - `IsValid()` must be true.
    /// - `device` must not be null.
    void UploadToGPU(IRHIDevice* device);
};

} // namespace uz
