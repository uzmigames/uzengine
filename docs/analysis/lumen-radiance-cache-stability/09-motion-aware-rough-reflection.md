# 09 - Motion-aware rough-reflection reconstruction: implementation spec

Date 2026-11-03. UzEngine, branch feat/soft-shadows-area-lights, HEAD 4b4e7260 (the working tree's
uncommitted changes touch vfx / clouds files only; none of the files cited here). Design spec only:
no code, no build, no commit in this lane; every claim is cited to the file and line it was read from
so the implementation is one pass. Input evidence: `_evidence/gi/motion-artifacts/report.md` (the
moving-camera diagnosis at the same HEAD), behaviour #3 (material_showcase Sphere_DarkMetal, "a white
circle / mask follows the camera") and behaviour #2 (Sponza specular shimmer in the shadows).

## 0. Decision in seven lines

1. Both artifacts are `RoughSpecular_Reconstruct` (rough_specular_reconstruct_cs.hlsl) reprojecting
   its history by the SURFACE motion vector (:64-67) and - because that is wrong for a reflection -
   dropping to a 4-frame history (rough_specular_math.hlsli:23-25), a 2-frame fast mean
   (reconstruct:101) and a 1-sigma clamp (math:26-30) as soon as a pixel moves more than 0.5 px. The
   25-tap spatial estimate then carries 1/4 of the output instead of 1/77, so every 1-spp window hit
   is a +-4.6 px Gaussian blob for four frames: the cloud (report.md:342-350; numbers :300-314).
2. Fix: reproject the history by the reflection's virtual point, computed INSIDE the reconstruct
   shader from inputs it already has (surface position, normal, camera, the hit-distance guide). No
   new texture, no RT-pass change (section 3, option A).
