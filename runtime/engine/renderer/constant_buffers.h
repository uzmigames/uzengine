/// UzEngine Constant Buffer Definitions
///
/// Standard constant buffer layouts for per-frame and per-object data.
/// Bound as root CBVs in the global root signature for minimal overhead.
///
/// # Root Signature Layout
/// - `b0`: Root constants (descriptor index — existing)
/// - `b1`: Per-frame CB (ViewProj, camera position, time)
/// - `b2`: Per-object CB (model matrix, normal matrix, material index)
///
/// # Alignment
/// All CB structs are padded to 256-byte boundaries (D3D12 requirement).
/// Fields use `float4x4` (64 bytes) and `float4` (16 bytes) for HLSL alignment.

#pragma once

#include "engine_math.h"
#include "rhi_types.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB — bound at b1 (root CBV, updated once per frame)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer data uploaded once per frame.
///
/// Contains camera transforms and global time values. Bound as a root CBV
/// at register `b1` so all shaders can access it without descriptor indirection.
///
/// Matches the HLSL struct:
/// ```hlsl
/// cbuffer PerFrameCB : register(b1) {
///     float4x4 viewProj;
///     float4   cameraPos;    // xyz = position, w = unused
///     float4   timeParams;   // x = totalTime, y = deltaTime, z = frameCount, w = unused
/// };
/// ```
struct PerFrameCB {
    Mat4  viewProj;         ///< Combined view-projection matrix (column-major)
    Vec4  cameraPos;        ///< xyz = world-space camera position, w = unused
    Vec4  timeParams;       ///< x = total elapsed seconds, y = delta time, z = frame count (float), w = unused
    float _pad[40] = {};    ///< Padding to 256-byte alignment (96 data + 160 pad = 256)
};

static_assert(sizeof(PerFrameCB) == 256, "PerFrameCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// PerObjectCB — bound at b2 (root CBV, updated per draw call)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-object constant buffer data uploaded per draw call.
///
/// Contains object transforms and material reference. Bound as a root CBV
/// at register `b2`.
///
/// Matches the HLSL struct:
/// ```hlsl
/// cbuffer PerObjectCB : register(b2) {
///     float4x4 model;
///     float4x4 normalMatrix;   // transpose(inverse(model)) for correct normal transforms
///     uint     materialIndex;  // index into material StructuredBuffer
/// };
/// ```
struct PerObjectCB {
    Mat4     model;          ///< Object-to-world transform (column-major)
    Mat4     normalMatrix;   ///< transpose(inverse(model)) — for lighting
    uint32_t materialIndex;  ///< Index into the material array
    float    _pad[3] = {};   ///< Align materialIndex row to 16 bytes
    float    _pad2[28] = {}; ///< Padding to 256-byte alignment
};

static_assert(sizeof(PerObjectCB) == 256, "PerObjectCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// CBRingAllocator — triple-buffered constant buffer upload ring
// ─────────────────────────────────────────────────────────────────────────────

/// Ring-buffer allocator for per-frame constant buffer uploads.
///
/// Pre-allocates a large upload-heap buffer and suballocates 256-byte-aligned
/// slots per frame. Triple-buffered to avoid GPU/CPU synchronization stalls.
///
/// # Usage
/// ```cpp
/// CBRingAllocator ring;
/// ring.Init(device, 3, 64 * 1024);  // 3 frames, 64KB per frame
///
/// // Per frame:
/// ring.BeginFrame(frameIndex);
/// auto alloc = ring.Allocate(sizeof(PerFrameCB));
/// memcpy(alloc.cpuPtr, &perFrameData, sizeof(PerFrameCB));
/// cmdList->SetGraphicsRootConstantBufferView(1, alloc.gpuAddr);
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Use from the render thread only.
class IRHIDevice;

struct CBAllocation {
    void*    cpuPtr;   ///< CPU-writable pointer (upload heap mapped memory)
    uint64_t gpuAddr;  ///< GPU virtual address for root CBV binding
};

class CBRingAllocator {
public:
    CBRingAllocator() = default;
    ~CBRingAllocator();

    /// Initializes the ring buffer with the given number of frames and per-frame capacity.
    ///
    /// # Arguments
    /// * `device` — RHI device for buffer creation. Must outlive this allocator.
    /// * `framesInFlight` — Number of frames to triple-buffer (typically 3).
    /// * `perFrameBytes` — Maximum bytes per frame (rounded up to 256-byte alignment).
    ///
    /// # Returns
    /// `true` if the backing buffer was created successfully.
    bool Init(IRHIDevice* device, uint32_t framesInFlight, uint64_t perFrameBytes);

    /// Releases the backing buffer.
    void Shutdown();

    /// Resets the write offset for the current frame's region.
    ///
    /// # Arguments
    /// * `frameIndex` — Current frame index (0, 1, 2, ...). Used modulo framesInFlight.
    void BeginFrame(uint32_t frameIndex);

    /// Suballocates a 256-byte-aligned block from the current frame's region.
    ///
    /// # Arguments
    /// * `sizeBytes` — Number of bytes needed (will be rounded up to 256).
    ///
    /// # Returns
    /// A `CBAllocation` with CPU write pointer and GPU virtual address.
    /// Returns `{nullptr, 0}` if the frame's region is full.
    [[nodiscard]] CBAllocation Allocate(uint64_t sizeBytes);

private:
    IRHIDevice*    m_device = nullptr;
    RHIBufferHandle m_buffer;
    uint8_t*       m_mappedBase = nullptr;
    uint64_t       m_gpuBaseAddr = 0;
    uint64_t       m_perFrameBytes = 0;
    uint32_t       m_framesInFlight = 0;
    uint32_t       m_currentFrame = 0;
    uint64_t       m_currentOffset = 0;
};

} // namespace uz
