#pragma once

/// GPU->CPU readback for sub-emitter particle trigger flags.
///
/// After the VFX simulation dispatch writes `SubEmitterFlags[i] = 1u` for
/// triggered particles, a small compact compute pass gathers the triggered
/// particle states (position, velocity, color, age) into a dense buffer
/// capped at `kMaxReadbackParticles` entries. That compact buffer is then
/// copied to a triple-buffered Readback heap buffer so the CPU can map it
/// the following frame without stalling the GPU.
///
/// # Latency
/// 1-frame latency by design. Frame N's triggered particles are readable
/// by the CPU on frame N+1. This avoids any GPU/CPU synchronisation stall.
///
/// # Triple Buffering
/// Three readback buffers rotate each frame (write index 0->1->2->0).
/// The CPU always maps the buffer that is at least 2 frames old, which is
/// guaranteed to have completed its GPU copy by the time the CPU reads it.
///
/// # Data Flow
/// 1. Simulation CS writes `SubEmitterFlags[i] = 1u` (existing shader snippet).
/// 2. Compact CS scans flags, appends triggered particle data to `compactBuffer`
///    via `InterlockedAdd` on an atomic counter (capped at kMaxReadbackParticles).
/// 3. `IssueCopy()` copies `compactBuffer` and `compactCount` to the current
///    readback slot, then advances the write index.
/// 4. Next frame, `GatherTriggered()` maps the oldest readback slot, reads
///    the count and payload data, and returns a `SubEmitterReadback`.
///
/// @ref GPU-driven particle sub-emission with 1-frame-latency readback

#include "engine/rhi/rhi_types.h"
#include "engine/vfx/modules/sub_emitter_module.h"

#include <cstdint>
#include <string_view>

namespace uz {

class IRHIDevice;
class IRHICommandList;
struct ParticleAttributeSet;

// ---------------------------------------------------------------------------
// SubEmitterGPUPayload -- GPU-side compact triggered particle data
// ---------------------------------------------------------------------------

/// Per-particle payload written by the compact compute pass on the GPU.
///
/// Layout is byte-identical to the HLSL struct of the same name. Three
/// float4 rows (48 bytes total) for cache-friendly 16-byte-aligned access.
///
/// # HLSL Mapping
/// ```hlsl
/// struct SubEmitterGPUPayload {
///     float3 position; float _pad0;    // 16 bytes
///     float3 velocity; float age;      // 16 bytes
///     float4 color;                    // 16 bytes
/// };
/// ```
struct alignas(16) SubEmitterGPUPayload {
    float posX   = 0.0f;   ///< World-space X position (cm).
    float posY   = 0.0f;   ///< World-space Y position (cm).
    float posZ   = 0.0f;   ///< World-space Z position (cm).
    float _pad0  = 0.0f;   ///< Padding for 16-byte HLSL alignment.

    float velX   = 0.0f;   ///< Velocity X component (cm/s).
    float velY   = 0.0f;   ///< Velocity Y component (cm/s).
    float velZ   = 0.0f;   ///< Velocity Z component (cm/s).
    float age    = 0.0f;   ///< Particle age in seconds at trigger time.

    float colorR = 1.0f;   ///< Linear-space red channel.
    float colorG = 1.0f;   ///< Linear-space green channel.
    float colorB = 1.0f;   ///< Linear-space blue channel.
    float colorA = 1.0f;   ///< Alpha channel.
};

static_assert(sizeof(SubEmitterGPUPayload) == 48,
              "SubEmitterGPUPayload must be 48 bytes to match HLSL struct layout");
static_assert(alignof(SubEmitterGPUPayload) == 16,
              "SubEmitterGPUPayload must be 16-byte aligned for GPU structured buffer");

// ---------------------------------------------------------------------------
// SubEmitterReadbackBuffer -- triple-buffered GPU->CPU readback
// ---------------------------------------------------------------------------

/// Triple-buffered readback manager for sub-emitter trigger data.
///
/// Owns the GPU-side flag buffer (UAV), the compact payload buffer (UAV),
/// an atomic counter (UAV), and three Readback-heap copies that rotate
/// each frame. The CPU reads the oldest readback buffer while the GPU
/// writes to the newest, with no synchronisation stall.
///
/// # Lifetime
/// Call `Create()` once when the emitter is initialised. Call `Destroy()`
/// on shutdown. Between those, call `IssueCopy()` after each simulation
/// dispatch and `GatherTriggered()` before the next `Execute()`.
///
/// # Thread Safety
/// Not thread-safe. Intended for single-threaded render thread use.
///
/// # Memory Budget
/// Per-emitter overhead:
/// - Flag buffer: `capacity * 4` bytes (Default heap).
/// - Compact buffer: `kMaxReadbackParticles * 48` bytes (Default heap).
/// - Compact count: 4 bytes (Default heap).
/// - 3x readback payload buffers: `kMaxReadbackParticles * 48` bytes each (Readback heap).
/// - 3x readback count buffers: 4 bytes each (Readback heap).
/// Example (50K particles): ~200 KB flags + ~36 KB compact + ~36 KB readback = ~272 KB total.
struct SubEmitterReadbackBuffer {
    /// Number of rotating readback slots. Three ensures the CPU reads data
    /// that is at least 2 frames old (safely past any GPU queue depth).
    static constexpr uint32_t kBufferCount = 3;

