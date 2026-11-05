# 05 - Variance reduction before integration: a Lumen-shaped gather layer for UzEngine

Date: 2026-11-02. Branch `feat/soft-shadows-area-lights`, HEAD `94014f84` (radiance-cache
stage-1: JitterCycle + hysteresis blend + classification hysteresis, all default OFF). Design
study, read-only: no code was changed, nothing was built or captured. Lumen reference:
`E:/UnrealGITHUB` (read-only), paths relative to `Engine/Shaders/Private/Lumen/` (`.usf`/`.ush`)
and `Engine/Source/Runtime/Renderer/Private/Lumen/` (`.cpp`). UzEngine paths relative to
`uzengine/`. Every number is a default read from source, not a measurement; the few estimates are
labelled as such.

Companion to 01 (Lumen mechanisms), 02 (the radiance-cache crawl and its producer-side fix), 03
(the parity audit) and 04 (DDGI/RTXGI mapping). 03 concluded that the deeper gap is not a missing
temporal filter but the absence of variance reduction *before* the per-pixel integral. This
document designs that layer.

## 0. Answer first

UzEngine's final gather is a per-pixel estimator: 8 cosine-weighted rays per half-resolution
pixel, averaged into E/pi, blurred once as irradiance over a 5x5 receiver-plane kernel, then
accumulated for up to 64 frames (unbounded with `StableTemporal`, default ON). Every ray is
spent on exactly one receiver, the direction set is the same cosine distribution regardless of
where the light is, the first and only filter runs on the *integrated* quantity, and the history
length is the only knob that lowers the visible noise. Lumen's gather is a probe estimator: 64
rays per 16x16-pixel probe, directions chosen from BRDF x incoming lighting, every ray clamped,
radiance shared between neighbouring probes *per direction* and only where their hits agree in
angle, then converted to irradiance, interpolated to pixels stochastically with a jittered probe
grid, and accumulated for at most 10 frames. Lumen's per-pixel estimate reaches its temporal
filter with roughly two orders of magnitude more (correlated) samples behind it than UzEngine's,
at about one eighth of the ray count.

Three mechanisms do that work, and all three are missing in UzEngine today:

| Mechanism | Lumen | UzEngine today | What it buys |
|---|---|---|---|
| (a) Importance sampling of gather rays | BRDF SH x last frame's filtered probe radiance (radiance cache as fallback), culled below `MinPDFToTrace`, culled budget re-spent on 2x2 refinement of the brightest texels (`LumenScreenProbeImportanceSampling.usf`) | cosine hemisphere, Hammersley + Cranley-Patterson rotation (`sdf_diffuse_gather.hlsli:98`) | fewer rays wasted below the horizon and on dark directions; temporal *reuse* without blending (history steers rays, never mixes into the result) |
| (b) Inter-probe spatial filter on radiance, hit-angle gated | 3 passes, 4-neighbour cross, weight = depth-proximity x (1 - angle(neighbour hit)/10 deg), neighbour hit clamped to own (`LumenScreenProbeFiltering.usf:299-382`) | 5x5 kernel on E/pi after integration, receiver-plane weight only (`ssgi_spatial_denoise_cs.hlsl:38-50`) | averages far-field noise across up to a radius-3 diamond of probes per direction while leaving contact occlusion untouched; blurs noise, not signal |
| (c) Firefly clamp + stochastic interpolation + structured jitter, left to TSR | `MaxRayIntensity` 10 (pre-exposed) before any averaging; 1-of-4 probe pick with blue noise; +-16 px interpolation jitter accepted only on the pixel's plane; 10-frame history (`LumenScreenProbeFiltering.usf:79-85`, `LumenScreenProbeGather.usf:1169-1264`) | no clamp; bilateral 4-tap upsample; 64-frame to unbounded history; TAA default OFF (`frame_setup.cpp:48`) | bounds the per-ray variance; converts structured interpolation error into high-frequency noise a temporal AA removes in a few frames; removes the need for long history |

Two designs were weighed (section 3): **A**, keep the per-pixel lattice and bolt on (a) and (c);
**B**, restructure the gather into screen probes and take all three. A cannot implement (b) at
all (per-pixel direction sets do not line up, and per-ray storage at half resolution is 8x the
traffic), and (b) is the item that carries the most variance reduction. **Recommendation: B,
built in five default-off stages**, the first of which (the firefly clamp) is a one-line change
that A and B share. The probe lane runs beside the existing lane, is bit-exact when off, reuses
the SDF/DXR tracer, the hit shading, the history reprojection and the bilateral upsample, and at
`DownsampleFactor` 16 its probe atlas has exactly the texel count of today's half-resolution
trace texture.

It complements the radiance-cache stage-1/2 work rather than replacing it (section 4): the
gather layer removes *Monte-Carlo* variance; the radiance-cache work removes a *coherent*
low-frequency wander that no amount of inter-probe averaging can touch. It freezes
`StableTemporal` out of the probe lane (10-frame cap) and leaves ReSTIR-GI as a mutually
exclusive alternative on the per-pixel lane.

## 1. Gap statement: the gather today versus Lumen's

### 1.1 UzEngine, from the code

Pass order (`gi/ssgi_renderer.cpp:436-460`): `DispatchTrace` (or `DispatchRestir` when
`r.GI.ReSTIR` >= 1, `:446-451`) -> `DispatchSpatialDenoise` (`:454`) -> `DispatchTemporalAccum`
(`:457`; the stable variant when `r.GI.FinalGather.StableTemporal`, `:1066-1067`) ->
`DispatchUpsample` (`:460`). All at half resolution (`m_halfWidth = (width + 1) / 2`,
`:113-114`); the half-res lattice maps to full pixel `2p` (`ssgi/ssgi_surface.hlsli:78`,
`SurfacePixel`), with no per-frame lattice jitter.

Trace (`ssgi/ssgi_trace_cs.hlsl`): the world-gather branch (`sdfDiffuseSamples != 0`, `:184`)
calls `GatherSurfaceLightingIdentified` (`:192`) and writes E/pi (`:195`). The legacy
screen-space branch (`:236-283`) is the same estimator shape with `InterleavedGradientNoise`
rotation and a linear depth march; it is not on the `r.GI.FinalGather` path and is not discussed
further.

The estimator (`gi/sdf_diffuse_gather.hlsli:68-125`):

- ray count `count = clamp(samples, 1, 32)` (`:90`), `r.GI.FinalGather.Samples` default 8
  (`frame/frame_constants.cpp:85`), reported as "the shipping sample count" (`:58-60`);
- direction `SampleCosineHemisphere(frac(Hammersley(i, count) + rotation))` (`:98`), rotation
  from `SdfDiffuseRotation(pixel, frame, mode)` (`:32-56`): mode 0 (shipping,
  `r.GI.FinalGather.SampleRotation` default 0, `frame_constants.cpp:91`) hashes pixel and frame
  through a base-2 radical inverse; mode 1 is an R2 Kronecker advance per frame; mode 2 is a
  white-noise reference. All three are Cranley-Patterson rotations of the same cosine Hammersley
  set: **the direction density never depends on where the light is**;
- per ray `TraceSdfSurfaceRadianceIdentified(scene, origin, direction, 0u, geometricNormal,
  hitT)` (`:105`), or `TraceDxrGatherRadiance` under `UZ_GATHER_DXR` (`:103`); the hit is shaded
  by `ShadeSdfReflectionHit` (`gi/sdf_reflections.hlsli:213-252`): field-gradient normal, sun x
  one shadow ray (`:237`), per-mesh-average albedo, plus the radiance-cache SH irradiance
  `SdfReflectionIrradiance` (`:165-199`) read at **every** hit distance (`:251-252`); escaped
  rays read the sky cube (`:262-264`);
