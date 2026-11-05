# 04 - DDGI / RTXGI probe stability, mapped onto the radiance cache

Date: 2026-11-02. Design study, read-only (no code was changed). Companion to the Lumen
radiance-cache study and the UzEngine wobble diagnosis in this folder.

Question: the world-space radiance cache (16^3 L2-SH probes, SDF-traced) "crawls". DDGI and
NVIDIA RTXGI solved the same class of problem for octahedral probes. Which of their mechanisms
address which of our crawl sources, which ones does UzEngine's own DDGI already implement (so
the fix can reuse them), and what should the fix be?

## 0. Answer first

The crawl has three producers in the cache and one in its readers, and the DDGI/RTXGI toolkit
has one mechanism per producer:

| Crawl source (cache as it is) | DDGI / RTXGI mechanism |
|---|---|
| A. Flat EMA (alpha 0.12 per update, 32 rays, every probe every 4th frame): the estimate never settles, it keeps a stationary noise floor | Hysteresis blend (0.97) kept high because a per-texel **change detector** re-arms it when lighting really changes; 256 rays; a fresh/cleared probe takes the new value at once |
| B. Probes near surfaces flip valid/rejected: a binary "inside" test on the biased distance field, SH **zeroed** when inside, **replaced** by a single 32-ray estimate when valid again, binary guard-plane rejection in the readers | **Relocation** instead of rejection (persistent offset, deterministic fixed rays, relaxes back), **classification** that keeps a probe's data while inactive, continuous **Chebyshev** visibility instead of a plane test |
| C. Grid slide: the slab that enters the grid on a recentre starts from one 32-ray trace; the 1024-probe budget sweeps the grid in four horizontal slabs | Cleared-probe rule + change detector converge a new probe in a few updates; every probe is updated every frame (budget is spent by classification, not round-robin) |
| D. Reader side: hard-zero normal weight, zero indirect when all eight probes are rejected, no surface bias, `max(EvalSH,0)` clamp bias on noisy SH | Wrap-shading floor (+0.2), Chebyshev floor (0.05), weight crushing, surface bias `n*0.1 + v*0.1`, perceptual encoding |

