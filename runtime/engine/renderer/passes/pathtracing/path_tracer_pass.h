/// UzEngine Path Tracer Pass
///
/// Runtime mode toggle (`r.PathTracer.Enable`) that replaces the deferred
/// lighting / GI / shadow chain with a Monte Carlo path tracer. The pass
/// writes the lit result directly to `hdrSceneColor` so all downstream
/// post-process (bloom, exposure, tonemap, AA) keeps working unchanged.
///
/// # Step 1 (current)
/// Solid magenta fullscreen — verifies pass wiring.
///
/// # Future Steps (see docs/analysis/path-tracing/04-incremental-plan.md)
/// 2. GBuffer first hit visualization
/// 3. One-bounce diffuse + accumulation
/// 4. Multi-bounce + Russian roulette + direct sun
/// 5. GGX VNDF specular via material/brdf.hlsli
/// 6. Camera-cut accumulation reset
/// 7. Analytic primitives from PtPrimitiveComponent
/// 8. Falcor PSNR parity (CLAUDE.md RULE 0)
///
/// @ref Kajiya 1986 "The Rendering Equation"

#pragma once

#include "constant_buffers.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstddef>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

class CBRingAllocator;
class GlobalDistanceField;

/// Per-mesh material entry for the path tracer's bounce-hit shading.
///
/// Indexed by TLAS `InstanceID()` (the engine packs `cmd.meshId` there —
/// see tlas_manager.cpp). Raw geometry views recover triangle UVs and normals;
/// missing views retain the average-albedo fallback. Mirrored by
/// `GIMeshMaterial` in shaders/runtime/gi/gi_trace_common.hlsli.
///
/// Phase D stage 2 appended the mesh's emission (`emissiveSrv`, `emissive*`: the
/// GBuffer's emissive colour x intensity and its texture) for the surface
/// radiance cache's cell lighting; the path tracer never reads the new fields.
struct alignas(16) GIMeshMaterialGPU {
    float    tintR = 1.f;        ///< BaseColor tint factor (linear).
    float    tintG = 1.f;
    float    tintB = 1.f;
    uint32_t albedoSrv = UINT32_MAX; ///< Albedo texture SRV (UINT32_MAX = tint only).
    uint32_t vbSrv = UINT32_MAX; ///< Raw vertex SRV; sentinel disables triangle lookup.
    uint32_t ibSrv = UINT32_MAX; ///< Raw index SRV; sentinel disables triangle lookup.
    uint32_t vertexStride = 0;   ///< Byte distance between vertices.
    uint32_t uvOffset = 0;       ///< Float2 UV byte offset within a vertex.
    uint32_t normalOffset = 0;   ///< Normal byte offset within a vertex.
    uint32_t indexFormat = 0;    ///< Index element size in bytes (2 or 4).
    uint32_t normalFormat = 0;   ///< 0 = float3, 1 = signed packed int8 xyz.
    uint32_t emissiveSrv = UINT32_MAX; ///< Emissive texture SRV (UINT32_MAX = colour only).
    float    emissiveR = 0.f;    ///< Emissive colour x intensity (linear radiance); zero = no emission.
    float    emissiveG = 0.f;
    float    emissiveB = 0.f;
    uint32_t padding = 0;
};

static_assert(sizeof(GIMeshMaterialGPU) == 64, "GIMeshMaterialGPU must be 64 bytes");
static_assert(alignof(GIMeshMaterialGPU) == 16);
static_assert(offsetof(GIMeshMaterialGPU, tintR) == 0);
static_assert(offsetof(GIMeshMaterialGPU, tintG) == 4);
static_assert(offsetof(GIMeshMaterialGPU, tintB) == 8);
static_assert(offsetof(GIMeshMaterialGPU, albedoSrv) == 12);
static_assert(offsetof(GIMeshMaterialGPU, vbSrv) == 16);
static_assert(offsetof(GIMeshMaterialGPU, ibSrv) == 20);
static_assert(offsetof(GIMeshMaterialGPU, vertexStride) == 24);
static_assert(offsetof(GIMeshMaterialGPU, uvOffset) == 28);
static_assert(offsetof(GIMeshMaterialGPU, normalOffset) == 32);
static_assert(offsetof(GIMeshMaterialGPU, indexFormat) == 36);
static_assert(offsetof(GIMeshMaterialGPU, normalFormat) == 40);
static_assert(offsetof(GIMeshMaterialGPU, emissiveSrv) == 44);
static_assert(offsetof(GIMeshMaterialGPU, emissiveR) == 48);
static_assert(offsetof(GIMeshMaterialGPU, emissiveB) == 56);
static_assert(offsetof(GIMeshMaterialGPU, padding) == 60);