- the result is the plain mean `irradianceOverPi / count` (`:124`; the comment at `:66-67` says
  so explicitly: "Every ray carries the same weight, 1/count"). **No intensity clamp anywhere on
  the diffuse path.** The reflection gather has a Karis weight (`sdf_reflections.hlsli:332`,
  `scene.plainMean`), the diffuse gather deliberately does not (`ssgi_world_gather.hlsli:29`:
  "diffuse gather does not use the reflection estimator's weight").

Spatial filter (`ssgi/ssgi_spatial_denoise_cs.hlsl`): 5x5 taps on the **integrated E/pi**
(`:38-50`), weight `exp(-r^2/8) * SurfaceWeight(position, normal, samplePosition, sampleNormal,
sigmaNormal)` (`:44`), where `SurfaceWeight` is a receiver-plane distance term times a normal
cosine power (`ssgi_surface.hlsli:100-104`; sigma 128 from `ssgi_renderer.cpp:1016`). The header
states the design choice: "No color edge stop: the input is noisy E/pi, not albedo, and isolated
bright samples must be averaged" (`:17-18`). The kernel knows the receiver's geometry only; it
cannot know whether two receivers' rays saw the same thing, so it blurs irradiance detail (contact
darkening, shadow edges in the indirect term) at the same rate as it blurs noise.

Temporal (`ssgi/ssgi_temporal_accum_cs.hlsl`): camera-only reprojection through
`previousViewProj` (`:21-22`, `:63-67`), four bilinear taps gated by `SurfaceCompatible`
(`:72-86`), history rejected inside distance-field change boxes (`:31-36`, `:167-168`), then
`count = min(historyCount + 1, 64)` and `alpha = max(1/count, temporalAlpha)` (`:179-181`;
`r.SSGI.TemporalAlpha` 0.05, `ssgi_renderer.cpp:45`). The stable variant (default ON,
`ssgi_renderer.cpp:46-47`) removes the 64 cap once 16 consecutive quiet observations agree
(`ssgi/ssgi_stable_temporal.hlsli:19-28`, confidence rule `:15-17`). The upsample reconstructs
full resolution from the four surrounding half-res taps with the same plane weight
(`ssgi/ssgi_upsample_cs.hlsl:88-99`). TAA is `false` by default (`frame/frame_setup.cpp:48`; the
quality presets turn it on from the second tier up, `config/quality_presets.cpp:79`).

### 1.2 Lumen, from the code

Placement: one probe per `r.Lumen.ScreenProbeGather.DownsampleFactor` = 16 px tile
(`LumenScreenProbeGather.cpp:86-92`), at `tile * 16 + GetScreenTileJitter(frame % 8)` where the
jitter is `Hammersley16(i, 8, 0) * DownsampleFactor` (`LumenScreenProbeCommon.ush:72-75`,
`:122-125`): the probe lattice itself moves every frame on an 8-frame cycle. Adaptive probes fill
tiles whose four corners fail the plane test (`LumenScreenProbeGather.usf:1744-1790`,
`NumAdaptiveProbes` 8, `AdaptiveProbeAllocationFraction` 0.5, `Gather.cpp:47-56`).

Rays: `TracingOctahedronResolution` = 8 -> 64 per probe (`Gather.cpp:70-76`), gathered at the
same resolution (`GatherOctahedronResolutionScale` 1, `:78-84`). Texel-centre jitter is blue
noise per tile indexed by `ScreenProbeRayDirectionFrameIndex = StateFrameIndex % MaxRayDirections
(8)` (`LumenScreenProbeTracingCommon.ush:8-22`, `Gather.cpp:2281`, `:235-239`; the CVar help:
"Should be tweaked based on MaxFramesAccumulated"). Directions come from
`EquiAreaSphericalMapping(ProbeUV)` (`TracingCommon.ush:138`) over the **full sphere**; the
importance sampler (section 2.1) is what keeps the below-horizon half from being traced.

Then, in order (`LumenScreenProbeFiltering.cpp` event names: `CompositeTraces` `:484`,
`CalculateMoving` `:513`, `TemporallyAccumulateRadiance` `:567` (off by default),
`FilterRadianceWithGather` x3 `:585-611`, `ScreenProbeConvertToIrradiance` `:705`, `FixupBorders`
`:729`, `GenerateMip` `:752`), then `ScreenProbeIntegrateCS` (`LumenScreenProbeGather.usf:1130`)
and `ScreenProbeTemporalReprojectionCS` (`LumenScreenProbeGatherTemporal.usf:289`) with
`MaxFramesAccumulated` 10 (`Gather.cpp:227-233`), `Alpha = 1 / (1 + N)` (`Temporal.usf:522`).

### 1.3 The budget, side by side (1080p, defaults)

| | UzEngine | Lumen |
|---|---|---|
| Receivers estimated per frame | 960 x 540 half-res pixels | 120 x 68 = 8160 uniform probes (+ up to 50 % adaptive) |
| Rays per receiver | 8, cosine | 64, importance sampled over the sphere |
| Rays per frame | ~4.1 M sphere traces | ~0.52 M (+ adaptive) |
| Clamp before averaging | none | `MaxRayIntensity` 10 x pre-exposure |
| Samples shared before integration | none (E/pi blurred after) | per direction, up to a radius-3 diamond of probes, hit-angle gated |
| Interpolation to pixels | bilateral 4-tap upsample, fixed lattice | 4-probe plane-weighted, or 1 stochastic pick; lattice jittered on 8 frames; +-16 px pixel jitter |
| Temporal length | 64 frames; unbounded in stable mode | 10 frames, shortened where traces hit moving geometry |
| Final integrator | TAA off by default | TSR assumed |

Order-of-magnitude estimate (Bernoulli half-lit-hemisphere model, no importance-sampling gain
counted, labelled as an estimate): UzEngine's 8-ray mean has ~35 % relative standard deviation
per pixel per frame; the 5x5 kernel's effective sample count is ~23 taps (weights `exp(-r^2/8)`),
giving ~7 % before the temporal pass, ~1 % after 64 frames. Lumen's 64 rays give ~12 % per
probe; averaging over ~10 effective probes per direction in the far field gives ~4 % per pixel
before the temporal pass, ~1.2 % after 10 frames - the same floor with one sixth of the history
and one eighth of the rays. The pre-temporal number is the one the design must move; the
post-temporal number is what today's history length hides.

## 2. The three mechanisms

### 2.1 (a) Importance sampling of the gather rays: BRDF x incoming lighting

**Lumen's algorithm.** Three passes, all default on (`LumenScreenProbeImportanceSampling.cpp:12-18`
`ImportanceSample` 1, `:28-34` `IncomingLighting` 1, `:36-42` `ProbeRadianceHistory` 1).