3. The virtual point MUST be curvature-corrected (thin lens: t_v = 1 / (2 kappa + 1 / t)). On the
   DarkMetal sphere the bright windows are HDRI MISSES - hit distance 0 = infinity
   (rt_reflection_cs.hlsl:316) - and a planar virtual point at infinity moves on screen like the sky
   (~6.5 px/frame on the report's slow orbit) while the true highlight moves ~0.3 px and the surface
   0.67 px (section 2.2). The report's "standard fix" as written would be ~10x WORSE than today on
   this receiver. With kappa the virtual point sits rho/2 behind the surface and the first-order
   error is zero for orbit, dolly and strafe.
4. With a trusted virtual history the motion mode is lifted: the cap becomes parallax-driven
   (static cap x (1 - delta / theta_lobe)^2, floor 4: ~60 frames on the fast orbit at roughness
   0.4, ~128 on Sponza stone) and the clamp returns to the static 3-sigma / 8-frame-fast form. A
   short history (count < 8: disocclusion, reset, rejected fetch) gets a hue-preserving firefly
   clamp on every spatial tap at k x max(history luminance, 3x3 median), k = 8 relaxing 2x per
   frame (section 4.4).
5. The spatial radius is NOT history-dependent today (math:18-22). It is gated by curvature (the
   support must stay inside the lobe's angular footprint on the receiver) and the NRD /
   realtime-denoiser "widen on disocclusion" pattern (nrd_reproject_cs.hlsl:105-127,
   realtime_temporal_cs.hlsl:82-96) is explicitly NOT adopted (section 4.5).
6. Gate: `r.RayTracing.Reflections.RoughReconstruct.MotionAware` selects a `UZ_ROUGH_MOTION_AWARE=1`
   permutation of the reconstruct shader (`CreateShaderWithDefines`, shader_manager.h:98); the OFF
   pipeline preprocesses to today's text = byte-identical DXIL. The b2 config CB grows by
   prevViewProj, unjitteredViewProj, prevCameraPos and four tunables under the same `#if`; root
   constants unchanged (17 DWORDs). Default OFF at landing; the flip to ON is the deliverable of
   section 6's moving-camera gates - the static validation is what let 70813e96 ship.
7. Two pre-existing upstream bugs found and left alone because they sit on the mirror route: the SSR
   temporal reads the pixel-space velocity as UV (ssr_temporal_cs.hlsl:183, :187 vs
   velocity_ps.hlsl:11-12) and places its "virtual hit" in FRONT of the surface (:176). Section 8.

## 1. The pass today (what the implementation edits)

### 1.1 Position in the frame

Denoise route (the user's lane D: `r.RayTracing.Reflections.Denoise 1`, SSR on):
RT_Reflections writes ray texels + hit distances into the SSR resolved buffer ->
SSR_Temporal (frame_gi_passes.cpp:848-914: reads the resolved buffer :876, writes
m_ssrHistory[write]) -> SSR_SpatialDenoise (:924-973: reads the history just written :950, writes
the resolved buffer :962) -> `RegisterRoughSpecularReconstruction` (:975) -> Reconstruct + Apply
(:1054-1073). Unified route (`r.Denoiser.Unified`, SSR off): RealtimeDenoiser, then the same
reconstruct (:1399-1413). On both routes the reconstruct's `inputSrv` is m_ssrResolvedOutputSrv
(:1069): its input is the signal AFTER the upstream temporal + spatial chain, not the raw ray texels.

Gate (:1057-1060): RoughReconstruct && Rough && bindless && RT rays available && (Denoise ||
standalone) && Prepare. CVar: `cv_RoughReconstruct` (:79-80, default true; its help text still says
"default off" - fix the string while there).

### 1.2 Resources and constants

- Root (b0), 17 DWORDs: rough_specular_common.hlsli:17-23 = rough_specular_reconstruction.cpp:16-23
  (static_assert). `kSlotRootConstants` 0, `kSlotPerFrame` 1, `kSlotPerObject` 2 (root_slots.h:30-36).
- b1 = the PerFrameCB prefix (common:6-12): `viewProj` and `invViewProj` are the JITTERED matrices
  (frame_constants.cpp:187-190), `cameraPos`, near/far.
- b2 = `RoughSpecularConfig`: previousJitter, currentJitter, nothing else (common:13-16;
  cpp:24-26, uploaded :118-126).
- Histories: two sets of {color, moments, geometry, guides}, RGBA32F, ping-pong (cpp:46-58,
  :85-99). geometry = world position + octahedral-packed normal (reconstruct:22); guides = roughness,
  hitDistance, linearDepth, producer alpha (:23). The reader uses geometry + guides.x/.z through
  `Compatible` (:78-86); guides.y (hit distance) is stored but not consumed.
- historyValid = `RealtimeHistoryReusable` (previous frame + 1 == frame, same epoch, no reset;
  realtime_denoiser.h:29-33) with reset = !matrixValid or a content reset other than FrameGap
  (cpp:114-117). Epoch = lighting hash + glossy / indirect / maxRoughness / intensity / maxDistance
  (frame_gi_passes.cpp:1061-1067).
- Previous view-projection: `ctx.temporal.prevViewProj` is the previous frame's UNJITTERED matrix
  (temporal_history_state.h:51, :84), already uploaded per frame for the SSR temporal
  (frame_gi_passes.cpp:890-902). Current unjittered: `ctx.unjitteredViewProj`
  (scene_renderer_frame.h:181); jitter `ctx.jitterX/Y` (:177-178).
- Velocity: render pixels, current - previous, computed from the UNJITTERED matrices, +x right, +y
  down (velocity_ps.hlsl:11-12, :112-129; velocity_common.hlsli:4-12). Zero on sky (:102-103). The
  TAA consumes it as pixels (taa_resolve_cs.hlsl:311-312).
- Hit distance (m_ssrHitDistanceSrv, frame_gi_passes.cpp:1070): the ray t of the ONE VNDF sample
  (rt_reflection_cs.hlsl:445); 0 for a sky miss (:316, "0 = no hit, surface reprojection"), 0 for a
  zero-throughput sample (:273, rgb 0 with the full blend weight :272) and for a no-reflection texel
  (:214).

### 1.3 The reconstruct shader, step by step

1. `Receiver r` (position = jittered invViewProj x depth, normal, roughness, hitDistance,
   linearDepth; common:42-56); geometry/guides written (reconstruct:22-23); early out on sky, mirror
   (`Reconstructs`: roughness > 0.04, math:14) or alpha 0 (:24-28).
2. Spatial: radius = `SpatialRadius` = 1 + min(2, 8 r^2 footprint), footprint = t / (t + depth), a
   miss has footprint 1 (math:18-22). 25 taps at p + round((x, y) x radius) (:33-57); weight =
   Gaussian x normal^NormalPower x plane x roughness x hit-distance (:46-49); `Compatible` = same
   surface within `PlaneTolerance` (max(0.5 cm, 0.1 % of the camera distance)), normals within 25
   degrees, roughness within 0.05 (common:57-69). Valid black taps participate (:41). Outputs:
   estimate, luminance moments, effectiveSamples, spatialVariance (:58-62). At roughness 0.4 on an
   HDRI miss: radius 2.28 -> the support spans +-4.6 px.
3. Temporal fetch: previousPixel = p + previousJitter - currentJitter - velocity (:64-67); four
   bilinear taps, each accepted if its history alpha > 0 and it is `Compatible` with the STORED
   geometry / guides at that texel (:72-93).
4. Motion mode, motionLength > 0.5 px: fastCount cap 2 instead of 8 (:101); `ClampHistory` extent 1
   sigma instead of 3 (math:28); `HistoryCap` 4 instead of min(128, 16 + 384 r^2) = 77 at r 0.4
   (math:24). Blend alpha = 1 / count (:113-116). Moments carry first / second luminance moments,
   the fast mean and its count (:119).
5. Apply (rough_specular_apply_cs.hlsl): writes the result into the resolved buffer for roughness
   > 0.04 only (:12-16); the mirror route is never written.

## 2. Why the cloud, and what the static validation could not see

### 2.1 The mechanism in the numbers

Report table 4.2 (fast orbit, DarkMetal pixels, specular_indirect; report.md:300-314): moving vs
static, bright area (> 1.0) 1.9 % vs 0.9 % (2.1x), very bright (> 1.5) 0.67 % vs 0.09 % (7.4x),
mean +15 %, p99 1.33 vs 0.95. RoughReconstruct OFF on the same frames: p99 1.96, max 10.3 - a
dither of fireflies, not a cloud. Slow orbit per-frame change of the sphere's specular (table 4.3,
:320-332): 4.6 % ON, 49.5 % OFF, 96 % raw.

Under motion the output is a 1/4-weighted blend of the last four frames' spatial estimates, each a
Gaussian spread (sigma = one tap = 2.28 px) of that frame's 1-spp window hits, and the history is
forced into +-1 sigma of a TWO-frame fast mean of the same positively skewed estimator (reconstruct
:101, :111; math:28). The lower clamp bound lifts the history whenever the fast mean carries a
firefly, the upper bound only pulls it back to mean + sigma: a one-sided bias on a skewed signal,
which is the measured +15 % energy. Static, the same pixel averages 77 frames with a 3-sigma clamp
and converges to the correct soft highlight.

### 2.2 The receiver is a sphere: where the report's "standard fix" breaks

Take the camera at distance d from the sphere centre O, radius rho, orbiting by d_theta per frame
with the sphere pinned at screen centre (the report's orbit paths), focal length f in pixels. To
first order, per frame:

- a surface point moves on screen by rho x d_theta x f / d (this is the velocity buffer);
- the reflection of a FIXED far direction D (an HDRI window) sits where the normal bisects D and the
  direction to the camera; that half vector rotates at HALF the camera's angular rate, so the
  highlight moves by rho x d_theta x f / (2 d) - half the surface motion, same direction;
- a planar virtual point at infinity (hit distance 0 -> "infinite" footprint, math:19-20; the only
  honest reading of a miss) is a fixed world direction and moves like the sky: d_theta x f.

The report's slow orbit measured 0.67 px of surface motion on the sphere (report.md:346), which
puts d at ~10 rho for this scene; f ~ 935 px at 1080p / 60 degrees. The three motions are then
0.67 px (surface), 0.33 px (true highlight) and 6.5 px (planar virtual point at infinity). The
uncorrected virtual reprojection fetches history 6 px from where the highlight was - on the slow
path; 33 px on the fast one - and `Compatible` accepts it (same sphere, same roughness): a long,
confidently wrong history. That is a smear far worse than today's cloud.

The convex-mirror image of a far object is at the focal distance rho / 2 behind the surface, not at
infinity; for a hit at finite t the image distance is 1 / (2 kappa + 1 / t) with kappa = 1 / rho
(the thin-lens / mirror equation; NVIDIA NRD ReBLUR applies the same "ApplyThinLensEquation" with a
curvature estimated from the normal's screen derivatives). A virtual point at P + incident x rho / 2
is a fixed world point rho / 2 inside the sphere along the eye ray; seen from the previous camera it
is displaced by (rho / 2) d_theta at depth ~d, i.e. rho d_theta f / (2 d) - exactly the highlight's
motion. The same check passes for the dolly (the virtual point is slightly deeper than the surface
and scales accordingly) and the strafe (relative motion rho dx f / (2 d^2) in both derivations). For
a plane kappa = 0 and the formula reduces to the planar virtual point; for a planar miss, to the
pure direction (sky-like) reprojection, which is right there (the reflection of the sky in a floor
moves as the sky does).

So the virtual point is mandatory AND it must be curvature-corrected. The in-repo precedent does the
first and not the second: `NrdVirtualPosition` (nrd_signal.hlsli:34-39, nrd_math.hlsli:31-34) unfolds
P + reflect(dir, N) x t = P + incident x t, written per pixel by the path tracer's closest-hit
(pathtracer_closesthit.hlsl:121-127) and miss shader (pathtracer_miss.hlsl:12-14, with a 10000 cm
stand-in for infinity), and `nrd_reproject_cs.hlsl:34-47` reprojects it through previousViewProj and
lerps toward the surface coordinate by roughness^2 (:45). Reuse the reprojection delta and the
roughness lerp; add the thin lens.

### 2.3 The upstream regime change (why the clamp must not trust carried-over statistics)

The velocity buffer is in pixels (section 1.2). `ssr_temporal_cs.hlsl` treats it as UV:
`prevUV = uv - surfaceVelocity` (:183) and `prevUV = uv - velocity` (:187), and its
`VELOCITY_THRESHOLD = 0.05` is documented as "UV space - 5 % of screen" (:54) but compared with a
pixel magnitude (:251-253). Consequences: for any motion above a fraction of a pixel most pixels'
prevUV leaves [0, 1] and the pass outputs the CURRENT frame (:191-194); the ones that stay reach alpha
0.5 (:253). Static, it is a 32-frame EMA (`roughnessProxy = saturate(currentSSR.a)` reads the blend
weight, which is 1 in rough mode - rt_reflection_contract.hlsli:107-108 - so targetSamples is always
32, :246-248). The reconstruct's input therefore flips between "smooth 32-frame EMA + 9-tap spatial"
(static) and "raw 1-spp + 9-tap spatial" (moving) at motion onset, and the moments carried in its
history (:105-106) describe the wrong regime for a few frames. Any clamp that fires on a short
history must take its reference level from the current neighbourhood, not from the carried variance.
This also means the reconstruct is, today and after this change, the ONLY temporal filter on the
rough signal while the camera moves; section 8 lists the SSR temporal fixes as a separate pass.

### 2.4 The "widening" is a weight, not a radius

`SpatialRadius` depends on roughness, hit distance and depth only (math:18-22); the support is the
same static and moving. What changes under motion is the spatial estimate's share of the output:
alpha = 1 / count = 1/4 instead of 1/77 (reconstruct:113-115). The report's item 3 ("do not widen
the spatial radius while the history is short", report.md:362-363) is therefore satisfied by not
introducing the NRD / realtime-denoiser disocclusion widening, and the real lever is keeping the
history long (section 4.3). Section 4.5 adds the curvature gate because the 25-tap support is the
right size for THIS sphere (section 4.5's footprint check) but not for small or tightly curved
receivers.

## 3. Options

A. Virtual reprojection inside the reconstruct shader (recommended). Inputs already in the pass:
   r.position, r.normal, cameraPos (common:7-8, :49-50), the per-pixel hit distance (:52), the 25
   compatible taps (reconstruct:33-57) to filter the hit distance and the four axis neighbours to
   estimate curvature. Needs two matrices + the previous camera position in the b2 CB. Blast radius:
   one shader permutation, one struct, ~25 lines of C++ in `Register`. OFF = today's DXIL.

B. The RT pass writes a virtual-position texture (the NRD pattern, pathtracer_closesthit.hlsl:
   121-127). Exact sampled direction and t per pixel, but: a new RGBA32F texture kept alive across
   SSR_Temporal / SSR_SpatialDenoise, an 18th root DWORD in `RTReflectionRootConstants`
   (rt_reflection_cs.hlsl:89-107, 17 DWORDs mirrored in C++), graph plumbing in two routes, a 1-spp
   virtual point that is as noisy as the hit distance it is built from, and the RT pass still has no
   curvature (one pixel's normal). Rejected: more surface for no accuracy gain.

C. Fix the SSR temporal (units, sign, roughness proxy; section 8) and simply raise the reconstruct's
   motion cap. Touches the MIRROR route (the SSR temporal is the mirror denoiser; "mirror / sharp
   reflections unchanged" is a gate), stacks two reprojected histories whose interaction is unmeasured,
   and still leaves the reconstruct's own history on the surface vector. Follow-up, not this pass.

D. Raise the motion cap by roughness without changing the reprojection (report.md:258-259, first
   half). The cap is 4 BECAUSE the surface vector is wrong (report.md:348-350): a longer history on
   the wrong coordinate smears. Rejected.

Simplest architecture that meets the requirement: A, plus the firefly clamp and the curvature gate in
the same shader. No new passes, no new textures, no root-signature change.

## 4. Specification

### 4.1 Inputs, CB, permutation

Permutation: `UZ_ROUGH_MOTION_AWARE=1` compiled with `CreateShaderWithDefines(EngineShader::
RoughSpecularReconstructCS, Compute, "UZ_ROUGH_MOTION_AWARE=1")` (shader_manager.h:98; the pattern of
pipeline_gi.cpp:873-874), as a second pipeline `m_reconstructMotionAware` beside `m_reconstruct` in
`RoughSpecularReconstruction::Prepare` (cpp:34-43), created lazily on first use with the CVar on; on
compile failure log once and run the OFF pipeline (ssgi_renderer.cpp:540 pattern). Every new shader
line lives inside `#if UZ_ROUGH_MOTION_AWARE` so the OFF text - including the cbuffer declarations
shared with the apply shader through rough_specular_common.hlsli - is unchanged.

b2 `RoughSpecularConfig` (common:13-16; cpp:24-26), new members under the same `#if` (the C++ struct
always carries them; the OFF shader declares and reads only the first 16 bytes):

| member | source | use |
|---|---|---|
| `row_major float4x4 prevViewProj` | `frame.temporal.prevViewProj` (frame_gi_passes.cpp:898 pattern) | previous-frame projection of the virtual point and of P |
| `row_major float4x4 unjitteredViewProj` | `frame.unjitteredViewProj` (scene_renderer_frame.h:181) | current-frame projection (jitter-free, to match velocity_ps.hlsl:112-115) |
| `float4 prevCameraPos` | `m_previousCameraPos`, stored in the execution callback beside the jitter (cpp:130) from `static_cast<const PerFrameCB*>(frame.perFrameAlloc.cpuPtr)->cameraPos` (the :1062 read pattern) | parallax delta (4.3) |
| `float fireflyK` | CVar `...MotionAware.FireflyK` (default 8) | 4.4 |
| `float shortHistory` | CVar `...MotionAware.ShortHistory` (default 8 frames) | 4.4 |
| `float parallaxPower` | CVar `...MotionAware.ParallaxPower` (default 2) | 4.3 |
| `float curvatureGate` | CVar `...MotionAware.CurvatureGate` (default 1 = on, 0 = off) | 4.5 |

Size 16 + 64 + 64 + 16 + 16 = 176 bytes; `ring->Allocate(sizeof(config))` (cpp:119) grows with it.
Root constants unchanged (17 DWORDs). Reset semantics unchanged: `historyValid` already drops the
history on !matrixValid (cpp:114), which is also when prevViewProj is meaningless.

### 4.2 Virtual reflected-ray reprojection

All of this replaces step 3 of section 1.3 under the permutation; the surface path stays as the
fallback, verbatim.

1. Eye ray: incident = normalize(r.position - cameraPos) (rt_reflection_cs.hlsl:246's definition).
2. Lobe hit distance: in the existing 25-tap loop (reconstruct:33-57) accumulate one more weighted
   sum, invT += w x (t > 0 ? 1 / t : 0), over taps that carry directional information: skip a tap
   whose rgb is exactly 0 and t is 0 (the zero-throughput sample of rt_reflection_cs.hlsl:269-275 -
   it is a valid black for the mean but says nothing about where the lobe lands). A miss contributes
   1 / t = 0 (infinity) - the natural encoding of the pass's "0 = no hit" convention (:316), so no
   sentinel change in the RT pass. meanInvT = invT / weights.
3. Curvature: load `Receiver` at p +- (1, 0) and p +- (0, 1) (common:42-56; the hit-distance load
   is not needed for them). For each axis with both neighbours `Compatible` with r: kappa_axis =
   angle(N+, N-) / |P+ - P-|, signed convex-positive by dot(N+ - N-, P+ - P-) > 0; if only one
   neighbour is compatible use the one-sided difference; if none, 0. kappa = max(0, max(kappa_x,
   kappa_y)). Concave receivers are treated as planes (kappa 0) - section 7.
4. Thin lens: denom = 2 kappa + meanInvT. If denom > 1e-6 (per cm: an image closer than 10 km),
   t_v = 1 / denom and X_v = P + incident x t_v, projected as a point (w = 1). Else the image is at
   infinity: project the DIRECTION incident with w = 0 through both matrices. If either projection
   has clip.w <= 0, fall back to the surface path.
5. Virtual motion in render pixels, the velocity pass's convention (velocity_ps.hlsl:118-129,
   `NdcToRenderPixels` velocity_common.hlsli:7-12): virtualMotion = pixels(X_v through
   unjitteredViewProj) - pixels(X_v through prevViewProj). The difference cancels the fact that
   r.position lies on the jittered ray (common:49 uses the jittered invViewProj): both projections
   shift equally. Same idea as nrd_reproject_cs.hlsl:41-45 (`uv + virtualUV - currentVirtualUV`).
6. Roughness lerp (the lobe's dominant direction tilts toward N as it widens; nrd_reproject_cs.hlsl:
   45): motionUsed = lerp(virtualMotion, velocity, roughness^2). At 0.4: 84 % virtual; on Sponza
   stone (0.7-0.9): 50-80 % surface, which is right - a very wide lobe's reflection moves with the
   surface.
7. Object-motion guard: cameraMotion = pixels(P through unjitteredViewProj) - pixels(P through
   prevViewProj). If |velocity - cameraMotion| > 1 px the receiver itself moved (skinned / animated
   geometry, velocity_object_*): use the surface path with today's cap and clamp. Camera-only motion
   is the user's case and the only one the virtual point models.
8. previousPixel = p + previousJitter - currentJitter - motionUsed (the :67 form; same jitter
   handling as the surface path, so the history is sampled on the previous jitter lattice exactly as
   today). The fetch itself (:68-93) is unchanged: four bilinear taps, `Compatible` against the
   stored geometry at each tap. The stored geometry is the SURFACE at that texel; a virtual fetch
   lands where the reflection was, a few pixels along the same receiver, and the plane-distance test
   passes comfortably on the sphere (a 1.2 cm shift along a 50 cm sphere is 0.014 cm of plane
   distance against a 0.5 cm tolerance; common:61-63). It rejects correctly when the reflection has
   crossed onto another object.
9. If the virtual fetch's accepted weight is < 0.5, redo the fetch on the surface path (four more
   loads, only on rejection) and treat the result as untrusted (4.3). trusted = virtual path taken
   AND accepted >= 0.5.

Static camera: both matrices are identical, both projections coincide, virtualMotion = 0 =
velocity, delta = 0: the fetch and the cap are bit-for-bit today's (the firefly clamp is off once
count >= shortHistory, 4.4).

### 4.3 History cap and clamp under motion

Today: `HistoryCap(r, motion)` = 4 and `ClampHistory` 1-sigma for motion > 0.5 px, fastCount cap 2
(math:23-30; reconstruct:101, :111, :113). Under the permutation, with trusted = true:

- Parallax of the lobe: R = reflect(incident, N); R_prev = reflect(normalize(P - prevCameraPos), N);
  delta = acos(saturate(dot(R, R_prev))) - how far the lobe at this surface point rotated since the
  previous frame. theta_lobe = 2 atan(alpha), alpha = `ReflectionGlossyAlpha`(roughness) = max(r^2,
  0.0064) (rt_reflection_contract.hlsli:151-153): the reflected-direction spread of the GGX lobe at
  half maximum. At r 0.4: 0.317 rad (18 degrees); at r 0.8: 1.14 rad.
- `MotionCap`(roughness, delta) = max(kMotionCapFloor = 4, HistoryCap(roughness, 0) x
  saturate(1 - delta / theta_lobe)^parallaxPower). Fast orbit (2 degrees / frame = 0.035 rad) at r
  0.4: 77 x 0.89^2 = 61 frames. Slow orbit: 74. Sponza pan (0.45 degrees / frame) at r 0.8: ~126
  of the 128 static cap. A near-mirror (r 0.1, theta 1.1 degrees) under any real motion: the floor
  4 = today.
- count = min(old.a + 1, MotionCap) (replaces :113's cap); fastCount cap 8 and the 3-sigma extent:
  pass clampMotion = 0 to :101 and `ClampHistory` (:111) instead of motionLength. `HistoryCap` and
  `ClampHistory` themselves are unchanged, so the existing gtests
  (test_rough_specular_reconstruction.cpp:22-37) stand.
- Not trusted (surface fallback, object motion, accepted < 0.5): today's three rules verbatim.

Why the 3-sigma clamp is safe with a 60-frame cap: the extent is sqrt(variance / fastCount) with the
variance floored by the spatial variance (:105-106) - a light change moves the 8-frame fast mean
within ~8 frames and the clamp drags the history with it (section 6, gate G7 measures this).

### 4.4 Short-history firefly clamp

Purpose: while the reprojected history is short, one bright 1-spp sample must not enter the
estimate at a weight that dominates it. Applies to each spatial tap BEFORE it enters the 25-tap mean
and the moments (so the variance that drives the 3-sigma clamp is also protected).

- Reorder under the permutation: do the history fetch (4.2) first, the spatial loop second (today
  it is the other way round, :30-62 then :64-93; the fetch does not depend on the loop).
- Reference level: L_nb = median of the nine luminances of the unit-stride 3x3 around p (input
  texture, taps that are ineligible / not `Compatible` replaced by the centre's luminance); L_ref =
  accepted > 0 ? max(Luminance(old.rgb), L_nb) : L_nb. The median makes the reference robust to up
  to four fireflies in the 3x3; the history term keeps a converged bright highlight from being
  clipped when its own neighbourhood is dark this frame. Nine extra loads of `input`, unit stride.
- Limit: with countPrev = old.a (0 when nothing was accepted), the clamp is active while countPrev <
  shortHistory (8) and relaxes as the history builds: `FireflyLimit`(L_ref, countPrev) = L_ref x
  fireflyK x exp2(max(0, countPrev - shortHistory / 2)) -> 8 L_ref up to count 4, 16 at 5, 32 at 6,
  64 at 7, off at 8. Hue-preserving: c.rgb x= min(1, limit / l) per tap.
- Behaviour: at motion onset from a converged state countPrev is ~77 -> the clamp never engages
  (the cap drops to ~60, not below 8). After a reset, a disocclusion or a rejected virtual fetch the
  highlight fades in over ~8 frames instead of flashing; today the same frames are the sparkle.
  Static convergence is unbiased after the first 8 frames (the clamp is off), so the converged image
  is today's within the EMA residue of the clamped first frames (gate G9 bounds it at 1 %).
- Not a global energy clamp: a converged bright region (L_ref high) passes its samples at 8x its own
  level; only isolated spikes relative to the local level are cut. The reference is current-frame /
  current-history, never the carried moments (section 2.3).

### 4.5 Spatial support on curved receivers

- Fact: the support is history-independent (math:18-22). Do NOT add the disocclusion widening of
  nrd_reproject_cs.hlsl:105-127 (radius 3 when count < 2) or realtime_temporal_cs.hlsl:82-96
  (radius 1-2 on a rejected fetch); on a curved receiver those spread the brightest frame.
- Curvature gate (curvatureGate = 1): the support must not exceed the lobe's footprint on the
  receiver. Across the whole support (2 x radius taps each side, world step s = |P+ - P-| / 2 per
  pixel from the neighbours loaded in 4.2.3) the reflected direction turns by 2 kappa x s x 2 radius;
  require that to stay below theta_lobe: radiusLimit = theta_lobe / (4 kappa s), radius =
  max(1, min(SpatialRadius(...), radiusLimit)). Check on the report's sphere (rho ~50 cm, d ~10 rho
  = 500 cm, s = d / f ~0.53 cm/px at 1080p / 60 degrees, r 0.4): radiusLimit = 0.317 / (4 x 0.02 x
  0.53) = 7.5 > 2.28: inactive, so the static image on this sphere keeps today's support (gate G9).
  A 10 cm sphere (kappa 0.1): 1.5; a 5 cm pipe (kappa 0.2): 0.75 -> radius 1, and the highlight
  stays a highlight. Flat receivers (kappa 0): no change.
- With the virtual history trusted the spatial estimate's share is 1 / 60, not 1 / 4: that - not a
  radius change - is what removes the cloud (section 2.4).

### 4.6 Order of operations in the permuted shader

1. Receiver, guides out, early outs - unchanged (:20-28).
2. Neighbour receivers (4 loads), kappa, world step s (4.2.3, 4.5).
3. 3x3 median L_nb (4.4).
4. Virtual fetch: object-motion guard, meanInvT is not yet known - so the thin lens uses the
   CENTRE pixel's 1 / t for the fetch (t = r.hitDistance, 0 -> infinity) OR, preferred, the
   history's stored lobe inverse hit distance: under the permutation write meanInvT into guides.y
   (today raw hitDistance, consumed by nobody - section 1.2) and read it back at the fetched texel;
   first frame falls back to the centre's 1 / t. This keeps one pass over the taps and gives a
   temporally smoothed virtual depth. Then the surface fallback if needed (4.2.9).
5. Spatial loop with the firefly clamp on each tap, accumulating sum, moments, invT (4.4, 4.2.2);
   radius from 4.5.
6. Cap / clamp per 4.3; blend as today (:113-116); write colour (count in .a), moments, geometry,
   guides (with guides.y = meanInvT).

Everything in steps 2-5 that is new sits inside `#if UZ_ROUGH_MOTION_AWARE`; the `#else` branch is
today's statements in today's order.

### 4.7 C++ changes (rough_specular_reconstruction.cpp / .h)

- `Prepare`: second shader + pipeline for the permutation, lazily (4.1).
- `RoughSpecularConfig`: the members of 4.1; `static_assert(sizeof == 176)`.
- `Register`: pick the pipeline by the CVar (read through the registry the way frame_gi_passes.cpp:
  88-91 reads r.RayTracing.GI, or pass a bool from `RegisterRoughSpecularReconstruction`); fill
  prevViewProj = frame.temporal.prevViewProj, unjitteredViewProj = frame.unjitteredViewProj,
  prevCameraPos = m_previousCameraPos, the four tunables; after dispatch store m_previousCameraPos
  from the PerFrameCB (beside :130). Add the CVars' values to the epoch hash (:1061-1067) so a
  toggle starts a fresh history instead of blending two estimators.
- `Apply`: unchanged.
- Help text of `cv_RoughReconstruct` (:80) corrected to "default on".

### 4.8 Shared math and gtests

New C++-shared helpers in rough_specular_math.hlsli (the file is included by the gtest,
test_rough_specular_reconstruction.cpp:2): `LobeHalfAngle(roughness)`, `ThinLensDistance(kappa,
invT)` (returns a negative sentinel for "infinite"), `MotionCap(roughness, parallax, power)`,
`FireflyLimit(reference, count, k, shortHistory)`, `CurvatureRadiusLimit(roughness, kappa,
worldStep)`. Tests to add beside :6-37:

- ThinLensDistance(0, 1/100) = 100 (plane: the planar virtual point); (1/50, 0) = 25 (sphere, miss:
  rho / 2); (0, 0) = infinite sentinel; monotone decreasing in kappa and in invT.
- MotionCap(0.4, 0, 2) = HistoryCap(0.4, 0) = 77; MotionCap(0.4, LobeHalfAngle(0.4), 2) = 4;
  MotionCap(0.4, 0.035, 2) in [58, 64]; MotionCap(0.8, 0.008, 2) > 120.
- FireflyLimit(1, 0, 8, 8) = 8; (1, 5, 8, 8) = 16; (1, 8, 8, 8) = none (infinite); scales linearly
  in the reference.
- CurvatureRadiusLimit(0.4, 0.02, 0.53) > 2.28 (the report's sphere keeps today's support);
  CurvatureRadiusLimit(0.4, 0.2, 0.53) < 1.0 (the raw limit; the shader floors the radius at 1).
- The existing cap / clamp tests unchanged (OFF semantics).

## 5. CVar gating and byte-identical OFF

- `r.RayTracing.Reflections.RoughReconstruct.MotionAware` (bool, default 0 at landing) +
  `.FireflyK` (8), `.ShortHistory` (8), `.ParallaxPower` (2), `.CurvatureGate` (1), registered beside
  `cv_RoughReconstruct` (frame_gi_passes.cpp:79-80) with `UZ_CVAR_BOOL / UZ_CVAR_FLOAT`.
- OFF: the OFF pipeline is compiled from a preprocessed text identical to today's (all additions
  under `#if UZ_ROUGH_MOTION_AWARE`, including the cbuffer members), so its DXIL hash is unchanged
  and the output is byte-identical by construction; the larger CB upload is not read by it. Gate G6
  verifies the hash and the dump.
- ON is a lazily compiled second pipeline; the toggle changes the epoch (4.7) so the switch is a
  clean restart of the history, never a blend of two estimators.
- Default: flip to ON in the commit that carries section 6's numbers from the moving-camera lane,
  not before. RoughReconstruct went default-ON with a documented "motion smear" caveat after a static
  validation (report.md:17, :368-369); this feature must not repeat that.

## 6. Validation on a MOVING camera - metrics and gates

Harness: the report's, unchanged (`_evidence/gi/motion-artifacts`: capture_ma.py, matrix_ma.py,
analyze_motion.py, blob_table.py; Lua paths showcase_fast.lua / showcase_dolly.lua /
showcase_motion.lua / sponza_motion.lua; LINEAR HDR region dumps of specular_indirect and
taa_output, never PNGs; report.md:24-60). Lane D settings. Three runs per path: ON (MotionAware 1),
OFF (today), no_roughrecon; identical frame-indexed camera paths so the parallax term cancels. Place
the dolly ROIs on Sphere_DarkMetal (the report's dolly ROIs sat on the chrome sphere, :396).

| # | metric (specular_indirect unless stated) | today (OFF) | gate for ON |
|---|---|---|---|
| G1 | DarkMetal fast orbit, moving / static ratio of sphere area > 1.0 | 1.9 % / 0.9 % = 2.1 | <= 1.3 |
| G1b | same, area > 1.5 | 0.67 % / 0.09 % = 7.4 | <= 2.0 |
| G1c | same, spec mean moving vs static | +15 % | within +-5 % |
| G1d | same, p99 moving vs static | 1.33 vs 0.95 | <= 1.1 x static |
| G2 | not the sparkle: slow-orbit motion-compensated per-frame change of the sphere's specular (DM_hi / DM_lo ROIs) | 4.6 % ON, 49.5 % no_roughrecon | <= 4.6 % |
| G2b | fast orbit spec max on the sphere | 2.3 ON, 10.3 no_roughrecon | <= 3.0 |
| G3 | dolly frames 90-145, G1 / G1b on DarkMetal | (re-measure) | same ratios as G1 / G1b |
| G4 | Sponza fly / pan: specular pixels changing > 25 % per frame (5 ROIs) | 34.0 % / 40.0 % (static 13.8 %) | <= 25 % / <= 30 % |
| G4b | Sponza fly: taa_output pixels changing > 25 % per frame | 0.63 % (static 0.06 %) | <= 0.45 %, and no pan / hold metric above today's |
| G5 | mirror unchanged: chrome sphere (roughness <= 0.04) specular_indirect, ON vs OFF, every frame | - | max abs diff = 0 (the apply pass never writes mirrors, apply:13) |
| G6 | OFF byte-identical: MotionAware 0 vs the pre-change build, whole path, specular_indirect and taa_output dumps; DXIL hash of RoughSpecular_Reconstruct | - | bit-identical dumps; identical hash |
| G7 | light-change lag: a scripted sun-intensity step at a fixed frame, static and during the fast orbit; frames to reach 90 % of the new converged sphere mean | measure in the same run (static ~3-sigma clamp dynamics; moving: 4-frame cap) | static: today +-1 frame; moving: <= 12 frames, no overshoot beyond today's |
| G8 | GPU: RoughSpecular_Reconstruct ms (manifest gpu_ms, frames 120-369) | 0.41 ms (report.md:77) | <= 0.55 ms |
| G9 | static convergence unchanged: hold window (frame >= 200), sphere spec mean / p99, ON vs OFF | - | within 1 % / 2 % |
| G10 | no new ghosting: fast strafe (24 cm / frame), motion-compensated residual of the sphere's specular and a visual trail check on the montage | residual today 4.6 % class | <= today's; no trail longer than the static highlight's width |

Acceptance = every gate; the default flip (section 5) cites the table. If G1 passes but G2 fails the
cap is too low or the firefly clamp too aggressive (raise ParallaxPower's effect by lowering it to
1.5, or ShortHistory to 6); if G1 fails with G2 passing the virtual fetch is being rejected (check
the accepted weight: add a debug channel of `trusted` to the diagnostics route the realtime denoiser
already has, realtime_temporal_cs.hlsl:17, :100) or kappa is zero on the sphere (normal map /
neighbour compatibility).

## 7. Risks

1. Ghosting from an over-trusted virtual history. A systematic per-frame reprojection error
   epsilon leaves a trail of length ~epsilon x cap; at cap 60 a 0.2 px bias is a 12 px trail. Sources
   of bias: kappa from shading normals (a normal map on a flat wall yields spurious curvature -> a
   virtual depth too shallow -> the reflection sticks to the surface, which is the roughness-lerp
   direction and mild), the lobe hit distance from 1-spp samples (temporally smoothed via guides.y,
   4.6.4), the roughness^2 lerp being only a proxy for the dominant direction. Guards: `Compatible`
   on every fetched texel, the parallax cap, the 3-sigma clamp against an 8-frame fast mean, the
   ParallaxPower tunable, G10.
2. Hit-distance accuracy. Along the sampled VNDF direction, not the mirror direction; 0 for misses
   and for zero-throughput samples (indistinguishable at the texture level, rt_reflection_cs.hlsl:
   273, :316) - the rgb == 0 exclusion in 4.2.2 separates them. On curved receivers the thin lens is
   insensitive to t (t_v -> rho / 2 as t -> infinity); on planes t errors map 1:1 into virtual depth
   and the error grows with camera translation: bounded by the roughness lerp for rough planes, by
   the cap floor and `Compatible` for glossy ones.
3. Interaction with the SSR temporal and TAA. The SSR temporal is a static-only filter today
   (section 2.3): the reconstruct's input regime changes at motion onset / stop, the carried moments
   lag by a few frames, and the 3-sigma clamp may clip a legitimate history for one or two frames
   (a dimming blink). The firefly reference is taken from the current frame for that reason. If
   section 8's SSR fixes land later, two reprojected histories stack: re-run G1-G10. TAA
   (taa_resolve_cs.hlsl) receives a smoother specular_indirect under motion - less for its variance
   clip to reject (G4b should drop) - but its own clip can still eat a converged highlight on a
   moving sphere; that is unchanged from today and outside this pass.
4. Metallic receivers. DarkMetal is metallic 1: the reflection IS the surface (no diffuse to hide
   temporal error), and the sample weight's F0 proxy is 1 (rt_reflection_contract.hlsli:145-147),
   so window hits carry full Fresnel - the largest fireflies. The clamp constants are tuned on this
   case; coloured metals get a hue-preserving (luminance-scaled) clamp. Dielectrics are strictly
   easier.
5. Concave receivers (kappa < 0): the thin lens has a real-image singularity at 2 kappa t = -1; kappa
   is clamped to >= 0 so a concave surface reprojects as a plane - today's error, no worse.
6. Virtual fetch across object boundaries: `Compatible` rejects, the surface fallback runs, the
   firefly clamp engages for ~8 frames at those pixels (a fade-in fringe behind a moving occluder
   edge) instead of today's sparkle fringe. Acceptable; visible only on bright reflections.
7. Cost: four `Receiver` loads, nine input loads, two to four mat4 x vec4 per pixel, one acos.
   Estimated +15-25 % of 0.41 ms (G8).

## 8. Out of scope: pre-existing bugs found while reading (follow-ups, each its own gates)

1. ssr_temporal_cs.hlsl treats the pixel-space velocity as UV (:183, :187) against velocity_ps.hlsl:
   11-12 / velocity_common.hlsli:4-5; `VELOCITY_THRESHOLD` (:54) is UV in the comment and pixels in
   the comparison (:251-253). Effect: the SSR temporal is a static-only filter; any moving pixel is
   mostly output as the current frame. Fixing it changes the MIRROR route (it is the mirror denoiser)
   and the input regime of this pass: separate change, re-run section 6 afterwards.
2. ssr_temporal_cs.hlsl's "virtual hit" is on the wrong side of the surface: V points to the
   camera (:162) and the point is surface + V x hitDist (:176), in front of the mirror; the unfolded
   image is surface - V x hitDist (nrd_math.hlsli:31-34 has the correct form). The confidence lerp
   (:182-183) then blends a wrong coordinate with the surface one.
3. ssr_temporal_cs.hlsl:246-248 `roughnessProxy = saturate(currentSSR.a)` reads the blend weight;
   in rough mode that is `intensity` (rt_reflection_contract.hlsli:107-108), so the accumulation
   target is 32 samples at every roughness, not the roughness-driven 4-32 the comment describes.
4. `cv_RoughReconstruct` help string says "default off" while the default is true
   (frame_gi_passes.cpp:79-80) - fixed in passing by 4.7.
5. The hit-distance texture cannot distinguish a miss from a zero-throughput sample (both 0:
   rt_reflection_cs.hlsl:273, :316); this spec works around it at the reader (4.2.2). A negative
   sentinel for "zero throughput" in the RT pass would be cleaner but touches the mirror route's
   `SpatialRadius` input (math:19-20) and the SSR temporal (:146); not now.

## 9. Files touched by the implementation

- runtime/shaders/runtime/denoising/rough_specular_reconstruct_cs.hlsl - the permuted path
  (4.2-4.6) under `#if UZ_ROUGH_MOTION_AWARE`; today's statements verbatim otherwise.
- runtime/shaders/runtime/denoising/rough_specular_common.hlsli - the b2 cbuffer members under the
  same `#if` (common:13-16).
- runtime/shaders/runtime/denoising/rough_specular_math.hlsli - the five helpers of 4.8 (C++-shared).
- runtime/engine/renderer/passes/gi/rough_specular_reconstruction.cpp / passes/
  rough_specular_reconstruction.h - second pipeline, config struct, prevCameraPos state, pipeline
  selection (4.7).
- runtime/engine/renderer/frame/frame_gi_passes.cpp - five CVars beside :79-80, epoch hash
  (:1061-1067), the corrected help text.
- runtime/tests/engine/renderer/test_rough_specular_reconstruction.cpp - the tests of 4.8.
- Not touched: rt_reflection_cs.hlsl, the SSR passes, rough_specular_apply_cs.hlsl, root
  signatures, any texture or graph resource.
