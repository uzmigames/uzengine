#pragma once

/// Hardware ray-traced reflections — the CPU side of the `rt_reflection_cs.hlsl` binding
/// contract: the root constants at b0, and the fill of the shared b2 trace constants
/// (`RadianceCacheTraceConstants`, the rows `gi_trace_common.hlsli` declares for the path tracer
/// and the DDGI writer) from the global distance field, the frame's sun and skylight, and (stage 2)
/// the radiance-cache probe grid the indirect term reads and the frame index the glossy sample
/// sequence rotates with.
///
/// The pass itself is `SceneRenderer::RegisterRTReflectionPass` (frame/frame_gi_passes.cpp),
/// registered after the SSR passes (or between SSR_Resolve and SSR_Temporal on the denoise route)
/// and gated on `r.RayTracing.Reflections` (Phase B: default on where DXR is supported, with a
/// built TLAS — `RTManager::ReflectionsAvailable`): with the gate off, or without DXR / a TLAS,
/// nothing here is bound, compiled or dispatched and the frame keeps its screen-space / probe
/// reflections (`SelectRTReflectionRoute`). Stage 3: with SSR off (`r.SSR` 0) the pass runs on
/// its own (`SceneRenderer::RTReflectionsStandalone`), owning the reflection buffer the lighting
/// reads (`kRTReflectionFlagNoScreenTrace`), the SSR denoiser chained behind it on the denoise
/// route — the default route, since the engine's `r.SSR` defaults off.
///
/// @ref docs/specs/rendering/rt-reflections.md — design (root budget <= 20 DWORDs, hit attributes)

#include "constant_buffers.h"            // PerFrameCB (sun direction and colour, skylight)
#include "gi/global_distance_field.h"    // GlobalDistanceField::ClipmapData
#include "gi/radiance_cache.h"           // RadianceCacheTraceConstants (the shared b2 layout)

#include <cstddef>
#include <cstdint>