1. *BRDF PDF per probe* (`LumenScreenProbeGatherScreenData.usf:128-148`, `:202-207`): every
   pixel of the probe's tile whose plane weight passes (`DepthWeight > 0.01`, `:130`) adds
   `CalcDiffuseTransferSH3(Material.WorldNormal, 1.0)` (`:145`) - the clamped-cosine lobe of its
   normal as a 9-coefficient L2 SH (`LumenScreenProbeImportanceSamplingShared.ush:5`) - into
   group-shared storage; the sum is normalised and written per probe (`:207`). Tiles that mix
   normals (edges, curved surfaces) get a broad lobe; flat tiles a sharp one. The same pass flags
   the probe as *disoccluded* when >= `SpatialFilter.DisocclusionFraction` (0.4) of its pixels
   have `NumFramesAccumulated < SpatialFilter.DisocclusionMaxFrames` (4) in last frame's
   temporal history (`:162-172`, `:213`; CVars `LumenScreenProbeFiltering.cpp:21-34`).
2. *Lighting PDF per probe texel* (`ScreenProbeComputeLightingProbabilityDensityFunctionCS`,
   `LumenScreenProbeImportanceSampling.usf:37-193`): for each of the 64 texel directions,
   reproject the probe into last frame's probe grid (`:67-82`), take the 2x2 neighbouring history
   probes whose world position lies on this probe's plane (`exp2(-10000 * (d/z)^2) > 0.1`,
   `:101-103`), and average **last frame's filtered probe radiance** in that texel (`:106-115`).
   Where fewer than four history probes are valid, the remainder comes from the world radiance
   cache sampled in the cone direction (`:127-136`, `Transparency = 1 - saturate(weight/4)`,
   `:118`); with no cache, a constant (`:141`). `PDF = Luminance(Lighting)` (`:145`), normalised
   over the probe by a group reduction (`:155-190`).
3. *Ray generation* (`ScreenProbeGenerateRaysCS`, `:307-495`): per texel, `PDF = max(DotSH3(BRDF,
   SHBasis(direction)), 0)` (`:341-357`); if `IncomingLighting`, `PDF *= LightingPDF * 64`
   (`:363-368`), but a texel the BRDF did not cull is floored at `MinPDFToTrace` (0.1, `.cpp:52-58`)
   so a dark-but-visible direction is still traced (`:370-374`). Rays are sorted by PDF
   (`:394-423`); then, for every **three** rays with `PDF < MinPDFToTrace`, the highest-PDF
   remaining texel is subdivided into its four children at the next level (`:429-478`) - the three
   culled rays and the parent become four rays in the bright texel. The result is one `RayInfo`
   (texel, level; 16 bits, `TracingCommon.ush:67-78`) per ray (`:483-492`). The tracer turns that
   into a UV, cone angle and direction (`GetProbeTracingUV`, `TracingCommon.ush:80-107`); the
   composite weights a level-0 ray by `(8 / MipSize)^2` = 1/4 into its parent 8x8 texel
   (`LumenScreenProbeFiltering.usf:65-77`), so the estimator stays an unbiased per-texel mean.