    /// Maximum triggered particles read back per frame. Prevents excessive
    /// data transfer when many particles trigger simultaneously. Payloads
    /// beyond this cap are silently discarded for the current frame.
    static constexpr uint32_t kMaxReadbackParticles = 256;

    // -- GPU-side resources (Default heap) -----------------------------------

    /// Per-particle flag buffer (RWBuffer<uint>, Default heap).
    /// Written by the simulation compute shader: `SubEmitterFlags[i] = 1u`.
    /// Read by the compact compute pass to identify triggered particles.
    /// Size: `particleCapacity * sizeof(uint32_t)` bytes.
    RHIBufferHandle flagBuffer = RHIBufferHandle::Invalid();

    /// Compact payload buffer (RWStructuredBuffer<SubEmitterGPUPayload>, Default heap).
    /// The compact compute pass appends triggered particle data here via
    /// `InterlockedAdd` on `compactCount`. Size: kMaxReadbackParticles * 48 bytes.
    RHIBufferHandle compactBuffer = RHIBufferHandle::Invalid();

    /// Atomic counter for the compact buffer (RWBuffer<uint>, Default heap).
    /// Incremented by `InterlockedAdd` in the compact pass. Reset to 0
    /// by the CPU via a small upload copy before each compact dispatch.
    RHIBufferHandle compactCount = RHIBufferHandle::Invalid();

    // -- Readback-heap resources (triple-buffered) ---------------------------

    /// Readback copies of the compact payload buffer, one per frame-in-flight.
    /// Each holds `kMaxReadbackParticles * sizeof(SubEmitterGPUPayload)` bytes.
    RHIBufferHandle readbackPayloadBuffers[kBufferCount] = {
        RHIBufferHandle::Invalid(),
        RHIBufferHandle::Invalid(),
        RHIBufferHandle::Invalid(),
    };

    /// Readback copies of the atomic counter, one per frame-in-flight.
    /// Each holds `sizeof(uint32_t)` bytes.
    RHIBufferHandle readbackCountBuffers[kBufferCount] = {
        RHIBufferHandle::Invalid(),
        RHIBufferHandle::Invalid(),
        RHIBufferHandle::Invalid(),
    };

    /// Current write index into the readback ring (0, 1, or 2).
    /// Advanced by `IssueCopy()` each frame.
    uint32_t currentWriteIndex = 0;

    /// Particle capacity this buffer was created for.
    uint32_t particleCapacity = 0;

    /// Number of frames since `Create()`. Used to skip readback during
    /// the first `kBufferCount` frames when the readback buffers do not
    /// yet contain valid data.
    uint32_t framesSinceCreate = 0;

    // -- Lifecycle -----------------------------------------------------------

    /// Allocates all GPU and readback buffers for the given particle capacity.
    ///
    /// Creates the flag buffer (Default, UAV + CopySrc), compact payload
    /// buffer (Default, UAV + CopySrc), compact count (Default, UAV + CopySrc),
    /// and three readback slots (Readback, CopyDst) for both payload and count.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    /// - `capacity`: Maximum number of particles in the emitter. Must be > 0.
    ///
    /// # Returns
    /// `true` on success, `false` if any allocation fails. On failure, all
    /// partially allocated buffers are cleaned up via `Destroy()`.
    ///
    /// # Preconditions
    /// - `device` must not be null (fatal assert in debug).
    /// - `capacity` must be > 0 (fatal assert in debug).
    /// - Must not already be allocated (`IsValid()` must be false).
    [[nodiscard]] bool Create(IRHIDevice* device, uint32_t capacity);