What UzEngine's DDGI already has that the cache can reuse: the **depth-moment atlas writer**
(`ddgi_probe_update_cs.hlsl`, atlas mode 1), the **Chebyshev consumer** block
(`deferred_lighting_ps.hlsl` 1108-1135), the **border-fill pass**, the **per-ray buffers**
(block B.6) and - decisive for reuse - the **same probe grid and the same ray sequence**
(`ProbeRayDirection` in `ddgi_probe_common.hlsli` is the cache's construction, line for line).

What UzEngine's DDGI does **not** have: its blend is the same flat `lerp(prev, new, 0.12)`
(`ddgi_system.cpp:209`), it has no relocation, no classification, no variability. So "reuse the
DDGI stabilisation" means reusing DDGI's *data layout, depth writer and visibility consumer*; the
hysteresis has to be written once, in a shared include, and both writers get it. Meanwhile the
cache already owns the fixed-ray classifier RTXGI relocation needs (`probe_side_cs.hlsl`,
64 deterministic Fibonacci rays against the baked triangles) and a relocation-aware reader API
(`rc_probe_guard.hlsli`: `ProbeEffectivePosition`, `ProbeGuardRejects`, used by seven consumers) -
both shipped but off by default (`r.GI.RadianceCache.ProbeClassification` = 0).

Recommendation (section 5): a shared probe-management layer, built in three stages so each stage
is measurable on its own. Stage 1 is shader-only and attacks A, C and the writer half of B:
DDGI-style hysteresis with a noise-normalised change detector on the SH, keep (do not zero) the SH
of a probe that goes inside and blend instead of replace when it returns, seed the entering slab
from its inward neighbour, interleave the update budget spatially, and give the reader block the
wrap-shading floor and surface bias. Stage 2 gives the cache a Chebyshev depth atlas by writing
its rays into a buffer in the DDGI layout and running the existing DDGI depth pass and consumer
on it. Stage 3 turns the existing fixed-ray classifier into RTXGI-style persistent relocation
offsets shared by both grids.

## 1. The cache as it is (from the code)

| Item | Value | Where |
|---|---|---|
| Grid | 16^3 = 4096 probes, camera-centred, snapped to the spacing; spacing = 2*extent/(N-1) = 666.7 cm at the default `r.GI.RadianceCache.GridExtent` 5000, 266.7 cm on Sponza | `radiance_cache.cpp` `CreateResources`, `Update` |
| Rays | 32 per probe per update (`r.GI.RadianceCache.RaysPerProbe`, 4-64), uniform full sphere, Monte-Carlo projected to 9 L2 coefficients x 3 channels, 7 float4 per probe | `radiance_cache_trace_cs.hlsl` `main` |
| Update budget | 1024 probes per frame, contiguous from a cursor (`r.GI.RadianceCache.ProbesPerFrame`); the whole grid on a recentre or invalidation | `radiance_cache.cpp` `UpdateProbes` |
| Blend | `a = hasHistory ? (fieldChanged ? regionBlendAlpha : temporalBlendAlpha) : 1`; `lerp(history, new, a)`. `kTemporalBlendAlpha = 0.12f` (`radiance_cache.h:97`); the header prose and the struct comment still say 0.05 - stale. `regionBlendAlpha` default 1.0 = replace inside a change box | trace shader, end of `main` |
| Inside test (mode 0, default) | `SampleGdfSurface(probePos).r < 5 cm` on the biased, unsigned global field; inside -> all 7 SH records written to 0 (validity `record6.w = 0`) | trace shader `main` |
| Inside test (modes 1-3, off by default) | 1/2: back-face fraction of 64 fixed rays > 0.25, nearest face from `probe_side_cs`, trace origin moved off the face (`RelocateOrigin`, 10 cm clearance, <= half a spacing), guard plane written. 3: flood fill from the camera (`probe_reach_cs`), optional `ReachRelocate` to reached air | trace shader, `probe_side_cs.hlsl`, `probe_reach_cs.hlsl` |
| History on return | `hasHistory = validity[history].w > 0` -> a probe that was inside last update has no history and **replaces** with the new 32-ray estimate | trace shader |
| Recentre | whole-probe shift; history read at `coord + shift`; the slab that entered has no history -> replace | `radiance_cache.h` `m_pendingHistoryShift`, trace shader `HistoryShift` |
| Reader (deferred) | trilinear x `saturate(dot(geometricN, toProbe))^2` x validity x guard plane; `max(EvalSH_L2, 0)`; **zero** irradiance when the eight weights sum to zero | `deferred_lighting_ps.hlsl` 1165-1250 |
| Other readers | `SamplePreviousDiffuse` (writer feedback), `sdf_reflections.hlsli`, `surface_cache_{feed,hit,indirect,relight}`, `rt_reflection_cs.hlsl`: same weights through `rc_probe_guard.hlsli` | grep `ProbeGuardRejects` |

Cadence: with the 1024 budget each probe is updated every 4th frame, so the per-frame-equivalent
hysteresis is 0.88^(1/4) = 0.97 - the *time constant* (~33 frames) already matches RTXGI's
default. What differs is the noise per update and how it is spent:

| | Rays per update | Updates | Single-estimate CV of a half-lit DC term, sqrt(p(1-p)/N)/p | Stationary EMA factor sqrt(a/(2-a)) | Temporal CV of the stored value |
|---|---|---|---|---|---|
| Cache today | 32 | every 4th frame, a = 0.12 | 0.177 | 0.253 | **4.5 %** |
| Same time constant, every frame | 32 | every frame, a = 0.03 | 0.177 | 0.123 | 2.2 % |
| RTXGI default | 256 | every frame, h = 0.97 | 0.063 | 0.123 | **0.8 %** |

(Estimate: Bernoulli hit/sky model for the DC band; the L1/L2 bands are noisier. The point is the
ratio, ~6x, of which ~2.8x is the ray count and ~2x the coarse cadence.) 4.5 % independent
per-probe noise that re-randomises every 4 frames, trilinearly interpolated, is a textbook crawl.
A flat EMA cannot remove it: lowering `a` lowers the floor but lengthens the response to real
changes by the same factor. DDGI's answer is to decouple the two with a change detector.

The slab structure: probes are x-fastest, 256 per z layer, so a 1024 budget is four horizontal
layers (Z is up here - the sky swizzle is `(x, z, y)`); the noise refresh sweeps floor to ceiling
every four frames instead of shimmering uniformly.

Prior measurement that bears on source B: `docs/analysis/rc-probe-support-followups/07-support-motion.md`
recorded a 0.46 correlation between per-frame diffuse steps and per-frame `rc_support` weight
steps on the `left_wall` ROI (Sponza motion sweep, 201 frames): the reader's probe set changing
is a measurable part of the temporal signal.

## 2. The DDGI / RTXGI toolkit

Sources: Majercik, Guertin, Nowrouzezahrai, McGuire, "Dynamic Diffuse Global Illumination with
Ray-Traced Irradiance Fields", JCGT 8(2) 2019 (https://jcgt.org/published/0008/02/01/);
Majercik, Mueller, Marrs, Moreau, Dupuy, Nowrouzezahrai, McGuire, "Scaling Probe-Based Real-Time
Dynamic Global Illumination for Production", JCGT 10(2) 2021 (https://jcgt.org/published/0010/02/01/,
arXiv https://arxiv.org/abs/2009.10796); NVIDIA RTXGI-DDGI SDK (https://github.com/NVIDIAGameWorks/RTXGI-DDGI),
files `rtxgi-sdk/shaders/ddgi/ProbeBlendingCS.hlsl`, `ProbeRelocationCS.hlsl`,
`ProbeClassificationCS.hlsl`, `Irradiance.hlsl`, `include/rtxgi/ddgi/DDGIVolume.h`. The
constants below were read from those SDK files on 2026-11-02.

### 2.1 Hysteresis blending with change detection

RTXGI `ProbeBlendingCS.hlsl`, one thread per atlas texel, paraphrased:

```
// new estimate for this texel (irradiance pass)
E_new = sum_i(w_i * L_i) / (2 * sum_i w_i),  w_i = max(0, dot(texelDir, rayDir_i))
   - the 32 fixed rays are excluded when relocation/classification are on (they would bias E)
   - a back-face ray contributes nothing; if back-face rays exceed 10 % of the random rays
     (probeRandomRayBackfaceThreshold) the texel is NOT updated at all this frame (return)
E_new = pow(E_new, 1/5)                               // probeIrradianceEncodingGamma: blend in perceptual space

h = 0.97                                              // probeHysteresis
if (E_prev == 0) h = 0                                // cleared / brand-new probe: take E_new outright
delta = E_new - E_prev
if (maxComponent(|E_prev - E_new|) > 0.25) h = max(0, h - 0.75)   // probeIrradianceThreshold: real lighting change -> h = 0.22 this frame
if (luminance(delta) > 0.10)              delta *= 0.25           // probeBrightnessThreshold: huge jump (firefly) -> step at most a quarter of it
step = (1 - h) * delta
if (darkening) |step| = clamp(|step|, 1/1024, |delta|)            // never stall below the storage precision
E = E_prev + step

// depth pass: the same hysteresis, no thresholds
w_i = pow(max(0, dot), 50)                            // probeDistanceExponent
d_i = min(|hitT_i|, 1.5 * |probeSpacing|)             // clamp: a far hit must not dominate the variance
(mean, mean2) = lerp((sum w d, sum w d^2) / sum w, prev, h)
```

Why it is stable *and* responsive: `h = 0.97` gives a ~33-frame time constant and a stationary
noise factor of 0.12 (table above); the detector takes over only when the per-texel change is
far above the noise, and then for one frame the texel moves 78 % of the way (times the 0.25 clamp
when the jump is also large, i.e. ~20 %/frame on a light switch - a short visible fade instead of
a pop). With 256 rays, the cosine filter (each texel integrates about half the rays) and the
gamma-5 space, single-frame noise rarely crosses 0.25, so the detector does not misfire. The
2019 paper used a fixed hysteresis (0.9-0.98 depending on scene motion, lowered globally by the
application on fast light motion); the per-texel detector and the brightness clamp are the 2021
production additions.

Mapping to an SH probe. SH coefficients are linear in radiance, so an EMA on the 27 coefficients
is an EMA on the radiance field - the blend itself carries over unchanged (`lerp(history, new,
1-h)` on the 7 float4). What does not carry over is the detector: at 32 rays the DC term's
single-update CV is ~18 %, so a fixed 0.25 threshold would fire on noise and *lower* stability.
The detector must be noise-normalised. The ray loop already has the 32 per-ray luminances; one
extra accumulator (`sum lum^2`) gives the sample variance `s^2`, hence the standard error of the
DC estimate `sigma = s / sqrt(N)`. Rule for the cache:

```
muNew  = DC luminance of the new estimate, muHist = DC luminance of the history
if (|muNew - muHist| > max(3 * sigma, 0.05 * max(muHist, muNew)))  h = max(0, h - 0.75)   // re-arm
if (|muNew - muHist| > 1.0 * max(muHist, eps))                      delta *= 0.25          // clamp, applied to all 27 coefficients
```

A constant-radiance scene (`r.GI.Diagnostic.KnownRadiance`) must never trip the first rule; that
is the acceptance test for the thresholds. Gamma encoding does not apply to signed SH; it is not
needed once the detector is normalised to the estimator's own noise.

### 2.2 Probe relocation

RTXGI `ProbeRelocationCS.hlsl`, one thread per probe, every frame, from the **32 fixed rays**
(spherical Fibonacci, the same directions every frame; `RTXGI_DDGI_NUM_FIXED_RAYS = 32`). A
back-face hit is stored as `-hitT * 0.2` so it reads as a nearby occluder and marks the sign:

```
offset = stored per-probe offset (persists across frames)
count back faces, closest back face (dir, dist), closest and farthest front face (dir, dist)
if (backFaces / numRays > 0.25)                                      // probeFixedRayBackfaceThreshold: inside geometry
    full = offset + closestBackDir * (closestBackDist + 0.5 * minFrontfaceDistance)   // push out through the nearest back face
else if (closestFrontDist < minFrontfaceDistance)                    // probeMinFrontfaceDistance (1.0 scene units default)
    if (dot(closestFrontDir, farthestFrontDir) <= 0)                 // only if moving away does not approach another wall
        full = offset + farthestFrontDir * min(farthestFrontDist, 1)
else if (closestFrontDist > minFrontfaceDistance)                    // clear: relax back toward the grid point
    full = offset - normalize(offset) * min(closestFrontDist - minFrontfaceDistance, |offset|)
if (|full / probeSpacing| < 0.45) offset = full                      // never more than 45 % of a cell
```

Every reader (irradiance sampler, blending's `probeRayOrigin`, relocation itself) uses
`gridPosition + offset`; trilinear weights stay on the grid.

Why relocation stops boundary flicker where a valid/rejected flag does not:

1. The probe is never removed from the reader's set; it is *moved*. The set of eight probes a
   receiver interpolates is constant, so no term of the sum switches on and off.
2. The decision is driven by deterministic rays against the geometry, not by the per-frame
   random rays and not by a field whose voxel phase follows the camera. The same geometry gives
   the same answer every frame.
3. The offset is a state with hysteresis of its own: it persists, moves by bounded steps, and
   relaxes toward zero only when there is slack (`closestFrontDist > minFrontfaceDistance`), so a
   probe sitting exactly at a threshold does not oscillate.
4. The 0.45-cell cap keeps the moved probe nearer its own grid point than any neighbour's, so the
   trilinear weights remain meaningful.

UzEngine today: mode 0 is a binary flag on the biased field (`< 5 cm`), and the field is "biased
by half a mesh voxel plus half a global voxel" and re-rasterised as the camera moves
(`probe_side_cs.hlsl` header) - a probe 3-7 cm from its wall can flip as the clipmap phase
changes. Modes 1-3 relocate the *trace origin* once, deterministically from the triangles
(mode 1) or from a flood fill (mode 3), with a guard plane; the DDGI writer has no relocation at
all (`ProbePosition` is the grid point). The pieces RTXGI needs already exist: `probe_side_cs`
(fixed rays, back-face fraction, nearest face), the guard record (xyz offset, `w < 0` encoding for
"relocated"), and `ProbeEffectivePosition` in every consumer.

### 2.3 Classification and variability

RTXGI `ProbeClassificationCS.hlsl`, from the fixed rays only:

```
if (backFaces / 32 > 0.25)                           state = INACTIVE   (inside geometry)
else if (any front-face hit lies inside the probe's cell box, i.e. hitT <= distance to the
         +-probeSpacing planes along the ray)        state = ACTIVE
else                                                 state = INACTIVE   (nothing nearby to light)
```

Consequences: an inactive probe still traces its 32 fixed rays (so it can come back,
`ProbeTraceRGS.hlsl:57`) but skips the other 224; the blend pass returns early for it, so its
texels **keep their last values**; the irradiance sampler skips it (`Irradiance.hlsl:105`). The
stability property is "inactive is not zero": when the probe reactivates, blending resumes from
a plausible value with the normal hysteresis, and the change detector takes care of the case
where the lighting moved meanwhile.

Variability (SDK 1.3+): the blend pass writes per texel the coefficient of variation
`sqrt(var) / mean` of the irradiance update (luminance, in encoded space); a reduction averages it
over the volume; the application compares the average to a threshold of its choosing and lowers
the update rate or stops updating a converged volume. It is a cost control, not a stability
mechanism, but it is the right readout for "has this grid converged", and it is cheap.

UzEngine today: the cache has "inside" (SH zeroed, validity 0, replace on return) and no
"nothing nearby" state; the DDGI writer updates all 4096 probes every frame
(`rootConstants[6] = 0; // full grid each frame for now`). Neither tracks variability.

### 2.4 Visibility: depth moments and the Chebyshev test

Writer (2019 paper section 3.4 / RTXGI): per probe, a 16x16 (14 interior in the SDK) octahedral
tile of `(mean distance, mean distance^2)` filtered with `pow(cos, 50)` and clamped to 1.5
spacings, blended with the same hysteresis. Reader (`Irradiance.hlsl`):

```
p  = worldPos + n * normalBias + (-viewDir) * viewBias          // 2.5
for each of the 8 probes:
    w  = ((dot(dir(worldPos->probe), n) + 1) / 2)^2 + 0.2         // wrap shading: never zero
    (mean, mean2) = 2 * depthTile.Sample(oct(-dir(p->probe)))      // the writer halved the moments
    var = |mean^2 - mean2|
    if (dist(p, probe) > mean)  c = (var / (var + (dist - mean)^2))^3   else c = 1
    w *= max(0.05, c)                                              // Chebyshev floor
    w  = max(w, 1e-6); if (w < 0.2) w *= w^2 / 0.04                // crush tiny weights, keep the curve continuous
    w *= trilinear
```

Why it matters for temporal stability, not only for leaks: every factor is continuous in space
and in time (the moments are themselves EMA-blended), so as a receiver or a probe moves the
contribution fades rather than switches; and a probe behind a wall is suppressed by *distance
statistics of the probe's own rays*, which a plane test cannot express when the occluder is not
the probe's nearest face.

UzEngine's DDGI already has both halves. Differences from the reference worth noting while
reusing: writer weights are `w*w` instead of `pow(w, 50)`; hit distances are not clamped at 1.5
spacings but normalised by `maxTraceDistance = 32 spacings` (the FP16 saturation this once caused
is documented in the shader); the reader uses `variance = max(mean2 - mean^2, 1e-4)`, cubes the
visibility (correct; the comment's "pow 8" is not what the paper does), and applies **no**
wrap-shading or backface term, **no** 0.05 floor, **no** surface bias. The cache has none of this:
`rc_probe_guard.hlsli` is a half-space test with a 5 cm tolerance - binary, geometry-stateless,
and only about the probe's own relocation face.

### 2.5 Self-shadow bias and the knobs that move temporal stability

| Knob | RTXGI default | Effect on stability |
|---|---|---|
| `probeHysteresis` | 0.97 | noise floor vs. lag; raise only together with a change detector |
| `probeIrradianceThreshold` | 0.25 (encoded) | re-arm sensitivity; too low = detector fires on noise = crawl |
| `probeBrightnessThreshold` | 0.10 (encoded luminance) | firefly clamp; the step is quartered |
| `probeIrradianceEncodingGamma` | 5 | perceptual blend space; equalises visible noise across brightness |
| `probeNumRays` | 256 | single-update variance (1/N) |
| `probeDistanceExponent` | 50 | sharper visibility, risk of banding; too low = leaks that flicker |
| depth clamp | 1.5 x spacing | keeps variance meaningful; without it far hits inflate `var` and visibility goes to 1 |
| `probeNormalBias` / `probeViewBias` | 0.1 / 0.1 scene units (2019 form: `(n + 3 w_o) * bias`) | moves the visibility query off the surface, where `dist ~ mean` would make Chebyshev a coin flip per frame |
| wrap shading `+0.2`, Chebyshev floor `0.05`, crush `< 0.2` | fixed | continuity: no term reaches exactly zero, no probe set flips |
| `probeRandomRayBackfaceThreshold` | 0.10 | skip the update (keep texels) when the random rays say "inside" this frame |
| `probeFixedRayBackfaceThreshold` | 0.25 | inside decision for relocation/classification, on deterministic rays |
| `probeMinFrontfaceDistance` | 1.0 scene units | how close to a wall a probe may sit; the relocation dead-band |
| relocation cap | 0.45 x spacing | bounded offsets |
| back-face distance | `-hitT * 0.2` | a back-face hit counts as a close occluder in the moments |

The self-shadow bias deserves a sentence of its own: a receiver on a wall queries a probe whose
mean distance in that direction *is* the wall; `dist(p, probe) > mean` then flips with
sub-centimetre noise, frame to frame. Pushing the query point along `n` and toward the camera by
a fraction of the spacing gives the test a margin. UzEngine's DDGI block and cache block both
query at the raw `worldPos`.

## 3. Mapping onto the cache's crawl sources

### 3.1 Source A - the EMA noise floor

Today: `a = 0.12` per update, every 4th frame, 32 rays; 4.5 % stationary CV per probe (section 1).
DDGI mechanism: 2.1. For the cache this becomes (i) the blend expressed as a per-frame hysteresis
`h_f` raised to the probe's update interval, `h = h_f^k`, so the time constant is independent of
the budget; (ii) `h_f` raised to 0.97-0.98 **only together with** the noise-normalised detector,
which gives real changes the fast path the flat EMA had to leave open; (iii) the cleared-probe
rule (`a = 1` without history) kept as is; (iv) more rays where the budget allows - 64 rays
halves the variance and is already a CVar. Blending in SH space is exact, so none of DDGI's
texel machinery is needed for this item.

### 3.2 Source B - valid/rejected flips at surfaces

Today, three binary switches stack at a wall: the inside flag (mode 0, on the camera-phased
biased field), the SH zeroing + replace-on-return, and the readers' `normal^2` weight falling to
exactly zero plus the guard half-space. DDGI mechanisms: 2.2, 2.3, 2.4, 2.5.

Writer half, cheapest first: stop zeroing the SH of an inside probe - write validity `w = -1`
("invalid, data kept") instead of 0 ("never written"); every reader already tests `w > 0`, so
nothing leaks, and the writer's `hasHistory` becomes `w != 0`, so a probe that returns **blends**
from its last good SH instead of replacing with one noisy trace. This is RTXGI's "inactive is not
zero" (2.3) and costs one comparison. Then move the inside decision off the biased field onto the
deterministic fixed-ray classifier that already exists (`probe_side_cs`, mode 1's rule - back-face
fraction > 0.25, the threshold the H-64 bimodality measurement supports), and turn its one-shot
origin move into an RTXGI-style persistent offset (2.2) stored in the guard record the readers
already honour.

Reader half: the wrap-shading floor `+0.2` in place of `saturate(dot)^2 -> 0`, a surface bias on
the query point, and - Stage 2 - Chebyshev visibility from a cache depth atlas (2.4) replacing the
guard half-space as the leak control. With those, a receiver near a wall sees eight weights that
move smoothly as it and the probes move; the correlation measured in `07-support-motion.md`
should drop.

### 3.3 Source C - the grid slide and the slab cadence

Probe positions are a world lattice (the centre is snapped to the spacing), so a recentre does
not move probes; it exposes a new slab whose history is replaced by one 32-ray trace and then
averaged in at `a = 0.12` - a band of 4.5 %-noise probes with a wrong first guess, once per
spacing of travel. RTXGI's scrolling volumes clear the scrolled-in plane and rely on the
cleared-probe rule plus 256 rays. For 32 rays a better first guess is free: the full retrace on
the recentre frame can read history at the shifted coordinate **clamped into the grid** (the
inward neighbour one spacing away) and blend it at a low hysteresis (0.5) rather than replacing -
continuous with the slab that already converged, and corrected by the detector where it is wrong.

The slab cadence is a free fix as well: update the probes whose `(gx + gy + gz) mod 4 == frame
mod 4` instead of 1024 contiguous indices. Every cell's eight corners then span all four phases,
so each frame refreshes a quarter of every cell's support instead of a whole layer every fourth
frame. (Dispatch 4096 threads and early-out the three quarters that are out of phase; the cursor
logic goes away.) If budget permits, DDGI's own answer - every probe every frame at `h = 0.97` -
is the same time constant with 4x less variance; the trace cost of that should be measured, not
assumed (the DDGI pass measured 3.57 ms at 16^3 x 32 rays *before* the B.6 de-duplication; the
cache's own pass has no number in the docs I read).

### 3.4 Source D - the readers

Covered in 3.2: wrap-shading floor, surface bias, Chebyshev. One more: `max(EvalSH_L2, 0)` on a
noisy SH is a positive bias that *moves* with the noise (the trace shader's own comment on the
region blend says so). Lowering the noise (3.1) lowers the bias; nothing else to do here.

## 4. Reuse inventory - what UzEngine's DDGI already implements

| Mechanism | UzEngine DDGI | Radiance cache | Reuse verdict |
|---|---|---|---|
| Hysteresis with change detection | **No** - `lerp(prev, new, 0.12)` for both atlases (`ddgi_probe_update_cs.hlsl`, `ddgi_system.cpp:209`) | **No** - flat 0.12, replace inside change boxes | Write once as `gi_probe_blend.hlsli` (hysteresis from update interval, detector, clamp, cleared rule); include from both writers. The DDGI writer benefits too: at full-grid cadence its 0.12 is an ~8-frame time constant |
| Cleared/fresh probe takes the new value | **No** - the first update is `lerp(0, new, 0.12)` against the cleared atlas, so every texel fades in from black over ~8 frames; only the trace pass's feedback read checks `stored.w <= 0` | Yes (`hasHistory` false -> a = 1) | Keep the cache's rule; the shared blend include gives the DDGI writer the same rule (RTXGI: `if (prev == 0) h = 0`) |
| Relocation (persistent offset) | **No** - `ProbePosition` = grid point | **Partial** - modes 1/3 move the trace origin once, guard record carries the offset (`w < 0`), off by default | Generalise `probe_side_cs` output into an RTXGI offset pass shared by both grids; DDGI's `ProbePosition` and its lighting block read the offset; the cache's readers already do (`ProbeEffectivePosition`) |
| Deterministic fixed rays | **No** | **Yes** - `probe_side_cs.hlsl`: 64 spherical-Fibonacci rays vs. the baked triangles, back-face fraction, nearest face; camera-independent; recomputed on grid move / mesh hash | This *is* RTXGI's fixed-ray stage, already written for the shared grid |
| Classification (active/inactive) | **No** - all 4096 probes every frame | **Partial** - inside/valid only; data zeroed | Adopt "inactive keeps data" (validity -1) in the cache now; the "nothing nearby" state is a later cost optimisation for both writers |
| Variability | No | No | Optional readout; the cache's diagnostic readback (`RC diagnostic`) already samples the SH buffer and could log a per-probe CoV |
| Depth moments atlas (writer) | **Yes** - atlas mode 1 of `ddgi_probe_update_cs.hlsl`, 16x16 + border, normalised moments, `w*w` weights | **No** | Reusable **as is** for the cache once its rays are in a buffer with the DDGI layout (`[probe*nRays+i] = (radiance, hitT)`): the pass reads only that buffer and `ProbeRayDirection(i, nRays, probe, frame)`, which both writers construct identically |
| Border fill | **Yes** - `ddgi_border_fill_cs.hlsl` | n/a | Reuse as is for the cache depth atlas |
| Chebyshev consumer | **Yes** - `deferred_lighting_ps.hlsl` 1108-1135 | **No** - guard half-space | Factor the block into a helper taking (atlas SRV, probe index, probe position, receiver) and call it from both lighting blocks; later from the seven hit-point consumers through `rc_probe_guard.hlsli` |
| Per-ray buffers | **Yes** - block B.6, `ddgi_probe_trace_cs.hlsl` | **No** - one thread per probe traces, shades, projects and blends | The cache's trace loop becomes a trace pass writing the same layout; SH projection + blend becomes a per-probe pass reading it |
| Surface bias, wrap-shading floor, Chebyshev floor, crush | **No** | **No** | Add to both lighting blocks (section 2.4/2.5 values, bias scaled to the spacing) |
| Shared constants / grid | **Yes** - `RadianceCacheTraceConstants` is the DDGI CB; same origin, spacing, 16^3 | - | Already shared; the two free slots the DDGI writer uses for the ray buffers (`shBufferUav`, `firstWrite`) conflict with the cache's own use, so the cache's depth pass binds its own constants allocation |

## 5. Designs

### Design A - shader-only, inside the cache

Change `radiance_cache_trace_cs.hlsl` and the cache block of `deferred_lighting_ps.hlsl` only:
hysteresis from update interval + noise-normalised detector + clamp; keep SH while inside
(validity -1) and blend on return; seed the entering slab from the inward neighbour; interleaved
update phases; wrap-shading floor and surface bias in the reader. No new resources, no new
passes, every consumer API unchanged.

Trade-offs: smallest diff, directly addresses A, C and the writer half of B; a week of shader
work with the existing diagnostics as gates. Does nothing for leaks (the guard half-space stays
the only visibility control) and leaves the DDGI writer on its flat blend. Failure mode: a
detector threshold that still fires on noise - caught by the KnownRadiance test.

### Design B - shared probe-management layer (recommended)

Design A, plus: (1) `gi_probe_blend.hlsli` shared by both writers; (2) the cache's trace split
into a trace pass that writes `(radiance, hitT)` per ray in the DDGI layout and an SH pass that
reads it, so the existing DDGI depth pass and border fill run unchanged on the cache's rays and
produce a cache depth atlas; (3) the Chebyshev block factored into a helper used by both lighting
blocks; (4) `probe_side_cs` output turned into a persistent RTXGI offset per probe, stored in the
guard record and consumed by both writers' probe positions and both lighting blocks.

Trade-offs: touches `ddgi_system.cpp` (sharing the depth dispatch), `radiance_cache.cpp` (a
second buffer, a second atlas of ~5 MB at 16^3 x 18^2 x RG16F, two extra dispatches), the lighting
shader and the guard include; the seven hit-point consumers keep working through the existing
API and gain visibility later. Higher coupling between the two GI paths - but they already share
the grid, the constants and the ray sequence by design, and the duplication of stabilisation
logic is exactly what caused both writers to ship the same flat blend. Failure modes: the cache's
SDF hits are 5 cm early plus the field's expansion, so its depth moments are biased short - the
surface bias absorbs that; `maxTraceDistance` differs between the two writers (15 vs. 32 spacings)
and the consumer hard-codes the DDGI definition, so the cache's depth pass must bind that
definition or the consumer must take it as a constant.

### Design C - make the cache an SDF backend of DDGISystem

Swap `TraceProbeRay` for `TraceGDF` inside the DDGI writers and drop the SH representation.
Rejected: `EvalSH_L2` of the cache's buffer is consumed by reflections, the surface cache, the RT
reflection pass and the gather at hit points (seven shaders); replacing the representation is a
different project, and SH blending is not the part that is broken.

### Recommendation and stages

Design B, staged so each stage has a measurement:

Stage 1 (Design A's content, shader-only). Values to start from: `h_f = 0.97` per frame,
`h = h_f^k` with `k` the probe's update interval (4 today); re-arm when `|dMu| > max(3 sigma,
0.05 max(mu))` -> `h -= 0.75`; clamp `delta *= 0.25` when `|dMu| > 1.0 max(mu)`; validity -1 for
inside-with-data; recentre seed at `h = 0.5` from the clamped history coordinate; phases
`(gx+gy+gz) mod 4`; reader `w = ((dot+1)/2)^2 + 0.2` and a query bias of `n * 0.1 s + (-v) * 0.1 s`
with `s` the probe spacing (RTXGI's defaults are 0.1 scene units on metre-scale scenes with
~1 m spacing, i.e. ~0.1 s), tuned down if a receiver on a thin wall is biased through it.
Also raise `RaysPerProbe` to 64 if the pass budget allows.

Stage 2 (visibility). Trace pass + SH pass for the cache, DDGI depth pass and border fill reused
on the cache's ray buffer, Chebyshev helper in both lighting blocks with the 0.05 floor and the
1.5-spacing clamp added to the writer; the guard half-space stays for the hit-point consumers
until they get the helper.

Stage 3 (relocation). RTXGI offset pass from `probe_side_cs` (back-face > 0.25 -> push out
through the closest back face; front face closer than `minFrontface` ~ 0.1 s -> move away along the
farthest front face; else relax; cap 0.45 s), offsets in the guard record, consumed by both
writers and both lighting blocks; `ProbeClassification` default moves from 0 to the fixed-ray
rule once Stage 3 is measured.

Verification per stage, with tooling that exists: `scripts/rendering/run_probe_support_motion.py`
(per-frame diffuse step p99/max and `rc_support` correlation on the Sponza S3 sweep; run with the
gather off so the cache is isolated - that configuration showed `lit_floor` max step 0.083);
`r.GI.Diagnostic.KnownRadiance` (the detector must never fire; count fires in a diagnostic
record); a 300-frame static-camera run with `r.GI.RadianceCache.Diagnostic` reading back the SH
buffer for the per-probe temporal CV of the DC term (expected >= 2x lower after Stage 1); the
existing DDGI-vs-cache transport pairing for Stage 2's depth atlas sanity.

Risks: (1) the re-arm threshold - too tight brings the crawl back, too loose makes light switches
take ~1 s; the 3-sigma rule plus the KnownRadiance gate is the control. (2) Keeping stale SH for
inside probes means a probe that returns after a long time fades from old lighting over the time
constant unless the detector fires; acceptable against a noisy replace. (3) Stage 2 adds a pass
and ~5 MB; measure the cache's trace pass first - the split into trace + SH passes should not cost
more than today's single pass, the ray writes are 4096 x 32 float4. (4) Stage 3 changes the
effective positions seen by seven consumers; the API already exists, but the sdf/surface-cache
readers that pass `gridPosition` into `ProbeGuardRejects` must be checked for the `w < 0`
branch when offsets become common rather than rare.

## 6. References

- Majercik, Guertin, Nowrouzezahrai, McGuire. "Dynamic Diffuse Global Illumination with
  Ray-Traced Irradiance Fields." JCGT 8(2), 2019. https://jcgt.org/published/0008/02/01/
- Majercik, Mueller, Marrs, Moreau, Dupuy, Nowrouzezahrai, McGuire. "Scaling Probe-Based
  Real-Time Dynamic Global Illumination for Production." JCGT 10(2), 2021.
  https://jcgt.org/published/0010/02/01/ (arXiv: https://arxiv.org/abs/2009.10796)
- NVIDIA RTXGI-DDGI SDK. https://github.com/NVIDIAGameWorks/RTXGI-DDGI - `ProbeBlendingCS.hlsl`,
  `ProbeRelocationCS.hlsl`, `ProbeClassificationCS.hlsl`, `Irradiance.hlsl`,
  `include/ProbeRayCommon.hlsl`, `include/rtxgi/ddgi/DDGIVolume.h` (defaults quoted in 2.5);
  volume documentation `docs/DDGIVolume.md`.
- UzEngine: `runtime/engine/renderer/gi/radiance_cache.{h,cpp}`,
  `runtime/engine/renderer/gi/ddgi_system.{h,cpp}`,
  `runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl`,
  `runtime/shaders/runtime/gi/rc_probe_guard.hlsli`,
  `runtime/shaders/runtime/gi/probe_side_cs.hlsl`, `runtime/shaders/runtime/gi/probe_reach_cs.hlsl`,
  `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl`,
  `runtime/shaders/runtime/gi/ddgi_probe_trace_cs.hlsl`,
  `runtime/shaders/runtime/gi/ddgi_probe_common.hlsli`,
  `runtime/shaders/runtime/gi/ddgi_border_fill_cs.hlsl`,
  `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` (DDGI block 1036-1160, cache block
  1165-1250), `docs/analysis/rc-probe-support-followups/07-support-motion.md`.
