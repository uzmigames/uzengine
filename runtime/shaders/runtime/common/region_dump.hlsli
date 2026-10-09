#ifndef UZ_REGION_DUMP_HLSLI
#define UZ_REGION_DUMP_HLSLI

/// Region dump (diagnostics) — shared record layout for the per-pixel captures
/// driven by `--regions` / `SceneRenderer::SetRegionDump`.
///
/// # Rect table
/// A `StructuredBuffer<uint4>` with two elements per rectangle:
///   [2r+0] = (x, y, w, h)                        screen rectangle in OUTPUT-resolution pixels
///   [2r+1] = (baseOffset, recordsPerPixel, 0, 0) float4 offsets inside the dump buffer
/// A pixel's records start at `baseOffset + ((py - y) * w + (px - x)) * recordsPerPixel`.
///
/// # Field groups (fixed order, each present only when its bit is set in `fields`)
///   bit 0 `LIGHTING` (2 records, deferred lighting PS, RENDER resolution domain)
///     +0 SDF reflection radiance (rgb, linear HDR, before the SSR blend) + roughness
///     +1 shading normal (xyz, world) + view distance (cm); (0,0,0,-1) on both = sky/no geometry
///   bit 1 `TAA` (5 records, TAA resolve CS, OUTPUT resolution domain, linear HDR RGB)
///     +0 raw centre sample (rgb) + final blend weight
///     +1 plus-filtered, jitter-weighted colour (rgb) + velocity.x (output px)
///     +2 reprojected history before the clamp (rgb) + velocity.y (output px)
///     +3 clamped history (rgb) + 1.0 when the history was rejected as off-screen
///     +4 TAA output (rgb) + the exposure the HDR weights used
///   bit 2 `OUTPUT` (1 record, TAA resolve CS) — the cheap preset for long sequences
///     +0 TAA output (rgb) + the exposure the HDR weights used
///   bit 4 `LIGHTING_EXTRA` (13 records: +0..+4 and +11..+12 deferred lighting PS, +5..+10 forward translucent
///        PS) — the lighting components, so their
///        composition can be checked against the scene colour the TAA reads
///     +0 direct light after shadowing (rgb) + shadow visibility (0..1)
///     +1 diffuse indirect as composited, i.e. after AO and sky tint (rgb) + ambient occlusion
///     +2 specular indirect as composited, i.e. reflections after the BRDF and specular occlusion
///        (rgb) + metallic
///     +3 emissive + environment MIS (rgb) + the active-path bitmask (asfloat of a uint)
///     +5..+10 belong to the forward translucent pass (the deferred pass writes a sentinel into all
///        six first, every frame, so an uncovered pixel never shows stale values):
///     +5 source colour before premultiplication (rgb) + opacity; -1 = not drawn this frame
///     +6 albedo as consumed (rgb) + albedo SRV index (asfloat of a uint)
///     +7 IBL diffuse as consumed (rgb) + irradiance SRV index (asfloat of a uint)
///     +8 shading normal as consumed (xyz) + shading-model id (asfloat of a uint)
///     +9 emission as consumed (rgb) + N.L
///     +10 x = translucent layers: fragments that passed the depth test at this pixel this frame
///         (every rasterised one when r.Translucency.DepthTest=0); y = depth (SV_Position.z) of the
///         last layer; z = the OPAQUE depth at this pixel, written by the deferred pass (raw
///         reversed-Z, the same space as y, and NOT the TAA record's dilated centre depth); w 0.
///         Read-modify-write, so overlap inside one draw call can be undercounted when
///         invocations run concurrently; it is never overcounted. +5..+9 hold the LAST layer.
///   Path bits: 1 SDF reflections, 2 SDF diffuse gather, 4 screen-space SSGI, 8 Brixelizer GI,
///   16 environment MIS, 32 aerial perspective, 64 glass/transmission branch, 128 SSR blended in.
///   bit 5 `GATHER` (9 records, SSGI temporal accumulation + bilateral upsample) — the gather's
///        own internal stages. Records 0-4 come from the half-resolution accumulator and describe
///        gather pixel `floor(p/2)`, which is tap (0,0) of the reconstruction below; records 5-8
///        come from the full-resolution upsample. RENDER resolution domain, like LIGHTING.
///     +0 current input, i.e. the spatially denoised trace (rgb) + its ambient occlusion
///     +1 reprojected history (rgb, already divided by the bilinear weight sum) + the sample count
///        that history carried
///     +2 accumulated output (rgb) + the effective alpha the blend used
///     +3 rejection code (asfloat of a uint, see below), gather pixel x, gather pixel y, and the
///        sample count the pixel now carries
///     +4 the accumulator's bilinear weight sum (< 1 means taps were rejected by the plane test),
///        reprojected history AO, output AO, and the `r.SSGI.TemporalAlpha` floor
///     +5 the four bilateral upsample tap weights, in the order (0,0) (1,0) (0,1) (1,1); all zero
///        when no tap passed the receiver-plane test
///     +6 upsample base x, base y, fraction x, fraction y — the reconstruction's own mapping
///     +7 gather width, gather height, full width, full height — the effective resolutions
///     +8 the upsampled value the lighting consumes (rgb) + the upsample state: 0 bilateral,
///        1 sky, 2 world-gather fallback, 3 no tap and no fallback
///     Records 9-16 come from the trace and identify what the gather sampled and what it read,
///     so alternating content can be told from alternating selection:
///     +9 the world position the gather resolved (xyz) + its depth
///     +10 the ray origin after `ResolveSdfRayOrigin` (xyz) + 1 when it resolved, 0 when it did not
///     +11 the geometric normal (xyz) + the hash seed (asfloat of a uint)
///     +12 the first ray direction (xyz) + the sample count
///     +13 the frame index the shader saw, then clipmap SRVs 0, 1 and 2 (all asfloat of uints)
///     +14 clipmap SRV 3, the probe-volume SRV, the sky SRV, raw clipmap SRV 0
///     +15 a probe origin snapped to a fixed world grid (xyz) + the grid cell size — deliberately
///         out of the jitter's reach, and recorded so the control can be checked for constancy
///     +16 the radiance that probe returned along a fixed world axis (rgb) + 1 when written
///     +17 comes from the accumulator: history SRV, history-geometry SRV, output UAV, history-valid
///        flag (all asfloat of uints) — the ping-pong index shows itself in these
///     +19..26 one record per ray of the diffuse gather, up to eight: the world-space direction
///         (xyz) + that ray's radiance contribution as luminance. The estimator is a plain mean —
///         the cosine PDF cancels — so every ray's weight is 1/count and the contributions must
///         average to record 27; a set that does not average to it is not the set that was traced.
///     +27 the raw trace output before the spatial denoise (rgb) + the ray count actually recorded
///     +28 the frame rotation the gather actually applied (xy) + the rotation mode
///         (`r.GI.FinalGather.SampleRotation`, asfloat of a uint). Recorded so a capture proves
///         which sampling path ran, rather than the CVar merely having been passed
///     +18 the probe volume read directly at the snapped position (rgb) + 1 when the snapped probe
///         ray hit geometry, 0 when it escaped to the sky. A traced ray only reaches the probe
///         volume on a hit, so this reads the alternating resource unconditionally and the flag
///         says whether record 16 reached it at all
///   Rejection codes: 0 accepted, 1 sky depth, 2 history invalid, 3 behind the camera,
///   4 reprojected off-screen, 5 zero bilinear weight sum. A code is recorded because a count of 1
///   alone cannot distinguish a rejected history from an initialised one.
///   bit 3 `TAA_EXTRA` (4 records, TAA resolve CS) — the clamp box, the reconstruction taps
///     and the history sample position the shader actually used (pixels) with its
///     Catmull-Rom interpolation fractions
///     +0 clip box minimum (rgb) + centre depth (reversed-Z)
///     +1 clip box maximum (rgb) + velocity magnitude (output px)
///     +2 the four plus-filter neighbour lumas (YCoCg Y, 4x scaled): up, left, right, down
///
/// All colours are linear HDR scene radiance, never pre-exposed and never tonemapped.
///
/// # Footer (4 float4 immediately after a rect's pixel records)
///   [0] lighting stamp: (asfloat(frameStamp), renderWidth, renderHeight, 1)
///   [1] TAA stamp:      (asfloat(frameStamp), outputWidth, outputHeight, 1)
///   [2] gather stamp:   (asfloat(frameStamp), gatherWidth, gatherHeight, 1) — absent when the
///       SSGI passes did not run, which is a legitimate state (disabled, or frozen by
///       `r.GI.FinalGather.FreezeAfterFrame`) and is reported rather than treated as corruption;
///       a stamp that disagrees with the frame is never legitimate and invalidates the file
///   [3] reserved
/// Each stamp is written by the single thread that owns the rect's origin pixel. The CPU
/// compares them with the frame it believes it read back; any mismatch or missing stamp
/// invalidates that file (`RegionFileHeader::flags`), so a stale or partially written
/// readback can never be analysed as if it were a real frame.