/// Configuration for the path tracer pass.
struct PathTracerPassDesc {
    rg::RgResourceId  hdrTarget;        ///< HDR scene color RT[1] (write, RenderTarget).
    rg::RgResourceId  accumCurr;        ///< RGBA32F accumulation curr write target RT[0].
    rg::RgResourceId  accumPrev;        ///< Previous ping-pong accumulation (read).
    rg::RgResourceId  normalGBuffer;    ///< GBuffer normal (read).
    rg::RgResourceId  depthGBuffer;     ///< GBuffer depth  (read).
    rg::RgResourceId  albedoGBuffer;    ///< GBuffer albedo+AO (read).
    rg::RgResourceId  positionGBuffer;  ///< GBuffer worldPos (read).
    rg::RgResourceId  emissiveGBuffer;  ///< GBuffer emissive HDR (read).
    rg::RgResourceId  shadowAtlas;      ///< CSM shadow atlas (read, ShaderResource).
    CBAllocation      perFrameAlloc;    ///< PerFrame CB at b1.
    RHIPipelineHandle pipeline;         ///< Path tracer fullscreen PSO (2 RTs).
    uint32_t          normalSrvIndex;
    uint32_t          depthSrvIndex;
    uint32_t          albedoSrvIndex;
    uint32_t          positionSrvIndex;
    uint32_t          emissiveSrvIndex;   ///< GBuffer emissive HDR SRV index.
    uint32_t          skyCubeSrvIndex;
    uint32_t          accumPrevSrvIndex;  ///< SRV of the OTHER ping-pong accum (history read).
    uint32_t          sampleCount;        ///< Accumulated sample count this frame (0=fresh).
    uint32_t          frameIndex;
    uint32_t          maxBounces = 4;       ///< Maximum traced bounces, clamped to [1,16].
    uint32_t          samplesPerFrame = 4;  ///< Per-frame ray budget, at least one per lobe.
    float             clampFirefly = 64.f; ///< Per-channel sample clamp; zero disables it.
    float             envIntensity = 1.f;  ///< Sky radiance multiplier.
    uint32_t          rrEnabled = 1;       ///< Enables throughput-based Russian roulette.
    uint32_t          rrMinBounce = 2;     ///< First bounce eligible for roulette termination.
    CBAllocation      shadowAlloc;        ///< ShadowDataCB allocation (b4) — for CSM sun shadows.
    /// Scene TLAS SRV for real multi-bounce transport (UINT32_MAX = fall
    /// back to the occlusion-free sky estimate — NOT reference quality).
    uint32_t          tlasSrvIndex = UINT32_MAX;
    /// Inputs for the b2 TraceConstants (the shared `RadianceCacheTraceConstants`
    /// layout: GDF clipmaps for hit normals + albedo, sun, skylight). The CB is
    /// built + bound INSIDE the execution callback — the PerFrameCB cpuPtr is
    /// only populated after graph build, so the sun/sky values MUST be read at
    /// execute time (reading them at registration yields zeros — the
    /// black-bounce bug of 2026-07-27).
    CBRingAllocator*           giCbRing = nullptr;   ///< null = no b2 bound.
    const GlobalDistanceField* gdf = nullptr;
    /// Atmosphere sky cubemap SRV for bounce-miss radiance (UINT32_MAX = the
    /// flat skylight colour). Passed as a root constant.
    uint32_t                   skyCubemapSrv   = UINT32_MAX;
    uint32_t                   equirectSrvIndex = UINT32_MAX; ///< Raw equirect HDRI SRV for direct env sampling (UINT32_MAX = none, fall back to cubemap).
    /// StructuredBuffer<GIMeshMaterialGPU> SRV indexed by TLAS InstanceID
    /// (UINT32_MAX = GDF surface albedo). Passed as a root constant.
    uint32_t                   materialTableSrv = UINT32_MAX;
    bool              bindless;
    float             viewportWidth;
    float             viewportHeight;
    uint32_t          envAliasSrv = UINT32_MAX;   ///< Environment alias table; sentinel disables NEE.
    uint32_t          envLumMip0Srv = UINT32_MAX; ///< Luminance weights used to evaluate the MIS PDF.
    uint32_t          envTotalLumBits = 0;        ///< Total luminance packed as float bits.
    uint32_t          envLumWidth = 0;            ///< Luminance mip-zero width.
    uint32_t          envLumHeight = 0;           ///< Luminance mip-zero height.
};

/// Registers the path tracer pass in the render graph.
///
/// The pass clears the HDR scene color and runs a fullscreen pixel shader
/// that writes the path-traced result. Called from the lighting registration
/// path when `r.PathTracer.Enable=1` (which also skips deferred lighting,
/// GI, and shadow passes).
void AddPathTracerPass(rg::RgBuilder& builder, const PathTracerPassDesc& desc);

/// Returns the reset command generation so each renderer can invalidate its history.
uint32_t GetPathTracerResetGeneration();

} // namespace uz