namespace uz {

/// Root constants at b0 of `rt_reflection_cs.hlsl` (bindless indices + the small params block).
struct RTReflectionRootConstants {
    uint32_t depthSrvIdx        = UINT32_MAX;  ///< [0]  GBuffer depth SRV.
    uint32_t normalSrvIdx       = UINT32_MAX;  ///< [1]  GBuffer normal SRV (rg oct normal, b metallic, a roughness).
    uint32_t ssrTraceSrvIdx     = UINT32_MAX;  ///< [2]  This frame's SSR trace output (a = screen confidence); sentinel = unread (kRTReflectionFlagNoScreenTrace).
    uint32_t outputUavIdx       = UINT32_MAX;  ///< [3]  SSR resolved buffer UAV (the texture the lighting blends).
    uint32_t tlasSrvIdx         = UINT32_MAX;  ///< [4]  Scene TLAS SRV.
    uint32_t outputWidth        = 0;           ///< [5]
    uint32_t outputHeight       = 0;           ///< [6]
    float    maxRoughness       = 0.35f;       ///< [7]  ReflectionEligible cutoff (r.RayTracing.Reflections.MaxRoughness, else r.SSR.MaxRoughness).
    float    rayMaxDistance     = 100000.0f;   ///< [8]  cm — ray TMax (r.RayTracing.Reflections.MaxDistance).
    float    intensity          = 1.0f;        ///< [9]  r.SSR.Intensity — blend-weight scale shared with SSR.
    float    roughnessMaskScale = -2.0f / 0.35f; ///< [10] -2 / maxRoughness — the SSR roughness fade.
    uint32_t materialTableSrv   = UINT32_MAX;  ///< [11] GIMeshMaterialGPU table by TLAS InstanceID (sentinel = GDF only).
    uint32_t skyCubemapSrv      = UINT32_MAX;  ///< [12] Sky cubemap for escaping rays (sentinel = flat skylight).
    uint32_t probeShSrv         = UINT32_MAX;  ///< [13] Radiance-cache probe SH buffer (sentinel = no indirect term).
    uint32_t hitDistUavIdx      = UINT32_MAX;  ///< [14] SSR hit-distance UAV (written on the denoise route).
    uint32_t flags              = 0;           ///< [15] rtreflection::kReflectionFlag* bits (glossy, indirect, hit distance, no screen trace, surface cache).
    uint32_t surfaceCacheParamsSrv = UINT32_MAX; ///< [16] Phase D stage 1: StructuredBuffer<SurfaceCacheParams> (1 element) SRV; sentinel = no cache.
};

/// `RTReflectionRootConstants::flags` bits — the shader contract's `rtreflection::kReflectionFlag*`
/// (rt_reflection_contract.hlsli); the gtest pins the two equal.
enum : uint32_t {
    kRTReflectionFlagGlossy      = 1u,   ///< r.RayTracing.Reflections.Glossy: GGX-VNDF sampled ray.
    kRTReflectionFlagIndirect    = 2u,   ///< r.RayTracing.Reflections.Indirect: probe SH at the hit.
    kRTReflectionFlagHitDistance = 4u,   ///< r.RayTracing.Reflections.Denoise: write the ray t for SSR_Temporal.
    /// Stage 3: no screen trace this frame (SSR off, or r.SSR.Trace 0) — every eligible texel is
    /// traced and the pass writes every texel of the buffer (zero weight where it does not trace).
    kRTReflectionFlagNoScreenTrace = 8u,
    /// Phase D stage 1: the surface radiance cache is bound (`surfaceCacheParamsSrv`): a readable
    /// cell replaces the whole hit radiance, a miss shades inline and updates the cell.
    kRTReflectionFlagSurfaceCache = 16u,
};

/// The ray-traced roughness cutoff: `r.RayTracing.Reflections.MaxRoughness` when set, else the
/// screen trace's `r.SSR.MaxRoughness` — the stage 1 range and blend weights (the contract's
/// `ReflectionMaxRoughness`).
inline float RTReflectionMaxRoughness(float rtMaxRoughness, float ssrMaxRoughness) {
    return rtMaxRoughness > 0.0f ? rtMaxRoughness : ssrMaxRoughness;
}

/// Phase B: the reflection route of a frame, from the three facts the renderer knows at
/// registration — whether the screen trace runs this frame (SSR on, active, with a previous
/// colour), whether SSR is switched off (`r.SSR` 0 / ShowFlag::SSR, as opposed to warming up on
/// its first frame), and whether the ray pass is available (`RTManager::ReflectionsAvailable` with
/// the resolved buffer allocated). The fallback is structural: without the rays the route is the
/// route of the frame before Phase B.
enum class RTReflectionRoute : uint8_t {
    None,                ///< No reflection layer: SSR warming up, or off without rays (the lighting keeps its SDF / probe / sky reflections).
    ScreenTraceOnly,     ///< The SSR chain alone — the fallback with rays unavailable (no DXR, empty TLAS, switch off).
    ScreenTraceAndRays,  ///< The SSR chain with the ray pass filling its holes (stages 1-2).
    RaysOnly,            ///< The ray pass owns the reflection buffer (stage 3 standalone; the default with `r.SSR` off).
};

[[nodiscard]] inline RTReflectionRoute SelectRTReflectionRoute(bool screenTraceThisFrame, bool ssrSwitchedOff,
                                                              bool raysAvailable) noexcept {
    if (screenTraceThisFrame) {
        return raysAvailable ? RTReflectionRoute::ScreenTraceAndRays : RTReflectionRoute::ScreenTraceOnly;
    }
    return (ssrSwitchedOff && raysAvailable) ? RTReflectionRoute::RaysOnly : RTReflectionRoute::None;
}

/// DWORDs bound at `kSlotRootConstants`; the engine's bindless root-constant budget is 20.
static constexpr uint32_t kRTReflectionRootDwords = sizeof(RTReflectionRootConstants) / sizeof(uint32_t);
static_assert(sizeof(RTReflectionRootConstants) == 68, "RTReflectionRootConstants is 17 DWORDs");
static_assert(kRTReflectionRootDwords <= 20, "bindless root constants are limited to 20 DWORDs");

/// Writes clipmap `index` (0..3) of the shared trace constants; an index beyond the four rows is
/// ignored. A clipmap left unset keeps the sentinel SRV and extent 0, which `gdf_surface.hlsli`
/// skips.
inline void SetRTReflectionClipmap(RadianceCacheTraceConstants& out, uint32_t index,
                                   const Vec3& center, float extent, uint32_t srvIndex) {
    if (index >= 4u) return;
    float*    centers[4] = {&out.gdfCenter0X, &out.gdfCenter1X, &out.gdfCenter2X, &out.gdfCenter3X};
    float*    extents[4] = {&out.gdfExtent0, &out.gdfExtent1, &out.gdfExtent2, &out.gdfExtent3};
    uint32_t* srvs[4]    = {&out.gdfClipmapSrv0, &out.gdfClipmapSrv1, &out.gdfClipmapSrv2, &out.gdfClipmapSrv3};
    centers[index][0] = center.x;
    centers[index][1] = center.y;
    centers[index][2] = center.z;
    *extents[index]   = extent;
    *srvs[index]      = srvIndex;
}

/// The frame's sun and skylight as the DDGI writer and the path tracer read them:
/// `PerFrameCB::lightDir` points toward the light, `lightColor` is already colour x intensity (so
/// the multiplier is 1), the sky rows carry the skylight colour x intensity every sky read is
/// scaled by, and `groundAlbedo` is the hit albedo outside every GDF cascade without a table entry.
inline void SetRTReflectionLighting(RadianceCacheTraceConstants& out, const PerFrameCB& perFrame) {
    out.skyColorR      = perFrame.skyLightColorR * perFrame.skyLightIntensity;
    out.skyColorG      = perFrame.skyLightColorG * perFrame.skyLightIntensity;
    out.skyColorB      = perFrame.skyLightColorB * perFrame.skyLightIntensity;
    out.lightDirX      = perFrame.lightDir.x;
    out.lightDirY      = perFrame.lightDir.y;
    out.lightDirZ      = perFrame.lightDir.z;
    out.lightIntensity = 1.0f;
    out.lightColorR    = perFrame.lightColor.x;
    out.lightColorG    = perFrame.lightColor.y;
    out.lightColorB    = perFrame.lightColor.z;
    out.groundAlbedo   = 0.18f;
}

/// Stage 2: the radiance-cache probe grid the shader's indirect term indexes the SH buffer with
/// (rows 1-2 of the shared layout). The origin must be the grid the published buffer was traced
/// with (`RadianceCache::GetCompletedGridOrigin()`, not the current origin on a recentre frame) and
/// the spacing `GetProbeSpacing()` = 2 x extent / (probes per axis - 1), the lighting's probe step.
inline void SetRTReflectionProbeGrid(RadianceCacheTraceConstants& out, const Vec3& origin,
                                     float spacing, uint32_t probesPerAxis) {
    out.gridOriginX   = origin.x;
    out.gridOriginY   = origin.y;
    out.gridOriginZ   = origin.z;
    out.probeSpacing  = spacing;
    out.probesPerAxis = probesPerAxis;
    out.totalProbes   = probesPerAxis * probesPerAxis * probesPerAxis;
}

/// Fills the shared trace constants for the reflection pass: every clipmap at the sentinel, then
/// the global distance field's clipmaps (when it is initialised), the sun and the skylight, and the
/// frame index (the glossy sample sequence's temporal rotation). The ray TMax is a root constant,
/// so `maxTraceDistance` is left at zero (unread by the shader); the probe grid rows stay zero
/// (`probesPerAxis` 0 = no indirect term) until `SetRTReflectionProbeGrid`.
inline void FillRTReflectionTraceConstants(RadianceCacheTraceConstants& out,
                                           const GlobalDistanceField& gdf,
                                           const PerFrameCB& perFrame,
                                           uint32_t frameIndex = 0u) {
    out = RadianceCacheTraceConstants{};
    out.gdfClipmapSrv0 = out.gdfClipmapSrv1 = out.gdfClipmapSrv2 = out.gdfClipmapSrv3 = UINT32_MAX;
    out.frameIndex = frameIndex;
    if (gdf.IsInitialized()) {
        out.numGdfClipmaps = GlobalDistanceField::kNumClipmaps;
        for (uint32_t i = 0; i < GlobalDistanceField::kNumClipmaps; ++i) {
            const auto& clip = gdf.GetClipmap(i);
            SetRTReflectionClipmap(out, i, clip.center, clip.extent, clip.srvIndex);
        }
    }
    SetRTReflectionLighting(out, perFrame);
}

} // namespace uz
