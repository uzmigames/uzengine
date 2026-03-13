/// UzEngine Constant Buffer Definitions
///
/// Standard constant buffer layouts for per-frame and per-object data.
/// Bound as root CBVs in the global root signature for minimal overhead.
///
/// # Root Signature Layout
/// - `b0`: Root constants (descriptor index — existing)
/// - `b1`: Per-frame CB (ViewProj, camera position, time, lighting)
/// - `b2`: Per-object CB (model matrix, normal matrix, material index)
///
/// # Alignment
/// All CB structs are padded to 256-byte boundaries (D3D12 requirement).
/// Fields use `float4x4` (64 bytes) and `float4` (16 bytes) for HLSL alignment.

#pragma once

#include "vector3.h"
#include "mat4.h"
#include "rhi_types.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB — bound at b1 (root CBV, updated once per frame)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer data uploaded once per frame.
///
/// Contains camera transforms, global time values, and scene lighting data.
/// Bound as a root CBV at register `b1` so all shaders can access it without
/// descriptor indirection.
///
/// Matches the HLSL struct:
/// ```hlsl
/// cbuffer PerFrameCB : register(b1) {
///     float4x4 viewProj;
///     float4   cameraPos;     // xyz = position, w = unused
///     float4   timeParams;    // x = totalTime, y = deltaTime, z = frameCount, w = unused
///     float4   lightDir;      // xyz = normalised direction toward light, w = 0
///     float4   lightColor;    // xyz = color * intensity, w = unused
///     float4   ambientColor;  // xyz = ambient light color, w = unused
/// };
/// ```
struct PerFrameCB {
    Mat4  viewProj;             ///< Combined view-projection matrix (column-major).
    Vec4  cameraPos;            ///< xyz = world-space camera position, w = unused.
    Vec4  timeParams;           ///< x = total elapsed seconds, y = delta time, z = frame count (float), w = unused.
    Vec4  lightDir;             ///< xyz = normalised direction toward the primary light source, w = 0.
    Vec4  lightColor;           ///< xyz = primary light color multiplied by intensity, w = unused.
    Vec4  ambientColor;         ///< xyz = ambient light color contribution, w = unused.
    Mat4  invViewProj;          ///< Inverse view-projection matrix for world position reconstruction (deferred lighting).
    float _pad[12] = {};        ///< Remaining padding to 256-byte alignment (144 + 64 data + 48 pad = 256).
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
///     float4x4 normalMatrix;
///     uint     albedoIndex;
///     uint     normalIndex;
///     uint     roughnessIndex;
///     uint     metallicIndex;
///     uint     aoIndex;
/// };
/// ```
struct PerObjectCB {
    Mat4     model;            ///< Object-to-world transform (column-major)
    Mat4     normalMatrix;     ///< transpose(inverse(model)) — for lighting
    uint32_t albedoIndex;      ///< SRV heap index for BaseColor map (0 = white default)
    uint32_t normalIndex;      ///< SRV heap index for normal map (0 = flat normal)
    uint32_t roughnessIndex;   ///< SRV heap index for roughness map (0 = 0.5 default)
    uint32_t metallicIndex;    ///< SRV heap index for metallic map (0 = 0.0 default)
    uint32_t aoIndex;          ///< SRV heap index for AO map (0 = 1.0 default)
    float    _pad[3] = {};     ///< Align to 16-byte boundary (5 uints = 20B + 12B pad = 32B)
    float    _pad2[24] = {};   ///< Padding to 256-byte alignment
};

static_assert(sizeof(PerObjectCB) == 256, "PerObjectCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// LightingCB — bound at b3 (root CBV, updated once per frame for deferred lighting)
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of point lights supported in the deferred lighting pass.
static constexpr uint32_t kMaxPointLights = 64;

/// Point light data for the deferred lighting pass.
///
/// Each point light contributes diffuse and specular illumination within its
/// attenuation radius. Packed to 32 bytes (two float4s) for efficient GPU access.
///
/// Matches the HLSL struct:
/// ```hlsl
/// struct PointLightData {
///     float3 position;   // world-space position
///     float  radius;     // attenuation radius
///     float3 color;      // color * intensity (linear HDR)
///     float  _pad;
/// };
/// ```
struct PointLightData {
    Vec3     position;    ///< World-space position of the light source.
    float    radius;      ///< Attenuation radius — light contribution falls to zero at this distance.
    Vec3     color;       ///< Light color multiplied by intensity (linear HDR, not clamped).
    float    _pad = 0.0f; ///< Padding to align to 32 bytes (two float4s).
};

static_assert(sizeof(PointLightData) == 32, "PointLightData must be 32 bytes (two float4s)");

/// Lighting constant buffer for the deferred lighting pass, bound at register `b3`.
///
/// Contains an array of point lights and a count of active lights. The shader
/// iterates `pointLightCount` entries and accumulates their contributions.
///
/// Padded to a 256-byte multiple as required by D3D12 constant buffer alignment.
///
/// Matches the HLSL struct:
/// ```hlsl
/// cbuffer LightingCB : register(b3) {
///     PointLightData pointLights[64];
///     uint           pointLightCount;
///     uint3          _pad;
/// };
/// ```
struct LightingCB {
    PointLightData pointLights[kMaxPointLights]; ///< Point light array (64 * 32 = 2048 bytes).
    uint32_t       pointLightCount = 0;          ///< Number of active point lights in the array.
    uint32_t       _pad[3] = {};                 ///< Align count field to 16 bytes.
    float          _pad2[60] = {};               ///< Pad to 2304 bytes (256-byte multiple).
};

static_assert(sizeof(LightingCB) % 256 == 0, "LightingCB must be a multiple of 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataCB — bound at b4 (root CBV, updated once per frame for shadow sampling)
// ─────────────────────────────────────────────────────────────────────────────

/// Shadow data constant buffer for cascade shadow map sampling in pixel shaders.
///
/// Contains per-cascade light-space view-projection matrices and split distances,
/// plus the shadow atlas SRV index for bindless sampling. Bound at register `b4`.
///
/// Padded to a 256-byte multiple as required by D3D12 constant buffer alignment.
struct ShadowDataCB {
    Mat4     cascadeViewProj[4]; ///< Light-space viewProj per cascade (4 * 64 = 256B).
    Vec4     cascadeSplits;       ///< View-space far split distance per cascade (x=0, y=1, z=2, w=3).
    uint32_t shadowAtlasIndex;    ///< SRV heap index for shadow atlas depth texture.
    uint32_t cascadeCount;        ///< Number of active cascades (1 to 4).
    float    normalBias;          ///< World-space normal bias applied before shadow comparison.
    float    depthBias;           ///< Depth bias added during shadow comparison.
    float    shadowMapSize;       ///< Per-cascade resolution in texels (e.g., 2048).
    float    _pad[3] = {};        ///< Padding to 16-byte boundary.
    float    _pad2[52] = {};      ///< Pad to 512 bytes (256-byte multiple).
};

static_assert(sizeof(ShadowDataCB) == 512, "ShadowDataCB must be 512 bytes (256-byte multiple)");

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

/// Result of a constant buffer suballocation from `CBRingAllocator`.
///
/// Holds both CPU-writable and GPU-visible pointers to the same memory region,
/// allowing the caller to memcpy data and immediately bind the GPU address as
/// a root CBV. Valid only for the current frame — the region is recycled on
/// the next `BeginFrame` call for this frame index.
///
/// # Thread Safety
/// Not thread-safe. Produced and consumed on the render thread only.
///
/// # Examples
/// ```cpp
/// auto alloc = ring.Allocate(sizeof(PerFrameCB));
/// memcpy(alloc.cpuPtr, &cbData, sizeof(PerFrameCB));
/// cmdList->SetGraphicsRootConstantBufferView(1, alloc.gpuAddr);
/// ```
struct CBAllocation {
    void*           cpuPtr;      ///< CPU-writable pointer (upload heap mapped memory)
    uint64_t        gpuAddr;     ///< GPU virtual address for root CBV binding (DX12/Vulkan)
    RHIBufferHandle bufferHandle; ///< Backing buffer handle (for DX11 handle-based binding)
    uint64_t        offsetBytes; ///< Byte offset into the backing buffer
};

/// Ring-buffer allocator for per-frame constant buffer uploads.
///
/// Pre-allocates a large upload-heap buffer and suballocates 256-byte-aligned
/// slots per frame. Triple-buffered (or N-buffered) so the GPU can read from
/// a completed frame's region while the CPU writes into the current frame's
/// region without synchronization stalls.
///
/// Typical lifetime: created once at renderer init, `BeginFrame` each frame,
/// `Allocate` for each CB upload, destroyed at renderer shutdown.
///
/// # Thread Safety
/// NOT thread-safe. All calls must originate from the render thread.
///
/// # Examples
/// ```cpp
/// CBRingAllocator ring;
/// ring.Init(device, 3, 64 * 1024);  // 3 frames, 64KB each
/// ring.BeginFrame(frameIndex);
/// auto alloc = ring.Allocate(sizeof(PerFrameCB));
/// memcpy(alloc.cpuPtr, &perFrame, sizeof(PerFrameCB));
/// ```
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