#define UZ_REGION_FIELD_LIGHTING  1u
#define UZ_REGION_FIELD_TAA       2u
#define UZ_REGION_FIELD_OUTPUT    4u
#define UZ_REGION_FIELD_TAA_EXTRA 8u
#define UZ_REGION_FIELD_LIGHTING_EXTRA 16u
#define UZ_REGION_FIELD_GATHER 32u

#define UZ_REGION_LIGHTING_RECORDS 2u
#define UZ_REGION_TAA_RECORDS      5u
#define UZ_REGION_OUTPUT_RECORDS   1u
#define UZ_REGION_TAA_EXTRA_RECORDS 11u
// TAA_EXTRA +4..+10 (2026-09-16, followup-12): what the plus-filter reconstruction consumed.
//   +4..+8 the five taps (up, left, centre, right, down): linear RGB + the weight applied (w)
//   +9     jitter offset the filter consumed (xy, px), sum of weights (z), spatial multiplier (w)
//   +10    exposure scale (x), HDR weight exponent (y), reconstruction mode (z)
// Mode 3 uses nine taps; its plus weights are recorded as 0 and z of +10 says 3.
// LIGHTING_EXTRA +11..+12 (2026-09-18, followup-01 review item 2): the support of the two probe
// queries at the pixel, so a zero can be told from an unsupported query. Nothing reads these.
//   +11 cache: x surviving weight sum, y probes with a facing weight > 0, z probes valid (not
//              buried), w probes surviving both rules (the ones that contributed)
//   +12 DDGI:  x visibility-weighted weight sum, y probes with Chebyshev visibility > 1e-3,
//              z probes with visibility <= 1e-3, w probes with a trilinear weight > 0
#define UZ_REGION_LIGHTING_EXTRA_RECORDS 13u
#define UZ_REGION_GATHER_RECORDS 31u
// GATHER +29..+30 (2026-09-16, followup-13): +29 x = fraction of hits inside clipmap 0, y = fraction
// in a coarser cascade, z = min hit distance to a clipmap-0 face in voxels (negative outside),
// w = fraction escaped; +30 clipmap 0 centre (xyz) and half-extent (w) as the shader saw them.
#define UZ_REGION_FOOTER_RECORDS   4u