Why it matters for stability as well as noise: the history influences *which directions are
traced*, never *what value is stored*. A wrong history costs variance for one frame, not a
ghost; the CVar help makes the alternative explicit ("When disabled, the Radiance Cache will be
used instead", `.cpp:40`). Below-horizon texels (BRDF PDF ~ 0) are always culled, which is why a
full-sphere octahedron costs no more than a hemisphere.

**Adaptation to UzEngine.** The per-pixel lattice has no per-direction history, so only the
radiance-cache branch of step 2 is available there (design A, section 3.1). In the probe lane
(design B) the mapping is one to one:

- Step 1 reads the GBuffer normal (`normalTex`, oct-decoded as in `ssgi_trace_cs.hlsl:176-177`)
  and depth of the 16x16 tile around the probe pixel, keeps pixels that pass `SurfaceCompatible`
  (`ssgi_surface.hlsli:94-98`) against the probe's plane, and accumulates the analytic L2
  clamped-cosine SH of each normal (zonal coefficients pi, 2pi/3, pi/4 times the real SH basis of
  the normal - the same basis `EvalSH_L2` already evaluates in the radiance-cache consumer). 9
  floats per probe. The disocclusion flag reads the temporal accumulator's `count` channel
  (`ssgi_temporal_accum_cs.hlsl:184` writes `float4(result, count)`) through the existing history
  reprojection.
- Step 2 reprojects with `previousViewProj` from `HistoryCB` (`ssgi_temporal_accum_cs.hlsl:22`),
  reads last frame's **filtered** probe radiance atlas (section 2.2 makes it ping-ponged), and
  falls back to `EvalSH_L2(rcProbeSH, direction)` - the L2 SH the radiance cache stores *is*
  incoming radiance over the sphere, so its evaluation in a direction is exactly Lumen's
  `SampleRadianceCacheInterpolated(...).Radiance` fallback. Floor at a small epsilon so the PDF is
  never zero where the BRDF is not.
- Step 3 is a straight port: 64 threads per probe, LDS sort, three-for-one refinement, one
  `R16_UINT` `RayInfo` texel per ray. `EquiAreaSphericalMapping` (Clarberg's equal-area octahedral
  map) is ~20 lines; UzEngine's `OctEncode/OctDecode` (`common/encoding.hlsli`) is the standard
  non-equal-area octahedral map and must **not** be used for the texel-to-direction mapping, or
  texels would carry unequal solid angles and the plain-mean composite would be biased.
- The tracer loop body is unchanged: `TraceSdfSurfaceRadianceIdentified` /
  `TraceDxrGatherRadiance` per ray, `hitT` as the hit distance. The cosine PDF cancellation at
  `sdf_diffuse_gather.hlsli:66-67` no longer applies; the probe stores *radiance* per texel and
  the cosine enters at integration (section 2.3), as in Lumen.

Cost: at `DownsampleFactor` 16 the lighting-PDF and ray-generation passes run on 8160 groups of
64 threads - the texel count of today's half-res trace texture. The LDS sort is O(64^2) per
group, which Lumen ships.

### 2.2 (b) Inter-probe spatial filter on radiance with a hit-angle gate

**Lumen's algorithm.** `ScreenProbeFilterGatherTracesCS` (`LumenScreenProbeFiltering.usf:388-506`),
run `SpatialFilterNumPasses` = 3 times (`Filtering.cpp:13`, loop `:585-611`) with
`SpatialFilterHalfKernelSize` 1 (`:36-42`), on the composited radiance (after the clamp of 2.3).
Per probe texel (one thread per direction):

```
self:        TotalRadiance = own texel, TotalWeight = 1   (only if own hit distance >= 0)   :442-451
for each of the 4 cross neighbours (-1,0)(1,0)(0,-1)(0,1):                                 :453-464
   PositionWeight = exp2(-1000 * ((z_n - z) / z)^2)   (0 if neighbour depth invalid)        :292-297, :316
   if PositionWeight > 0 and neighbour's texel hit distance >= 0:                          :346-351
       d_n = min(neighbourHitDistance, ownHitDistance)      // contact-shadow preservation :357-361
       hit_n = neighbourPos + dir * d_n                                                     :362-364
       angle = acos(dot(hit_n - ownPos, dir) / |hit_n - ownPos|)                           :365-367
       AngleWeight = 1 - saturate(angle / MaxRadianceHitAngle)   // 10 deg -> rad          :368
       accumulate neighbour radiance * PositionWeight * AngleWeight                        :371-378
if ScreenProbeMoving > 0.01: 8 more taps at radius 2 and the diagonals, AngleWeight = 1    :423-428, :466-483
if probe flagged disoccluded: AngleWeight = 1                                              :413-421
write TotalRadiance / TotalWeight, blue-noise quantised                                    :485-491
```

`SpatialFilterMaxRadianceHitAngle` is 10 degrees (`Filtering.cpp:44-50`, "Larger angles allow
more filtering but lose contact shadows") and is converted to radians on the C++ side
(`:597`, `Clamp(angle * PI / 180, 0, PI)`) - unlike the world-cache filter noted in 01 section
2.6, this one gets its units right. `SpatialFilterPositionWeightScale` 1000 (`:52-58`) puts the
depth gate at ~2.6 % relative depth for 1/e. Hit distances come from the composite pass, which
keeps the **minimum** hit distance of the rays landing in each texel (`Filtering.usf:130`,
`:144`, sqrt-encoded to 8 bits, `TracingCommon.ush:48-58`); a texel with no valid ray stores -1
(`:146-149`) and is neither a filter source nor a filter target.

The geometry of the gate is the whole point. For a direction whose hit is far (the opposite wall
of a room), the neighbour probe 16 px away sees that wall at almost the same angle: the angle
test passes and the two probes average - and averaging *radiance arriving from the same distant
surface* is physically right, even across a floor/wall corner where the receivers' normals
differ. For a direction whose hit is near (the underside of a table 20 cm above the floor
probe), the neighbour's hit point lies many degrees off the own direction: weight 0, the contact
occlusion stays sharp. A receiver-space filter on irradiance cannot make that distinction because
the distance information is gone by then. Clamping the neighbour's hit distance to one's own
(`:357-361`) closes the remaining hole: a neighbour that saw *past* the occluder would otherwise
report a far hit, a small angle, and leak distant light into the contact.

Widening rules couple the filter to the temporal state: where traces hit moving geometry
(`ScreenProbeMoving`, from the per-ray `bMoving` fraction, `Filtering.usf:129`, `:143`,
`:1007-1075`) the temporal filter is about to shorten, so the spatial filter widens to 13 taps
and drops the angle gate; where the probe was disoccluded last frame (history shorter than 4
frames), the gate is dropped for the first frames. "Increase spatial filtering when temporal
filter will be reduced" (`:466`).

After the three passes, `ScreenProbeConvertToIrradianceCS` (`:598`) integrates each probe's 8x8
radiance into either a 6x6 octahedral irradiance map (`IrradianceFormat` 1, default) or an SH3,
with a wrap border and one mip (`FixupBorders`, `GenerateMip`).

**Adaptation to UzEngine.** The pass is a direct port once the probe atlas exists:

- Inputs: the composited radiance atlas (half-res texel count at factor 16; `R11G11B10F` as Lumen
  or `RGBA16F` to reuse `m_ssgiTrace`'s format), a hit-distance atlas (`R16F`, or Lumen's
  sqrt-encoded `R8`), the probe depth/normal texture at probe resolution, the per-probe moving
  and disocclusion flags. Hit distance is `hitT` from `TraceSdfSurfaceRadianceIdentified`
  (`sdf_reflections.hlsli:255-260`): `>= 0` hit, `-1` escaped (store `MaxHitDistance` so sky
  texels filter freely, as Lumen's `GetProbeMaxHitDistance()` does, `Common.ush:234`), `-2`
  exhausted (store -1: invalid, never shared).
- Position weight uses the probe depth exactly as Lumen (`GetFilterPositionWeight`); the plane
  tolerance helper already in `ssgi_surface.hlsli:92` is a receiver-plane rule, not a depth-ratio
  rule, and is not the right gate here - keep Lumen's relative-depth form.
- Three dispatches with a two-texture ping-pong; the last pass's output is both the input of
  `ConvertToIrradiance` and **next frame's `HistoryScreenProbeRadiance`** for the lighting PDF
  of 2.1, which is why the atlas must be double-buffered.
- `bMoving`: the SDF tracer has no hit velocity. Use the distance-field change boxes the temporal
  accumulator already rejects history in (`HistoryCB::g_regionLo/Hi`, `InChangedRegion`,
  `ssgi_temporal_accum_cs.hlsl:23-36`): a ray whose hit point lies inside a change box is
  "moving". That gives Lumen's per-texel moving fraction and probe `ScreenProbeMoving` for free,
  with the same information the engine already trusts for history invalidation. The DXR path can
  later add per-instance velocity.
- Irradiance format: start with L2 SH (`r.GI.FinalGather.ProbeGather.IrradianceFormat` 0) because
  the per-pixel evaluation is the radiance-cache consumer's `EvalSH_L2` with the pixel's normal
  (7 `float4` per probe, 8160 probes = 0.9 MB), then add Lumen's 6x6 octahedral map (format 1) if
  SH ringing shows at sun-patch/shadow boundaries; Lumen defaults to the octahedral map for that
  reason.
- `ssgi_spatial_denoise_cs` is **skipped** on the probe lane (`ssgi_renderer.cpp:454`): the
  probe filter replaces it, and running both would blur twice.

### 2.3 (c) Firefly clamp, stochastic interpolation and structured jitter, handed to TSR

This is the variance *shaping* half: reduce what can be reduced cheaply at the source, convert
the rest into the kind of noise a temporal integrator removes in a few frames, and stop asking
the per-pixel history to do the work.

**Lumen's algorithm.**

1. *Clamp before averaging* (`ScreenProbeCompositeTracesWithScatterCS`, `Filtering.usf:79-85`):
   `if (max3(Lighting) > MaxRayIntensity) Lighting *= MaxRayIntensity / max3(Lighting)` per ray,
   with `MaxRayIntensity` 10 in pre-exposed units (`Filtering.cpp:68-74`, "Clamps the maximum ray
   lighting intensity (with PreExposure) to reduce fireflies"). It is applied to the ray *before*
   it is accumulated into the texel, i.e. before any spatial or temporal averaging can spread a
   firefly. Rays are also dithered to the neighbouring texel with blue noise to hide the 8x8
   grid (`:89-103`), and every stored value is blue-noise quantised (`:152`).
2. *Stochastic interpolation* (`ScreenProbeIntegrateCS`, `LumenScreenProbeGather.usf:1230-1264`;
   `StochasticInterpolation` 1, `Gather.cpp:340-346`): the four corner probes' weights (bilinear
   expanded by one pixel x `exp2(-10000 * InterpolationDepthWeight * (planeDistance/z)^2)`,
   `:147-210`) are normalised, and one probe is picked with a blue-noise scalar proportional to
   its weight (`:1234-1255`); the pixel is lit by that single probe with weight 1 (`:1257-1261`).
   Four texture reads become one, and the smooth bilinear blend between probes - which would
   otherwise show the 16 px probe grid as low-frequency structure under motion - becomes
   per-pixel noise.
3. *Structured jitter* (`:1169-1196`; `FullResolutionJitterWidth` 1, `Gather.cpp:94-100`): the
   interpolation point is offset by up to +-`DownsampleFactor` pixels of blue noise (`:1175-1177`,
   fading to half beyond 5 m, `:1172`), **accepted only if the jittered point lies on the pixel's
   plane** (`exp2(-1e6 * (d/z)^2) > 0.01`, `:1182-1195`). Together with the 8-frame probe-lattice
   jitter (`Common.ush:72-75`) and the 8-frame ray-direction cycle, every pixel sees a different
   probe set and direction set each frame, re-rolled from `View.StateFrameIndex`.
4. *Short temporal filter* (`Temporal.usf:486-527`): `N = min(N_hist + 1, 10)`, capped further by
   `(1 - FastUpdateModeAmount) * 10` where `LightIsMoving` (the integrated `ScreenProbeMoving`)
   exceeds `FractionOfLightingMovingForFastUpdateMode` (`:488-499`), `alpha = 1/(1+N)` (`:522`).
   There is no neighbourhood clamp in the default path and no variance estimate; the input is
   already clean enough for a 10-frame mean.
5. *TSR* resolves what remains. 01 section 3.8 verified that the TSR shaders have no Lumen-specific
   input: what reaches them is deliberately high-frequency, blue-noise-shaped noise.

**Why not more per-pixel history instead.** Every frame of history is a frame of lag under
camera or light motion and a frame of ghosting where reprojection is wrong; the stable
accumulator's own design lists both as its main risk
(`.rulebook/tasks/gi-diffuse-gather-temporal-reconstruction/design.md`, Risks). History cannot
remove the two kinds of error the gather actually produces: a *biased* per-frame estimate
(fireflies push the mean up; `max(..., 0)` on noisy SH pushes it up; no clamp means a single
sun-lit hit in 8 rays is `albedo * sun / (8 pi)` in E/pi) and a *coherent* low-frequency wander
(the radiance cache, 02). It only slows them down. 03 section 2.2 states the consequence: the
residual UzEngine hands to TAA is low-frequency and spatially coherent, which a temporal AA
cannot distinguish from a lighting change; Lumen's residual is high-frequency and uncorrelated,
which it removes in a handful of frames. Mechanism (c) is how the residual is given that shape.

**Adaptation to UzEngine.**

- Clamp: one helper, `ClampRayRadiance(L, maxPreExposed)`, applied to `contribution` in the ray
  loop (`sdf_diffuse_gather.hlsli:105-107`) on the per-pixel lane and in the composite pass on
  the probe lane. `maxPreExposed = r.GI.FinalGather.MaxRayIntensity / exposure`, with `exposure`
  from `PerFrameCB` (`core/params/constant_buffers.h:72`, "Exposure multiplier from
  auto-exposure"); 0 disables and is bit-exact. Use the previous frame's exposure to avoid a
  same-frame dependency on the auto-exposure pass.
- Stochastic interpolation and jitter live in the probe lane's integrate pass (section 3.3,
  stage 4). On the per-pixel lane the only analogue is a stochastic 1-of-4 tap in
  `ssgi_upsample_cs.hlsl:88-99`, which is not worth doing without TAA.
- Short temporal: the existing accumulator becomes configurable - `g_maxHistoryFrames` in the
  free `_regionPad` slot of `HistoryCB` (`ssgi_temporal_accum_cs.hlsl:24`), default 64 (bit-exact
  today), 10 on the probe lane; a `fastUpdate` scalar from the integrated `ScreenProbeMoving`
  caps `count` as Lumen does. `StableTemporal` is not dispatched on the probe lane
  (`ssgi_renderer.cpp:407`, `:1066-1067`).
- TSR: UzEngine has TAA and the `IUpscaler` path (03 section 1); the stage that adds stochastic
  interpolation and jitter is gated on a temporal integrator being active, because without one
  it adds visible grain (Lumen with TAA off is "grainy, not unstable", 01 section 3.8).

## 3. Designs, recommendation, and the staged plan

### 3.1 Design A - keep the per-pixel lattice, add (a) and (c)

Changes confined to `sdf_diffuse_gather.hlsli`, `ssgi_temporal_accum_cs.hlsl` and CVar plumbing:

- A1 firefly clamp in the ray loop (section 2.3).
- A2 importance sampling from the radiance-cache SH at the receiver: evaluate `EvalSH_L2` of the
  trilinear probe SH at the receiver in 16 hemispherical cells (concentric map), build a 16-entry
  CDF of `max(eps, luminance) * cos`, draw the 8 stratified samples through it, and weight each
  ray by `cos / (pi * p)`. Unbiased for any `eps > 0`; bit-exact when off.
- A3 stochastic 1-of-4 upsample tap and a shorter history cap, both pointless without TAA.

Trade-offs. Smallest diff; A1 is shared with design B verbatim. A2 brings the radiance cache's
wander *into the sampling pattern* (a wandering PDF re-allocates rays frame to frame; unbiased,
but a new source of temporal variance until stage-1 of 02 lands), costs 16 SH evaluations per
pixel per frame, and is discarded by design B (whose PDF is per probe and per texel, driven by
history first). A cannot implement (b): with a different Cranley-Patterson rotation per pixel
there is no "same direction" between neighbours to compare hits in, and storing 8 rays x (RGB +
hitT) per half-res pixel to filter them is 8x the texture traffic for a kernel that would still
have no texel structure. Since (b) is the mechanism with the largest variance reduction and the
only one that preserves contact detail while filtering, A leaves the audit's gap open.

### 3.2 Design B - a probe-space gather lane beside the per-pixel lane (recommended)

A second gather lane in `SSGIRenderer`, selected by `r.GI.FinalGather.ProbeGather` (default 0),
that replaces passes 2-3 (`DispatchTrace`/`DispatchRestir`, `DispatchSpatialDenoise`) and feeds
the unchanged `DispatchTemporalAccum` and `DispatchUpsample`. Pass structure (all compute, half
resolution or smaller, event names in the Lumen order):

| # | Pass | Threads | Writes | Lumen counterpart |
|---|---|---|---|---|
| P0 | ProbeScreenData | 1 group per probe | probe depth + packed normal (probe res), BRDF SH (9 floats/probe), disocclusion flag | `ScreenProbeGatherScreenDataCS` |
| P1 | ProbeLightingPDF | 8x8 per probe | lighting PDF atlas (`R16F`) | `ScreenProbeComputeLightingProbabilityDensityFunctionCS` |
| P2 | ProbeGenerateRays | 64 per probe | `RayInfo` atlas (`R16_UINT`) | `ScreenProbeGenerateRaysCS` |
| P3 | ProbeTrace | 64 per probe | trace radiance + hit (`hitT`, moving bit) | `LumenScreenProbeTracing.usf` (SDF/DXR body = today's loop) |
| P4 | ProbeComposite | 8x8 per probe | radiance atlas (clamped, dithered), min hit distance, texel moving | `ScreenProbeCompositeTracesWithScatterCS` |
| P5 | ProbeCalculateMoving | 8x8 per probe | `ScreenProbeMoving` (probe res) | `ScreenProbeCalculateMovingCS` |
| P6 | ProbeFilter x3 | 1 per texel | radiance atlas ping-pong; last = next frame's history | `ScreenProbeFilterGatherTracesCS` |
| P7 | ProbeConvertToIrradiance | 8x8 per probe | SH L2 (7 `float4`/probe) or 6x6 octahedral | `ScreenProbeConvertToIrradianceCS` |
| P8 | ProbeIntegrate | 1 per half-res pixel (stage 1-3), full-res optional (stage 4) | E/pi into `m_ssgiDenoised` | `ScreenProbeIntegrateCS` |

Resources at `DownsampleFactor` 16, 1080p: every atlas is 960 x 540 texels - the size of
`m_ssgiTrace` - so radiance x2 (`R11G11B10F`, 2 MB each), hit distance (`R16F`, 1 MB), moving
(`R8`, 0.5 MB), lighting PDF (`R16F`, 1 MB), `RayInfo` (`R16_UINT`, 1 MB), the probe-resolution
textures (120 x 68, negligible) and the SH buffer (0.9 MB): ~9 MB. At factor 8, 4x that.
Ray count: 8160 x 64 = 0.52 M sphere traces against 4.1 M today; the trace pass keeps today's
per-ray body (`TraceSdfSurfaceRadianceIdentified` or `TraceDxrGatherRadiance`, with
`ResolveSdfRayOrigin` once per probe) so the hit shading, the surface-cache read (Phase D) and the
`ValidSampleReject` semantics (`ssgi_surface.hlsli:47-49`: a probe whose origin cannot be
resolved writes a negative depth, is invalid for interpolation, and adaptive probes fill the
tile later) are unchanged.

Integration at the half-res lattice in stages 1-3 is what keeps the diff small: the probe lane
writes exactly what `ssgi_spatial_denoise_cs` writes today (E/pi + 1 in alpha, `:55`), so the
temporal accumulator, its history geometry, the change-box rejection and the bilateral upsample
run untouched, and `deferred_lighting_ps.hlsl` reads `ssgiSrvIndex` as before. The per-pixel
integrate is the radiance-cache consumer's math - plane-weighted 4-probe interpolation of SH,
`max(EvalSH_L2(E, n), 0)` - with the probe grid in place of the world grid. One inherited
detail to keep in view: when none of the upsample's four taps passes the plane test, it
re-traces a full per-pixel cosine gather at that pixel (`ssgi_upsample_cs.hlsl:103-107`,
`GatherSurfaceLighting`); on the probe lane that fallback should read the nearest valid probe
instead, or those (rare) pixels become the one place the old unfiltered estimator still shows.

Trade-offs. Nine dispatches and ~9 MB where there were two dispatches; a new pass structure in
`ssgi_renderer.cpp` (~400 lines C++, five or six shaders); contact detail depends on probe
density and adaptive probes (Lumen also adds a full-res short-range AO, which UzEngine's GTAO
covers only partly). In exchange: 8x fewer rays, the only architecture where (b) exists, a
natural home for (a) and (c), a probe radiance history that enables Lumen's temporal reuse
without blending, and a direct path to Lumen's remaining features (adaptive probes, screen
traces first, mesh SDFs near the receiver, radiance-cache reads beyond a trace distance).

### 3.3 Recommendation and stages

**Design B, in five stages, with A1 as stage 0.** Each stage has its own CVar (default = today's
behaviour, bit for bit), its own measurement, and lands only when its gate passes. Order is by
leverage per unit of work, with one true dependency: stage 3's default PDF source is stage 2's
filtered atlas.

Gates (named here, reused below). All run with `r.GI.RadianceCache.FreezeAfterFrame`
(`frame/frame_gi_passes.cpp:96`) set so the radiance cache stops updating before the measured
window - the gates must measure the *gather's* variance, not the cache's wander - and with
`StableTemporal` OFF on both lanes so the pre-temporal and post-temporal numbers are comparable.

- **G-fixed** (fixed-camera temporal std): `scripts/rendering/run_flicker_baselines.py` (3 runs x
  256 measured frames after 200 warm-up, `:3-4`; `PRIMARY_METRIC = 'std_mean'`, `:63`) on the W
  and A Sponza poses, plus the sphere-crawl harness named in the stable-temporal design
  (`_evidence/gi/sphere-crawl/capture.py`/`analyze.py`: material showcase, fixed camera, frames
  90-149 and 600-659, five nested sphere ROIs). Pass: `no_believable_worsening`
  (`scripts/rendering/flicker/criteria.py:68`) on every ROI; stage-specific targets below.
- **G-motion** (moving-camera lag/ghost): `scripts/rendering/run_taa_motion_check.py` on
  `sponza-flicker-w-motion` (`:35`), `lag_vs_raw` (`:74`) and `lag_at_rejected` (`:78`) per
  phase, plus `run_wall_motion_gate.py`. Pass: not above baseline; at stage 4 it must *fall*.
- **G-variance** (variance at equal sample count, pre-temporal): the region dump already records
  the accumulator's *current* (pre-temporal) sample per gather pixel per frame (slot 0 in
  `WriteRegionDumpGatherAccum`, `ssgi_temporal_accum_cs.hlsl:187-192`, parsed by
  `scripts/rendering/flicker/regiondump.py`). Over 256 frozen-camera frames at the same
  `r.GI.FinalGather.Samples`, report per ROI (i) the temporal std of slot 0 (`flicker/metrics.py:
  temporal_stats`, `:53`), (ii) its 256-frame mean against the baseline mean +- 2 standard errors
  (unbiasedness), (iii) the ray count. This is the metric that separates "removed variance at the
  source" from "hid it in history", and the brief's variance-at-equal-sample-count check. A short
  analysis script over the existing dump is the only new tooling.
- **G-known**: `r.GI.Diagnostic.KnownRadiance` (`gi/radiance_cache.cpp:33`) constant-radiance
  scene: the importance sampler and the probe filter must return the constant to within
  quantisation (weights normalise; nothing leaks).
- **G-energy**: mean E/pi over the ROIs against the path-tracer oracle (`r.PathTracer.*`, 03
  section 1), for the two mechanisms that bias on purpose (clamp, `MinPDFToTrace`).

**Stage 0 - firefly clamp** (`r.GI.FinalGather.MaxRayIntensity`, default 0 = off; Lumen 10).
`ClampRayRadiance` in `sdf_diffuse_gather.hlsli` after `:105`, pre-exposed through
`PerFrameCB.exposure`. Gate: G-variance (expect the largest drop on the sun-lit Sponza ROIs;
expect no change on `aeoff`/`aefix` projects' relative ordering), G-energy (quantify the loss;
Lumen accepts it at 10), G-fixed no worsening. One shader line plus a CVar; it survives into
stage 1's composite pass unchanged.

**Stage 1 - probe-space gather, uniform rays, no filter** (`r.GI.FinalGather.ProbeGather` 0/1;
`.DownsampleFactor` 16, 8 allowed; `.OctahedronResolution` 8; `.JitterCycle` 8). P0 (depth/normal
only), P3 (texel-centre blue noise, no `RayInfo`), P4 (clamp, min hit, dither), P7 (SH L2), P8 at
the half-res lattice with 4-probe plane weights; `ssgi_spatial_denoise_cs` skipped on the lane;
`ssgi_temporal_accum_cs` unchanged. Gate: G-variance - pre-temporal std **not above today's at
one eighth of the rays** (factor 16) and clearly below it at factor 8 (half the rays); G-energy
within noise of today (no bias introduced); G-fixed and G-motion no worsening. Expect a visibly
flatter contact region (no adaptive probes yet) - record it, it is stage 5's input.

**Stage 2 - inter-probe filter with the hit-angle gate** (`.SpatialFilter` 0/1,
`.SpatialFilterNumPasses` 3, `.MaxRadianceHitAngle` 10 deg, `.PositionWeightScale` 1000,
`.IrradianceFormat` 0/1). P5, P6 x3, radiance atlas double-buffered, moving bit from the change
boxes. Gate: G-variance - a further >= 3x drop in pre-temporal std on far-field ROIs (walls,
floor) with the contact ROIs (under the arches, column bases) unchanged in mean to within noise
(the gate's purpose); G-known exact; G-fixed; G-motion no worsening (the strong-filter widening
must not smear under motion more than today's 5x5 does).

**Stage 3 - importance sampling** (`.ImportanceSample` 0/1, `.ImportanceSample.IncomingLighting`
1, `.ImportanceSample.ProbeRadianceHistory` 1 with 0 = radiance-cache SH only,
`.ImportanceSample.MinPDFToTrace` 0.1). P0 gains the BRDF SH; P1, P2 added; P3 reads `RayInfo`;
P4 applies the `(8/MipSize)^2` weight. Gate: G-variance - a further drop on the ROIs with
directional lighting (sun patches, bright windows) at equal ray count, none elsewhere; G-energy
(the `MinPDFToTrace` cull darkens grazing contributions - Lumen's own CVar help warns of "black
corners"); G-known exact; a new frozen-camera check that the ray *allocation* is stable
(`RayInfo` histogram per probe frame to frame) once the radiance cache is frozen, to confirm the
history-driven PDF does not oscillate.

**Stage 4 - stochastic interpolation, structured jitter, short history** (`.StochasticInterpolation`
0/1, `.FullResolutionJitterWidth` 0 -> 1, `r.GI.FinalGather.TemporalMaxFrames` 64 -> 10 on the
probe lane, `.FastUpdate` 0/1 from `ScreenProbeMoving`; the stage refuses to enable unless a
temporal integrator (`r.TAA` or the upscaler) is active and logs why). Gate: G-motion - `lag_vs_raw`
and `lag_at_rejected` **below** the baseline (the shorter history must show); G-fixed with TAA on
within the Glass/Chrome control range on the sphere harness (the target the stable-temporal
design set for itself); and a light-change response check (sun step at a fixed frame, t90 of the
indirect term in frames) that must land near 10 frames.

**Stage 5 - adaptive probes and the remaining Lumen features** (`.NumAdaptiveProbes` 0 -> 8):
plane-test failure marks and spawns adaptive probes (`LumenScreenProbeGather.usf:1744-1898`);
later, screen traces first, mesh SDFs near the receiver, and radiance-cache reads beyond a trace
distance once 02's stage 2 gives the cache a far-field-only contract. Gate: contact ROIs against
the path tracer.

What I would **not** do: start with A2 (per-pixel importance sampling from the radiance cache) -
it is thrown away by stage 3 and imports the cache's wander into the sampling; extend
`StableTemporal` or add confidence heuristics to the per-pixel accumulator while the probe lane
is built; enable Lumen's probe-space temporal filter (`TemporalFilterProbes`, off in Lumen,
`Filtering.cpp:84-90`) - importance sampling from history is the cheaper temporal reuse and
cannot ghost.

## 4. Interaction with the radiance-cache work, StableTemporal and ReSTIR-GI

**Radiance-cache stage 1 (HEAD, default off: `r.GI.RadianceCache.JitterCycle`, `Temporal.Mode`,
`Temporal.MinAlpha` 0.06, `Temporal.MaxHistory` 64, `Temporal.ChangeThreshold` 4,
`Classification.Hysteresis`; `gi/radiance_cache.cpp:91-118`).** Complementary, not subsumed, in
both directions. The gather layer averages the *independent* part of each ray's error. The
cache's error is not independent: every gather ray that lands within a cell interpolates the same
eight wandering SH probes (`SdfReflectionIrradiance`, `sdf_reflections.hlsli:165-199`), so
neighbouring probes' rays in the same direction carry the *same* error, the hit-angle gate passes
them (they did see the same thing), and the filter averages the wander into itself unchanged.
02's producer-side fix is therefore still required, and the gates above freeze the cache
precisely so that the gather's own variance is what gets measured. Conversely, the probe lane
helps the cache's fix: once the gather no longer re-noises every frame, the cache's residual
becomes measurable on the final image (today it is buried under the gather's noise floor), and
stage 3's PDF fallback reads the cache's SH, so the hysteresis blend directly steadies the ray
allocation.

**Radiance-cache stage 2 (in flight: near-field exclusion + Chebyshev visibility).** The probe
lane does **not depend on it landing first.** UzEngine's gather rays never terminate *into* the
cache; they trace to the hit and read the cache there as the hit's indirect term, so Lumen's
`MinTraceDistanceBeforeInterpolation` contract (01 section 2.8) has no counterpart on this path
yet. The dependency appears at stage 5, when the probe lane adopts Lumen's "stop tracing at
distance D and read the cache" to cut sphere-trace cost on long rays: that read must be
far-field-only and parallax-corrected, which is exactly what 02's stage 2 provides. Chebyshev
visibility in the readers lowers the hit-shading noise the gather rays return and is welcome at
any time; it changes nothing in the gather's structure.

**StableTemporal (default ON, `ssgi_renderer.cpp:46-47`).** Kept as is on the per-pixel lane -
it is the measured interim for the shipped path. Not dispatched on the probe lane: an unbounded
history is the mechanism 03 warns diverges from Lumen, and the probe lane's point is to not need
it. The lane selection is already a branch in `Render` (`:407-409`); the probe lane forces the
legacy accumulator with `TemporalMaxFrames` 10 from stage 4 and 64 before it. The two lanes
must never share a history texture (`m_stableHistory` versus `m_ssgiHistory`, `ssgi_renderer.h:
304-315`, `:362-368`); switching lanes resets history as a mode change does today (`:413-418`).

**ReSTIR-GI (`r.GI.ReSTIR`, default 0, `ssgi_renderer.cpp:50-58`; `ssgi/ssgi_restir_gi.hlsli:
1-23`).** It is a per-half-res-pixel reservoir reuse of the gather's *directions*, a replacement
for `DispatchTrace` (`:446-451`). The probe lane has its own trace, so the two are mutually
exclusive: `ProbeGather` wins when both are on, logged once, as the DXR-mode log does (`:417`).
Lumen keeps ReSTIR as a separate, non-default final gather (`LumenReSTIRGather.cpp`, 03 section 1)
rather than a layer under the screen probes, and the same holds here. A later probe-space ReSTIR
(reservoirs per probe texel) is possible but not planned: stage 3's history-driven PDF is the
cheaper temporal reuse and, unlike reservoir reuse, cannot carry a stale value into the result.

**Other consumers.** `deferred_lighting_ps.hlsl`, the DDGI writer, the RT reflections' indirect
term and the surface cache read the radiance cache, not the gather; nothing in this design
touches them. The DXR gather permutation (`r.GI.FinalGather.DXR`, `raytracing/rt_manager.cpp:53`)
keeps working through the shared per-ray body in P3.

## 5. Risks, and what must be measured on the real render before trusting it

1. **The clamp is a bias, and it follows the exposure.** `MaxRayIntensity / exposure` means the
   indirect term's clipped energy changes as auto-exposure adapts; Lumen has the same structure
   (PreExposure) and lives with it. Measure G-energy on the `aeoff` and `aefix` Sponza projects
   and the light-change response (a sun step must not produce an exposure/GI feedback
   oscillation); use the previous frame's exposure; start at Lumen's 10.
2. **Contact detail at probe granularity.** Stage 1 will read flatter in corners and under
   furniture than the per-pixel gather, and Lumen compensates with adaptive probes, a strict
   plane test (`InterpolationDepthWeight`), bent normals and a full-res short-range AO. Measure
   the contact ROIs against the path tracer at stages 1, 2 and 5; do not judge the lane on
   far-field ROIs alone. If stage 5's adaptive probes do not close it, `DownsampleFactor` 8 is
   the fallback at half the ray cost of today.
3. **Hit distances from an expanded-surface tracer.** `TraceSdfRay` registers hits on the
   distance-dependent expanded surface (`sdf_reflections.hlsli:106-127`), so `hitT` is short by
   the expansion and a near-grazing neighbour's hit can land a few centimetres off. The angle
   gate is tolerant (10 degrees at a 20 cm hit is 3.5 cm), but verify the contact ROIs do not
   leak at stage 2; the clamp-to-own-hit rule is the first thing to check if they do. The DXR
   path has exact hits and is the control.
4. **Equal-area mapping and the plain-mean composite.** Using `OctDecode` instead of an
   equal-area octahedral map would weight texels unequally and bias the irradiance; G-known is
   the unit test (a constant scene must integrate to the constant), and it must run before any
   stage-1 measurement is believed.
5. **The history-driven PDF can oscillate** if last frame's filtered radiance differs frame to
   frame (radiance-cache wander, moving lights): the ray allocation then changes, and with it the
   per-frame variance pattern. Lumen tolerates this; measure the `RayInfo` histogram stability
   (stage 3 gate) with the cache frozen and unfrozen. If unfrozen is materially worse, the fix is
   02's stage 1, not a change to the sampler.
6. **`MinPDFToTrace` darkens grazing light** ("black corners", `ImportanceSampling.cpp:56`).
   Lumen's 0.1 is a tuned trade; measure G-energy with 0.1 and 0.05 before accepting a default.
7. **Change boxes as the moving signal are coarse.** A ray hitting inside a box marks the probe
   moving and widens the filter plus shortens history; a large box (a whole moving mesh's AABB)
   marks many probes. Measure `lag_at_rejected` under `sponza-flicker-w-motion` at stage 2 and
   stage 4; if the boxes are too coarse, the DXR path's per-instance velocity is the refinement.
8. **TAA is off by default and the lane's stage 4 assumes it.** Stages 0-3 are measured with
   TAA off (as today's captures are) and stage 4 with TAA on; the stochastic/jitter stage must
   refuse to enable without a temporal integrator, or it will be judged on grain it was designed
   to hand off. The quality presets (`quality_presets.cpp:79`) already turn TAA on from the
   second tier.
9. **SH ringing in the irradiance format.** L2 SH of an 8x8 probe with a sun patch next to a
   shadow rings; `max(.., 0)` hides it as a positive bias. Lumen defaults to the 6x6 octahedral
   map. Measure the contact and shadow-edge ROIs at stage 2 with both formats before choosing the
   default.
10. **Cost.** Nine dispatches replace two; the ray count falls 8x but the LDS sort, the three
    filter passes and the atlases add fixed cost. Profile (`UZ_PROFILE_SCOPE` per pass, as the
    existing passes do) at factors 16 and 8 at 1080p before stage 1 lands; the lane must not cost
    more than today's trace + denoise, or the ray savings are the only thing paying for it.
11. **Equal-sample-count comparisons must state the ray budget.** The probe lane at factor 16
    uses one eighth of today's rays; a pre-temporal std equal to today's is a win by 8x in budget,
    and a std equal to today's at factor 8 (half the rays) is a 2x win. Report both numbers; never
    only one.

## 6. Source index

Lumen (`E:/UnrealGITHUB`, read-only): `LumenScreenProbeGather.cpp` (`:70-100` octahedron,
downsample, jitter width; `:227-239` temporal frames and ray directions; `:340-346` stochastic
interpolation; `:2281` ray-direction frame index; `:2653`, `:2667`, `:1485`, `:2740` pass order),
`LumenScreenProbeFiltering.cpp` (`:13-74` filter and clamp CVars; `:84-90` probe temporal filter;
`:484-611`, `:705-752` pass events; `:597` degrees to radians), `LumenScreenProbeImportanceSampling.cpp`
(`:12-66` CVars), `LumenScreenProbeImportanceSampling.usf` (`:37-193` lighting PDF; `:307-495` ray
generation), `LumenScreenProbeImportanceSamplingShared.ush` (`:5-6`),
`LumenScreenProbeGatherScreenData.usf` (`:128-148`, `:162-172`, `:202-213`),
`LumenScreenProbeTracingCommon.ush` (`:8-22`, `:28-46`, `:48-58`, `:67-107`, `:138`),
`LumenScreenProbeCommon.ush` (`:72-75`, `:122-125`, `:234-262`), `LumenScreenProbeFiltering.usf`
(`:31-186` composite; `:292-297`, `:299-382`, `:388-506` filter; `:598` irradiance; `:1007-1075`
moving), `LumenScreenProbeGather.usf` (`:36`, `:147-242` weights; `:1130-1264` integrate;
`:1744-1898` adaptive placement), `LumenScreenProbeGatherTemporal.usf` (`:289`, `:486-527`, `:573`).

UzEngine (`uzengine/`): `runtime/shaders/runtime/gi/sdf_diffuse_gather.hlsli` (`:32-56`,
`:58-60`, `:66-67`, `:85-86`, `:90`, `:96-107`, `:124`), `runtime/shaders/runtime/gi/sdf_reflections.hlsli`
(`:47-51`, `:106-127`, `:165-199`, `:213-252`, `:255-266`, `:332`),
`runtime/shaders/runtime/gi/dxr_diffuse_gather.hlsli` (`:32-33`),
`runtime/shaders/runtime/ssgi/ssgi_trace_cs.hlsl` (`:1-8`, `:164`, `:176-177`, `:184-195`,
`:236-283`), `runtime/shaders/runtime/ssgi/ssgi_world_gather.hlsli` (`:13-35`),
`runtime/shaders/runtime/ssgi/ssgi_surface.hlsli` (`:34-35`, `:47-49`, `:78`, `:92-104`),
`runtime/shaders/runtime/ssgi/ssgi_spatial_denoise_cs.hlsl` (`:17-18`, `:38-57`),
`runtime/shaders/runtime/ssgi/ssgi_temporal_accum_cs.hlsl` (`:21-36`, `:53-92`, `:131`,
`:167-192`), `runtime/shaders/runtime/ssgi/ssgi_stable_temporal.hlsli` (`:7-28`),
`runtime/shaders/runtime/ssgi/ssgi_stable_temporal_accum_cs.hlsl` (`:192-209`),
`runtime/shaders/runtime/ssgi/ssgi_upsample_cs.hlsl` (`:88-109`),
`runtime/shaders/runtime/ssgi/ssgi_restir_gi.hlsli` (`:1-23`), `runtime/engine/renderer/gi/ssgi_renderer.cpp`
(`:24-58`, `:69`, `:113-114`, `:407-418`, `:436-471`, `:742`, `:1015-1017`, `:1066-1067`,
`:1333-1337`), `runtime/engine/renderer/gi/ssgi_renderer.h` (`:60-61`, `:122-151`, `:304-315`,
`:362-368`), `runtime/engine/renderer/gi/radiance_cache.cpp` (`:33`, `:57`, `:59`, `:91-118`),
`runtime/engine/renderer/frame/frame_constants.cpp` (`:84-91`, `:129`),
`runtime/engine/renderer/frame/frame_setup.cpp` (`:48`), `runtime/engine/renderer/frame/frame_gi_passes.cpp`
(`:36`, `:96`), `runtime/engine/config/quality_presets.cpp` (`:79`),
`runtime/engine/renderer/core/params/constant_buffers.h` (`:72`, `:292-296`),
`runtime/engine/renderer/raytracing/rt_manager.cpp` (`:53`), `scripts/rendering/run_flicker_baselines.py`
(`:3-4`, `:63`), `scripts/rendering/run_taa_motion_check.py` (`:1-9`, `:35`, `:74`, `:78`),
`scripts/rendering/flicker/metrics.py` (`:35`, `:53`), `scripts/rendering/flicker/criteria.py`
(`:68`, `:74`), `.rulebook/tasks/gi-diffuse-gather-temporal-reconstruction/design.md` (Risks,
Validation harness), and this folder's 01-04.
