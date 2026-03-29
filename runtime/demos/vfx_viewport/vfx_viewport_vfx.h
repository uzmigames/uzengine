/// VFX Viewport Demo — GPU Particle System
///
/// Manages GPU-driven particle buffers, compute pipelines (spawn, update,
/// indirect args, sort), and billboard rendering. Uses engine's ShaderManager
/// for shader compilation and RHI abstraction for all GPU resources.
///
/// Particle data is stored in Structure-of-Arrays (SoA) format with ping-pong
/// double buffering: each frame reads from set A and writes to set B (or vice
/// versa). This avoids read-write hazards without explicit synchronization.

#pragma once

#include "rhi_types.h"
#include "vector3.h"
#include "mat4.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class PipelineLibrary;
class CBRingAllocator;

// ─────────────────────────────────────────────────────────────────────────────
// VFX Emitter Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side emitter configuration. Controls particle spawn behavior and
/// billboard rendering parameters (flipbook, soft fade, emissive).
struct VFXEmitterConfig {
    float position[3]       = {0.0f, 1.5f, 0.0f};
    float spawnRadius       = 0.1f;
    float spawnVelocity[3]  = {0.0f, 3.0f, 0.0f};
    float velocityVariation = 0.5f;
    float spreadAngle       = 0.3f;
    float minLifetime       = 1.0f;
    float maxLifetime        = 3.0f;
    float initialSize       = 0.05f;
    float initialColor[4]   = {1.0f, 0.6f, 0.2f, 0.8f};
    float gravity[3]        = {0.0f, -9.81f, 0.0f};
    float damping           = 0.1f;
    // Billboard rendering parameters
    float    softFadeDistance  = 0.3f;  ///< World-space soft particle fade distance.
    float    emissiveIntensity = 0.0f; ///< Additive emissive multiplier.
    // Flipbook / SubUV sprite sheet parameters
    uint32_t flipbookRows        = 1;    ///< Rows in sprite sheet. 1 = no flipbook.
    uint32_t flipbookCols        = 1;    ///< Columns in sprite sheet. 1 = no flipbook.
    uint32_t flipbookTotalFrames = 1;    ///< Total valid frames.
    float    flipbookFPS         = 0.0f; ///< Playback speed (frames/sec). 0 = static.
    uint32_t flipbookBlendMode   = 0;    ///< 0=None, 1=Linear, 2=Random.
    // Size over lifetime (analytical piecewise-linear curve)
    float    sizeOverLifeStart    = 1.0f;  ///< Size multiplier at birth (t=0).
    float    sizeOverLifePeak     = 1.0f;  ///< Size multiplier at peak time.
    float    sizeOverLifeEnd      = 0.0f;  ///< Size multiplier at death (t=1).
    float    sizeOverLifePeakTime = 0.3f;  ///< Normalized age [0,1] where peak occurs.
    // Color over lifetime (linear interpolation start→end)
    float    colorLifeStartR = 1.0f;  ///< Start color red.
    float    colorLifeStartG = 1.0f;  ///< Start color green.
    float    colorLifeStartB = 1.0f;  ///< Start color blue.
    float    colorLifeStartA = 1.0f;  ///< Start color alpha.
    float    colorLifeEndR   = 1.0f;  ///< End color red.
    float    colorLifeEndG   = 1.0f;  ///< End color green.
    float    colorLifeEndB   = 1.0f;  ///< End color blue.
    float    colorLifeEndA   = 0.0f;  ///< End color alpha (fade to transparent).
    // Billboard alignment and stretching
    uint32_t alignmentMode   = 0;     ///< 0=CameraFacing, 1=VelocityAligned.
    float    stretchFactor   = 0.0f;  ///< Velocity stretch multiplier. 0=no stretch, >0=elongate along velocity.
};

/// Named emitter preset with a spawn rate.
struct VFXPreset {
    const char* name;
    VFXEmitterConfig config;
    float spawnRate;
};

/// Returns the built-in emitter presets array and its count.
///
/// # Arguments
/// * `outCount` — Receives the number of presets.
///
/// # Returns
/// Pointer to the static presets array.
const VFXPreset* GetVFXPresets(int& outCount);

// ─────────────────────────────────────────────────────────────────────────────
// VFX Spawn Constant Buffer (matches particle_spawn_cs.hlsl b0 layout)
// ─────────────────────────────────────────────────────────────────────────────