    /// Releases all GPU and readback buffers and resets all state.
    ///
    /// Safe to call on a never-created or already-destroyed instance.
    /// No-op when `device` is null.
    ///
    /// # Parameters
    /// - `device`: RHI device for deallocation. May be null (no-op).
    void Destroy(IRHIDevice* device);

    /// Returns `true` if the buffer has been successfully allocated.
    ///
    /// Checks that the flag buffer and at least the first readback slot
    /// are valid handles.
    [[nodiscard]] bool IsValid() const noexcept;

    // -- Per-frame GPU operations --------------------------------------------

    /// Returns the flag buffer handle for binding as a UAV in the simulation CS.
    ///
    /// The compute shader writes `SubEmitterFlags[i] = 1u` into this buffer.
    [[nodiscard]] RHIBufferHandle GetFlagBuffer() const noexcept;

    /// Returns the compact payload buffer handle for binding as a UAV in the compact CS.
    [[nodiscard]] RHIBufferHandle GetCompactBuffer() const noexcept;

    /// Returns the compact count buffer handle for binding as a UAV in the compact CS.
    [[nodiscard]] RHIBufferHandle GetCompactCount() const noexcept;

    /// Copies compact payload and count from Default heap to the current
    /// readback slot, then advances the write index.
    ///
    /// Must be called once per frame after the compact compute pass has
    /// finished writing to `compactBuffer` and `compactCount`. The GPU
    /// copy is enqueued on the command list and completes before the next
    /// frame's simulation begins.
    ///
    /// # Parameters
    /// - `cmdList`: RHI command list providing `CopyBuffer()`. Must not be null.
    ///
    /// # Preconditions
    /// - `IsValid()` must be true.
    /// - `cmdList` must not be null.
    void IssueCopy(IRHICommandList* cmdList);

    /// Maps the oldest readback slot, reads triggered particle data, and
    /// returns a `SubEmitterReadback` containing the event payloads.
    ///
    /// During the first `kBufferCount` frames after creation, the readback
    /// buffers do not yet contain valid data; this method returns an empty
    /// `SubEmitterReadback` in that case.
    ///
    /// # Parameters
    /// - `device`: RHI device providing `MapBuffer()` / `UnmapBuffer()`.
    ///   Must not be null.
    ///
    /// # Returns
    /// A `SubEmitterReadback` with 0 to `kMaxReadbackParticles` payloads.
    ///
    /// # Preconditions
    /// - `IsValid()` must be true.
    /// - `device` must not be null.
    [[nodiscard]] SubEmitterReadback GatherTriggered(IRHIDevice* device) const;

    // -- Compact shader code -------------------------------------------------

    /// Returns the HLSL source for the compact compute pass.
    ///
    /// This shader scans `SubEmitterFlags[]`, gathers triggered particle
    /// states from the SoA attribute buffers (Positions, Velocities, Colors,
    /// Ages), and appends them to `CompactPayloads[]` via an `InterlockedAdd`
    /// on `CompactCount[0]`, capped at `kMaxReadbackParticles`.
    ///
    /// # Expected bindings
    /// - `SubEmitterFlags` (Buffer<uint>, SRV) — per-particle flags from sim.
    /// - `Positions` (Buffer<float3>, SRV) — particle positions.
    /// - `Velocities` (Buffer<float3>, SRV) — particle velocities.
    /// - `Colors` (Buffer<float4>, SRV) — particle colors.
    /// - `Ages` (Buffer<float>, SRV) — particle ages.
    /// - `CompactPayloads` (RWStructuredBuffer<SubEmitterGPUPayload>, UAV).
    /// - `CompactCount` (RWBuffer<uint>, UAV) — atomic append counter.
    /// - `cbAliveCount` (uint) — number of alive particles to scan.
    /// - `cbMaxReadback` (uint) — cap on appended entries.
    [[nodiscard]] static std::string_view GetCompactShaderCode() noexcept;

private:
    /// Returns the readback slot index that is safe to read (oldest written slot).
    /// This is `(currentWriteIndex + 1) % kBufferCount` — the slot written
    /// 2 frames ago, guaranteed to have completed its GPU copy.
    [[nodiscard]] uint32_t GetReadIndex() const noexcept;
};

} // namespace uz