/// Record offset of the lighting group inside a pixel's records.
uint UzRegionLightingOffset(uint fields) { return 0u; }

/// Record offset of the TAA group inside a pixel's records.
uint UzRegionTaaOffset(uint fields) {
    return ((fields & UZ_REGION_FIELD_LIGHTING) != 0u) ? UZ_REGION_LIGHTING_RECORDS : 0u;
}

/// Record offset of the single-record output group inside a pixel's records.
uint UzRegionOutputOffset(uint fields) {
    uint o = UzRegionTaaOffset(fields);
    if ((fields & UZ_REGION_FIELD_TAA) != 0u) o += UZ_REGION_TAA_RECORDS;
    return o;
}

/// Record offset of the clamp-box / reconstruction-tap group inside a pixel's records.
uint UzRegionTaaExtraOffset(uint fields) {
    uint o = UzRegionOutputOffset(fields);
    if ((fields & UZ_REGION_FIELD_OUTPUT) != 0u) o += UZ_REGION_OUTPUT_RECORDS;
    return o;
}

/// Record offset of the lighting-component group inside a pixel's records.
uint UzRegionLightingExtraOffset(uint fields) {
    uint o = UzRegionTaaExtraOffset(fields);
    if ((fields & UZ_REGION_FIELD_TAA_EXTRA) != 0u) o += UZ_REGION_TAA_EXTRA_RECORDS;
    return o;
}

/// Record offset of the GATHER group (the SSGI/final-gather internal stages).
uint UzRegionGatherOffset(uint fields) {
    uint o = UzRegionLightingExtraOffset(fields);
    if ((fields & UZ_REGION_FIELD_LIGHTING_EXTRA) != 0u) o += UZ_REGION_LIGHTING_EXTRA_RECORDS;
    return o;
}

/// True when `p` is inside the rectangle `a` = (x, y, w, h).
bool UzRegionContains(uint4 a, uint2 p) {
    return p.x >= a.x && p.y >= a.y && p.x < a.x + a.z && p.y < a.y + a.w;
}

/// First record index of pixel `p` in rectangle (`a`, `b`).
uint UzRegionPixelBase(uint4 a, uint4 b, uint2 p) {
    return b.x + ((p.y - a.y) * a.z + (p.x - a.x)) * b.y;
}

/// First footer record index of rectangle (`a`, `b`).
uint UzRegionFooterBase(uint4 a, uint4 b) {
    return b.x + a.z * a.w * b.y;
}

#endif // UZ_REGION_DUMP_HLSLI