/// Constant buffer uploaded each frame for the spawn compute shader.
/// Layout matches VFXSpawnConstants in particle_spawn_cs.hlsl (128 bytes).
struct VFXSpawnCB {
    float    emitterPos[3];           float spawnRadius;          // 16 bytes
    float    spawnVelocity[3];        float velocityVariation;    // 16 bytes
    float    spreadAngle;             float minLifetime;
    float    maxLifetime;             float initialSize;          // 16 bytes
    float    initialColor[4];                                     // 16 bytes
    float    dt;                      uint32_t frameNumber;
    uint32_t spawnCount;              uint32_t maxParticles;      // 16 bytes
    // Bindless descriptor indices (SM6.6 path)
    uint32_t freeListSrvIndex;
    uint32_t positionsUavIndex;       uint32_t velocitiesUavIndex;
    uint32_t agesUavIndex;                                        // 16 bytes
    uint32_t lifespansUavIndex;       uint32_t sizesUavIndex;
    uint32_t colorsUavIndex;          uint32_t rotationsUavIndex; // 16 bytes
    uint32_t freeListHeadUavIndex;    uint32_t aliveCountUavIndex;
    uint32_t _pad0;                   uint32_t _pad1;             // 16 bytes
};
static_assert(sizeof(VFXSpawnCB) == 128, "VFXSpawnCB must be 128 bytes (8 x float4)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX Update Constant Buffer (matches particle_update_cs.hlsl b0 layout)
// ─────────────────────────────────────────────────────────────────────────────

/// Constant buffer for the particle update compute shader.
struct VFXUpdateCB {
    float    gravity[3];              float dt;                   // 16 bytes
    float    damping;                 uint32_t aliveCount;
    uint32_t frameNumber;             uint32_t maxParticles;      // 16 bytes
    // Bindless descriptor indices (SM6.6 path)
    uint32_t readPositionsSrv;        uint32_t readVelocitiesSrv;
    uint32_t readAgesSrv;             uint32_t readLifespansSrv;  // 16 bytes
    uint32_t readSizesSrv;            uint32_t readColorsSrv;
    uint32_t readRotationsSrv;        uint32_t _padRead;          // 16 bytes
    uint32_t writePositionsUav;       uint32_t writeVelocitiesUav;
    uint32_t writeAgesUav;            uint32_t writeLifespansUav; // 16 bytes
    uint32_t writeSizesUav;           uint32_t writeColorsUav;
    uint32_t writeRotationsUav;       uint32_t _padWrite;         // 16 bytes
    uint32_t freeListUavIndex;        uint32_t freeListHeadUavIndex;
    uint32_t aliveCountUavIndex;      uint32_t _padAtomic;        // 16 bytes
};
static_assert(sizeof(VFXUpdateCB) == 112, "VFXUpdateCB must be 112 bytes (7 x float4)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX Billboard Root Constants (matches VFXBillboardConstants in b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Unified root constants for billboard VS + PS bindless resource access.
/// Layout matches VFXBillboardConstants in particle_billboard_vs.hlsl / ps.hlsl.
/// Both stages share b0 memory so this single struct covers all indices.
struct VFXBillboardIndicesCB {
    // VS indices (bytes 0-31)
    uint32_t positionBufferIdx;
    uint32_t sizeBufferIdx;
    uint32_t colorBufferIdx;
    uint32_t rotationBufferIdx;
    uint32_t sortedIndicesSrvIndex;
    uint32_t velocityBufferIdx;
    uint32_t agesBufferIdx;
    uint32_t lifespansBufferIdx;
    // PS indices (bytes 32-47)
    uint32_t spriteTextureIdx;
    uint32_t depthBufferIdx;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
};
static_assert(sizeof(VFXBillboardIndicesCB) == 48, "VFXBillboardIndicesCB must be 48 bytes (12 x uint32)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX Billboard CB (matches VFXBillboardCB in b2, shared by VS and PS)
// ─────────────────────────────────────────────────────────────────────────────

/// Unified per-emitter constant buffer for billboard VS + PS.
/// Layout matches VFXBillboardCB in both particle_billboard_vs.hlsl and _ps.hlsl.
struct VFXBillboardCB {
    // VS data (emitter parameters, 160 bytes)
    Mat4     invView;                              ///< Inverse view matrix (rows 0,1 = camera right/up).
    float    emitterPosition[3];                   ///< World-space emitter origin.
    float    globalSizeMultiplier = 1.0f;          ///< Size multiplier.
    float    globalColorTint[4] = {1,1,1,1};       ///< Color tint.
    Mat4     prevViewProj;                         ///< Previous frame's view-projection (TAA).
    // PS data (rendering parameters, 48 bytes)
    float    softFadeDistance   = 0.3f;            ///< Soft particle depth fade distance.
    float    alphaErosion      = 0.0f;             ///< Subtractive alpha erosion [0, 1].
    float    emissiveIntensity = 0.0f;             ///< Additive emissive multiplier.
    float    _psPad0           = 0.0f;
    uint32_t flipbookRows        = 1;              ///< Sprite sheet rows. 1 = no flipbook.
    uint32_t flipbookCols        = 1;              ///< Sprite sheet columns. 1 = no flipbook.
    uint32_t flipbookTotalFrames = 1;              ///< Total valid frames.
    float    flipbookFPS         = 0.0f;           ///< Playback speed (frames/sec).
    uint32_t flipbookBlendMode   = 0;              ///< 0=None, 1=Linear, 2=Random.
    uint32_t _fbPad0             = 0;
    uint32_t _fbPad1             = 0;
    uint32_t _fbPad2             = 0;
    // ── Size over lifetime (analytical piecewise-linear curve, 16 bytes) ──
    float    sizeOverLifeStart    = 1.0f;          ///< Size multiplier at birth (t=0).
    float    sizeOverLifePeak     = 1.0f;          ///< Size multiplier at peak time.
    float    sizeOverLifeEnd      = 0.0f;          ///< Size multiplier at death (t=1).
    float    sizeOverLifePeakTime = 0.3f;          ///< Normalized age [0,1] where peak occurs.
    // ── Color over lifetime (linear interpolation, 32 bytes) ──
    float    colorLifeStart[4]    = {1,1,1,1};     ///< RGBA color at birth.
    float    colorLifeEnd[4]      = {1,1,1,0};     ///< RGBA color at death.
    // ── Billboard alignment and stretching (16 bytes) ──
    uint32_t alignmentMode        = 0;              ///< 0=CameraFacing, 1=VelocityAligned.
    float    stretchFactor        = 0.0f;           ///< Velocity stretch multiplier. 0=no stretch.
    float    _solPad0             = 0.0f;
    float    _solPad1             = 0.0f;
};
static_assert(sizeof(VFXBillboardCB) == 272, "VFXBillboardCB must be 272 bytes (256 base + 16 alignment/stretch)");

// ─────────────────────────────────────────────────────────────────────────────
// VFX GPU Particle System
// ─────────────────────────────────────────────────────────────────────────────

/// Manages all GPU resources and pipelines for the particle system.
///
/// Particle data uses SoA layout with 7 channels (positions, velocities,
/// ages, lifespans, sizes, colors, rotations). Each channel has two copies
/// (ping-pong) to allow simultaneous read and write without hazards.
///
/// Bookkeeping buffers: alive count, free list, free list head, indirect args.
/// Sort buffers: sort keys + sort indices for front-to-back depth sorting.
class VFXParticleSystem {
public:
    VFXParticleSystem() = default;
    ~VFXParticleSystem() = default;

    VFXParticleSystem(const VFXParticleSystem&) = delete;
    VFXParticleSystem& operator=(const VFXParticleSystem&) = delete;

    /// Initializes all GPU buffers and compile compute/graphics pipelines.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation.
    /// * `shaderMgr` — Shader manager for compute/graphics shader compilation.
    /// * `maxParticles` — Maximum particle pool capacity.
    ///
    /// # Returns
    /// `true` on success, `false` if any resource or pipeline creation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t maxParticles);

    /// Uploads initial free list data to GPU buffers via staging copy.
    /// Must be called once after Init(), with a command list in recording state.
    /// The command list must be submitted before the first DispatchCompute().
    void UploadInitialFreeList(IRHICommandList* cmd);

    /// Destroys all GPU resources. Safe to call multiple times.
    void Shutdown(IRHIDevice* device);

    /// Records spawn, update, indirect args, and sort compute dispatches.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `cbRing` — CB ring allocator for per-frame constant buffers.
    /// * `config` — Current emitter configuration.
    /// * `spawnCount` — Number of particles to spawn this frame.
    /// * `dt` — Frame delta time in seconds.
    /// * `frameNumber` — Monotonically increasing frame counter.
    void DispatchCompute(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                         const VFXEmitterConfig& config, uint32_t spawnCount,
                         float dt, uint32_t frameNumber);

    /// Records billboard draw commands using ExecuteIndirect.
    ///
    /// The particle count comes from the GPU-written indirect args buffer.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state (inside a render pass).
    /// * `cbRing` — CB ring allocator for per-frame constant buffers.
    /// * `perFrameCBGpuAddr` — GPU address of the PerFrameCB for this frame.
    /// * `viewMat` — Current view matrix (for billboard orientation).
    /// * `emitterPos` — World-space emitter position.
    /// * `prevViewProj` — Previous frame's view-projection (for TAA motion vectors).
    /// * `config` — Emitter config (flipbook, soft fade, emissive params).
    /// * `depthBufferSrvIdx` — SRV index for the scene depth buffer (soft fade).
    /// * `spriteTextureSrvIdx` — SRV index for the sprite atlas (UINT32_MAX = none).
    void DrawBillboards(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                        uint64_t perFrameCBGpuAddr,
                        const Mat4& viewMat, const Vec3& emitterPos,
                        const Mat4& prevViewProj,
                        const VFXEmitterConfig& config,
                        uint32_t depthBufferSrvIdx,
                        uint32_t spriteTextureSrvIdx = UINT32_MAX);

    /// Returns the billboard graphics pipeline handle.
    [[nodiscard]] RHIPipelineHandle GetBillboardPSO() const { return m_billboardPSO; }

    /// Returns the maximum particle count.
    [[nodiscard]] uint32_t GetMaxParticles() const { return m_maxParticles; }

    /// Flips the ping-pong index (call once per frame after compute dispatch).
    void FlipPingPong() { m_pingPong = 1 - m_pingPong; }

    /// Returns the current ping-pong index (0 or 1).
    [[nodiscard]] int GetPingPong() const { return m_pingPong; }

private:
    static constexpr uint32_t kChannelCount = 7; // pos, vel, age, life, size, color, rotation
    static constexpr uint32_t kSpawnGroupSize = 256;
    static constexpr uint32_t kUpdateGroupSize = 256;

    IRHIDevice* m_device = nullptr;
    uint32_t m_maxParticles = 0;
    int m_pingPong = 0;

    // Particle SoA buffers: [channel][pingPongSet]
    // Channels: 0=positions(float4), 1=velocities(float4), 2=ages(float),
    //           3=lifespans(float), 4=sizes(float), 5=colors(float4), 6=rotations(float)
    RHIBufferHandle m_particleBuffers[kChannelCount][2] = {};

    // Bookkeeping buffers
    RHIBufferHandle m_aliveCount;    ///< Single uint32: number of alive particles.
    RHIBufferHandle m_freeList;      ///< Array of uint32: free particle slot indices.
    RHIBufferHandle m_freeListHead;  ///< Single uint32: stack top pointer.
    RHIBufferHandle m_indirectArgs;  ///< Draw indirect args (6 uints: vertexCount, instanceCount, ...).

    // Sort buffers
    RHIBufferHandle m_sortKeys;      ///< float per particle: depth for sorting.
    RHIBufferHandle m_sortIndices;   ///< uint32 per particle: particle index.

    // Staging buffers for initial free list upload (destroyed on Shutdown)
    RHIBufferHandle m_freeListStaging;     ///< Staging buffer for free list init.
    RHIBufferHandle m_freeListHeadStaging; ///< Staging buffer for free list head init.
    RHIBufferHandle m_aliveCountStaging;   ///< Staging buffer for alive count init.
    RHIBufferHandle m_zeroInitStaging;     ///< Staging buffer for zeroing particle attributes.

    // Compute pipelines
    RHIPipelineHandle m_spawnPSO;
    RHIPipelineHandle m_updatePSO;
    RHIPipelineHandle m_indirectArgsPSO;
    RHIPipelineHandle m_sortPSO;

    // Graphics pipeline for billboard rendering
    RHIPipelineHandle m_billboardPSO;

    /// Returns the element stride in bytes for the given channel index.
    [[nodiscard]] static uint32_t ChannelStride(uint32_t channel);

    /// Creates a single structured buffer for a particle channel.
    [[nodiscard]] RHIBufferHandle CreateParticleBuffer(IRHIDevice* device,
                                                        uint32_t stride,
                                                        const char* name);
};

// ─────────────────────────────────────────────────────────────────────────────
// Trail Root Constants (matches TrailRootConstants in trail_common.hlsli, b0)
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side mirror of the HLSL TrailRootConstants struct (32 bytes = 8 x uint32).
///
/// Uploaded via `SetRootConstants(0, ...)` before each trail draw call. Shared
/// by the trail vertex and pixel shaders through root parameter 0 (b0).
struct TrailRootConstants {
    float    uvScrollRate;        ///< U-axis scroll speed (UV-units/second).
    float    softDepthRange;      ///< Soft-fade radius in metres. 0 = disabled.
    float    alphaClipThreshold;  ///< Alpha discard threshold [0, 1].
    uint32_t trailTexSrvIndex;    ///< Bindless SRV index for trail texture (UINT32_MAX = none).
    uint32_t depthBufferSrvIndex; ///< Bindless SRV index for scene depth buffer.
    uint32_t _pad0 = 0;          ///< Alignment pad — unused.
    uint32_t _pad1 = 0;          ///< Alignment pad — unused.
    uint32_t _pad2 = 0;          ///< Alignment pad — unused.
};
static_assert(sizeof(TrailRootConstants) == 32, "TrailRootConstants must be 32 bytes (8 x uint32)");

// ─────────────────────────────────────────────────────────────────────────────
// Decal Object CB (matches DecalObjectCB in geometry/decal_vs.hlsl + decal_forward_ps.hlsl, b2)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-decal constant buffer uploaded at b2 for the decal projection pass.
/// Layout matches DecalObjectCB in both decal_vs.hlsl and decal_forward_ps.hlsl.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer decals
struct DecalObjectCB {
    Mat4     decalWorld;                          ///< World transform (pos + rot + extents).
    Mat4     decalInvWorld;                       ///< Inverse of decalWorld (world → local projection).
    float    decalOpacity       = 1.0f;           ///< Global opacity [0, 1].
    float    decalNormalBlend   = 0.8f;           ///< Normal blend intensity [0, 1] (unused in forward).
    float    decalRoughness     = 0.5f;           ///< Roughness value/multiplier.
    float    decalMetallic      = 0.0f;           ///< Metallic value/multiplier.
    float    decalFadeDistance  = 10.0f;          ///< Edge fade distance in cm.
    float    decalAngleFade    = 60.0f;           ///< Angle fade start degrees (unused in forward).
    uint32_t decalAlbedoSrv    = UINT32_MAX;      ///< Bindless SRV index for albedo texture.
    uint32_t decalNormalSrv    = UINT32_MAX;      ///< Normal texture SRV index (unused in forward).
    uint32_t decalMaterialSrv  = UINT32_MAX;      ///< Material texture SRV index (unused in forward).
    uint32_t decalDepthSrv     = UINT32_MAX;      ///< Depth buffer SRV index.
    uint32_t decalNormalGbufSrv = UINT32_MAX;     ///< GBuffer normal SRV (unused in forward).
    float    _pad0             = 0.0f;            ///< Pad to 16-byte boundary.
};
static_assert(sizeof(DecalObjectCB) == 176, "DecalObjectCB must be 176 bytes (2x Mat4 + 12 floats/uints)");

// ─────────────────────────────────────────────────────────────────────────────
// Distortion Haze Root Constants (matches DistortionHazeConstants in distortion_haze_ps.hlsl, b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the procedural heat-haze distortion accumulation pass.
/// Uploaded via SetRootConstants(0, ...) before the fullscreen haze draw.
struct DistortionHazeConstants {
    float emitterScreenUV[2];  ///< Emitter position in screen UV [0,1].
    float strength;            ///< Global distortion strength (0=off, 1=full).
    float time;                ///< Total elapsed time for animation.
    float radius;              ///< Falloff radius in screen UV units.
    float frequency;           ///< Wave frequency for animated haze.
    float _pad0 = 0.0f;
    float _pad1 = 0.0f;
};
static_assert(sizeof(DistortionHazeConstants) == 32, "DistortionHazeConstants must be 32 bytes (8 x uint32)");

// ─────────────────────────────────────────────────────────────────────────────
// Distortion Resolve Root Constants (matches DistortionResolveIndices in distortion_resolve_ps.hlsl, b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the distortion resolve fullscreen pass.
/// Points the shader at the pre-distortion HDR color and the distortion accumulation textures.
struct DistortionResolveConstants {
    uint32_t colorTextureSrvIndex;       ///< SRV index: pre-distortion HDR color.
    uint32_t distortionTextureSrvIndex;  ///< SRV index: RGBA16F distortion buffer.
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
};
static_assert(sizeof(DistortionResolveConstants) == 16, "DistortionResolveConstants must be 16 bytes (4 x uint32)");

} // namespace uz
