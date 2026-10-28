# Unified real-time denoiser

Status: decision-complete design, not an implementation or measured performance claim.
Survey date: 2026-10-26.
Baseline: `feat/soft-shadows-area-lights`, `da33b8a642a0721822cf2c655eb8f9fb9697944f`.
Scope: final Lumen-parity Phase E real-time signal reconstruction.

## 1. Decision

Build an engine-native, NRD-style reconstruction backend with shared receiver guides,
history validation, neighborhood statistics, and spatial filtering, but separate
signal histories and policies. Do not add an NVIDIA SDK dependency, route real-time
signals through the PT composite, or combine diffuse, reflection, and shadow values
into one history. The committed native PT denoiser provides an implementation
precedent for the mechanics; its input and output contracts are not compatible with
the real-time buffers.

The first provable increment is **standalone RT reflection reconstruction**, behind
new default-off switches and the existing RT denoise switch. Replace its optional
SSR temporal/spatial tail with the new backend, initially using conservative surface
reprojection. Correct the motion-vector units, add previous receiver geometry and
actual roughness validation, and eliminate approximate virtual-hit reprojection on
this route. Keep trace sampling, hit shading, cache behavior, and composition intact.
This is a quality increment and the first backend client, not a claim that multiple
signals have already been consolidated.

Next adopt diffuse final gather and share guides between both clients. Subsequently
add actual-hit specular tracking and the mixed SSR/RT route. Add area shadows last:
their visibility currently exists only inside deferred lighting, so their extraction
is materially larger than replacing a texture-to-texture denoiser.

Keep ReSTIR reservoir reuse, world-space surface-cache accumulation, SH/DDGI probe
history, and final TAA in their existing domains. Reconstruct the resolved ReSTIR
signal with the common diffuse backend only in its own later opt-in stage.

These choices follow the in-repository native PT filtering precedent and the existing
standalone RT route. They require no unresolved package/API ownership decision.
Adopting the actual NRD SDK instead is a separate architecture proposal, not an
implicit implementation option.

## 2. Evidence and current signal paths

All source paths below are repository-relative. Counts describe enabled dispatches
in the inspected code, not measured GPU time or a guarantee that every feature runs
in every frame.

### 2.1 Survey table

| Signal / owner | Spatial reconstruction | Temporal reconstruction and clamp | Rejection / availability | Guides actually used | Destination and cost |
|---|---|---|---|---|---|
| Legacy screen SSGI and SDF world final gather; `runtime/engine/renderer/gi/ssgi_renderer.cpp`, `SSGIRenderer::Render` | `runtime/shaders/runtime/ssgi/ssgi_spatial_denoise_cs.hlsl::main`: half-resolution 5x5, `exp(-r2/8)`, symmetric receiver-plane distance and normal-power weight; no color edge stop. `ssgi_upsample_cs.hlsl::main`: four bilateral taps at full resolution, world-gather fallback ray work when no compatible tap exists. | `ssgi_temporal_accum_cs.hlsl::ReprojectGatherHistory`: previous VP, four validated bilinear history taps, RGB + AO; history count capped at 64; current weight `max(1/count, r.SSGI.TemporalAlpha)` (default .05). **No color clamp**: `varianceClipGammaBits` is a reserved ABI slot. Spatial precedes temporal. | Sky, invalid history, behind-camera/offscreen, incompatible geometry/zero weight, and current/previous GDF change boxes. `ssgi_surface.hlsli::SurfaceCompatible`: normal dot >= .9, symmetric plane distance <= `max(1 cm, .001 * camera distance)`. `r.GI.ValidSampleReject` defaults false; when enabled for world gather, alpha distinguishes unavailable samples from valid black. | Current depth and octahedral normal; previous world position + packed normal. Velocity is bound but not read by this temporal shader; no roughness gate. | Full-resolution E/pi and AO/miss fraction to deferred lighting. Three reconstruction dispatches: half spatial + half temporal + full upsample. Legacy screen mode additionally reduces previous HDR before trace and copies HDR after lighting. |
| Screen-indexed real-time GI-ReSTIR; same C++ owner | `ssgi_restir_spatial_cs.hlsl`: up to four disk neighbors, pairwise MIS, two replayed rays per accepted neighbor. Default neighbor count is zero, but the spatial/resolve dispatch still runs. It writes the ordinary half-resolution trace targets. | `ssgi_restir_temporal_cs.hlsl`: nearest reprojected reservoir, replayed targets at both domains, generalized balance MIS, previous M capped by candidates times `r.GI.ReSTIR.MaxHistory` (20 frames by default). No radiance neighborhood clamp on reservoir contents. | Stored receiver position, shading/geometric normal; temporal plane scale .001, spatial .01, normal dot >= .9; changed-region rejection. Canonical null candidates retain M. Unsupported receiver output has alpha 1 in the inspected spatial resolve; it does **not** automatically inherit the plain trace's new availability behavior. | Depth/normal for initial surfaces; stored reservoir surfaces and previous VP for reuse. No velocity or roughness. | Initial + temporal + spatial/resolve, **then all three ordinary SSGI reconstruction dispatches**. `Render` does not skip spatial denoise or radiance temporal accumulation when ReSTIR is on. World-mode gather rays, not legacy screen-depth trace rays; "screen-space" describes reservoir indexing. |
| SSR and optionally RT reflections; `frame_gi_passes.cpp::RegisterSSRPasses`, `RegisterSSRDenoisePasses`, `RegisterRTReflectionPass` | `runtime/shaders/runtime/postprocess/ssr_resolve_cs.hlsl`: center + 15 Halton taps in a roughness-adaptive ellipse, normal exponent 512, raw-depth weight, Karis HDR weighting. `ssr_spatial_denoise_cs.hlsl`: center + eight integer-offset taps, radius driven by actual GBuffer roughness, normal exponent 512, raw-depth weight. | `ssr_temporal_cs.hlsl`: bilinear history, 3x3 YCoCg mean +/- 1.25 sigma including alpha; adaptive alpha uses **current reflection alpha as a roughness proxy**, not material roughness. Hit-distance path projects `surfacePosition + V * hitDist` and blends that approximate virtual UV with surface UV. | No previous depth/normal guide validation. Current alpha <= .001 clears output; first frame and offscreen discard history. No per-tap geometry rejection. | Temporal reads depth, velocity, distance, previous VP; **no normal SRV**. Spatial/resolve read depth and current oct-normal/roughness. | Resolved radiance RGB + blend alpha reaches deferred lighting before environment BRDF. SSR normally has three reconstruction passes: resolve + temporal + spatial. RT denoise adds no second set: when enabled, RT writes after SSR resolve and before this tail. Standalone RT uses only temporal + spatial. |
| RT default route; `reflections/rt_reflection_cs.hlsl` | None dedicated over the RT replacement texels. | `r.RayTracing.Reflections.Denoise` defaults false; RT runs **after** SSR denoise and overwrites eligible holes, or owns the buffer with SSR off. Final TAA then accumulates the lit result. | Producer rejects ineligible surfaces/unusable hits; sky rays have valid radiance and hit distance zero. These are not denoiser disocclusion tests. | Trace has depth/normal/roughness, actual sampled direction R, actual hit point and t; only t is optionally exported for existing denoise. | Single RT trace dispatch; no dedicated denoise by default. Glossy sampling defaults false, indirect at the hit defaults true in C++ registration. |
| Area-light soft shadows; `runtime/shaders/runtime/lighting/area_light_shadows.hlsli::AreaShadowVisibility` | No screen-space filter. Sixteen Hammersley source samples, per-pixel IGN and per-frame R2 toroidal shift. | No dedicated history. Existing TAA integrates final lit color. Returns two bounded self-normalized ratios, diffuse and LTC-specular weighted visibility, not one unweighted visibility. | TLAS sentinel returns fully visible. Per-ray surface bias and emitter termination offset; no shadow-specific temporal rejection/clamp. | Receiver geometry, shading normal/view, emitter shape, LTC inverse matrix and TLAS in deferred PS; no shadow motion/history guide input. | Applied inside rect/shaped-light evaluation in `deferred_lighting_ps.hlsl` before final HDR. No extra denoise dispatch; up to 16 visibility rays per evaluated source/pixel (zero-weight samples skip rays). |
| Surface radiance cache; `gi/surface_cache_resolve_cs.hlsl` and `surface_cache_contract.hlsli::SurfaceCacheEma` | Hash-cell aggregation, not an image-space bilateral filter. Fixed-point contributions form a per-frame mean. | Running mean transitioning to EMA: `mean += (frameMean-mean)/frames`; default accumulation horizon 32. No screen reprojection or image-history clamp. Surface-record relight/indirect ownership can bypass folding a layer in resolve. | Changed-box cell eviction, lighting/geometry hash invalidation in `frame_gi_passes.cpp`, stale-cell eviction (default 64 frames), sample-count readability. | World cell/level key, hit normals through query/key contract, sample metadata, region boxes. No screen depth/roughness/motion. | Resolve before GI/reflection consumers; cells replace hit shading and feed later consumers. One table dispatch, plus separately enabled relight/indirect work. Must not be counted as a screen temporal pass. |
| Final TAA; `runtime/shaders/runtime/postprocess/taa_resolve_cs.hlsl`, `frame_postprocess.cpp::RegisterPostProcessPasses` | Jitter-aware HDR-weighted plus reconstruction; 3x3 neighborhood statistics. | Dilated receiver velocity, five-tap Catmull-Rom history; YCoCg variance box intersected with neighborhood AABB; default current weight .04, motion weighting and anti-flicker adjustment. Additional diagnostic/opt-in modes exist. | Offscreen history handling and reset integration; no previous receiver depth/normal test inside this resolve. Closest-depth selection chooses velocity; it is not geometric history validation. | Current depth and pixel velocity, current HDR, previous TAA output; no normal/roughness guide. | One final temporal resolve when TAA is active. Display-resolution output may differ from render resolution. |

The PT `nrd_*` shaders are under `runtime/shaders/runtime/pathtracing/`, not
`runtime/shaders/runtime/reflections/`. The actual real-time reflection filtering
shaders are under `postprocess/`; `reflections/` owns RT production and its contract.
Some old comments still describe SSR as post-TAA or RT as default-off. Executed
branches and CVar registration above, not those comments, establish current behavior.

### 2.2 End-to-end registration and composition

`runtime/engine/renderer/frame/frame_lighting.cpp::RegisterLightingPasses` registers
GDF/probe work, surface-cache resolve, SSGI, Brixelizer GI, SSR, and the after-chain RT
call before deferred lighting. `RegisterSSRPasses` also calls RT immediately after
`SSR_Resolve`; `RegisterRTReflectionPass` selects exactly one location using the
denoise CVar and `SelectRTReflectionRoute`. Standalone rays are selected when SSR is
off, not while SSR is merely waiting for its first previous-color frame.

`frame_gi_passes.cpp::RegisterSSGIPasses` passes current depth, normal, velocity,
frame constants, changed regions and optional surface-cache parameters to SSGI.
`SSGIRenderer::Render` dispatches trace or ReSTIR, spatial, temporal, upsample,
then advances its history. It resets history on explicit reset, frame gaps and scene
revision, and carries previous matrices/regions. These behaviors must survive adoption.

`runtime/engine/renderer/frame/frame_constants.cpp` publishes
`perFrame.ssrSrvIndex = m_ssrResolvedOutputSrv` for a usable SSR chain or standalone
RT route. The same file packs the valid-sample flag into bit 31 of
`sdfSampleRotation`; default registration is `r.GI.ValidSampleReject=false`.

`runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` consumes:

- World gather as `cosineIrradiance` from `ssgiSrvIndex` before diffuse material
  modulation, replacing the fallback irradiance for that path.
- Legacy screen gather as `ssgiColor + ssgiMiss * diffuseIrradiance`; the "AO"
  texture here is uncovered-hemisphere fraction, not an extra ambient shadow factor.
- SSR/RT as `radiance = lerp(radiance, ssrHit.rgb, saturate(ssrHit.a))` before
  environment BRDF/Fresnel and multi-scattering terms.
- Area visibility separately on diffuse and specular LTC contributions.

`frame_lighting_pass.cpp::RegisterDeferredLightingPass` selects the area-shadow
permutation and supplies the TLAS through existing `LightingCB`, and binds the
GBuffer/lighting inputs. `frame_postprocess.cpp` runs the upscaler/TAA before
bloom/tonemap. Its legacy additive `r.SSR.Composite` defaults to zero; do not enable
it for this project. SSR previous color is copied after TAA; legacy SSGI previous
color is copied after lighting/atmosphere before postprocess. Denoising does not
remove those producer feedback dependencies.

### 2.3 Redundancy, gaps and inconsistencies

**Repeated mechanics:** SSGI, reflection temporal, PT NRD-style temporal and TAA
each implement history coordinates, bounds handling and temporal weights. SSGI
spatial/upsample, SSR resolve/spatial and PT a-trous decode normals and evaluate
geometry edges repeatedly. ReSTIR adds a second temporal system before SSGI's
radiance temporal accumulation; the surface cache can already have averaged the
same incoming radiance. Their statistical roles are different, so deleting every
"duplicate temporal" operation would be wrong.

**Concrete quality gaps:**

- Area-shadow noise has only whole-image TAA; no light identity, penumbra signal,
  or dedicated history is exported.
- Default RT replacement texels bypass dedicated reconstruction.
- SSGI availability rejection is opt-in; unavailable black can contribute weight
  when off. Valid black must remain a sample. ReSTIR unsupported receivers
  currently resolve to alpha 1, a separate adapter issue.
- SSGI geometric history rejection does not detect illumination-only changes
  outside its invalidation inputs and does not track object motion with velocity.
- Reflection temporal has no previous receiver geometry validation. More critically,
  it subtracts raw velocity directly from UV and uses its magnitude against a UV
  threshold. `runtime/shaders/runtime/common/velocity_common.hlsli` defines the
  actual contract as **current minus previous, unjittered current-render pixels**;
  `velocity_ps.hlsl` writes that contract and TAA divides by render dimensions.
  This mismatch is visible in the inspected expressions; its image impact is not
  measured by this survey.
- Reflection virtual motion uses view direction, not the sampled reflection ray.
  Temporal alpha conflates reflection blend confidence with material roughness.
- `ssr_spatial_denoise_cs.hlsl` clamps roughness to >= .04 before testing `< .04`;
  its stated perfect-mirror early-out is unreachable in that implementation.

**Different rejection/clamp rules:** SSGI uses world-plane compatibility and no
color clamp; ReSTIR uses nearest-tap receiver-plane gates and domain replay;
reflections use no prior-geometry gate and 1.25-sigma YCoCg clamp; TAA uses a
different YCoCg scaling and sigma/AABB intersection. Surface cache uses world-key
invalidation, not disocclusion. Sharing one numerical threshold indiscriminately
would erase meaningful distinctions.

**Pass accounting:** with plain world gather, SSR and TAA active, reconstruction
is three gather passes + three reflection passes + one TAA = seven image
dispatches, of which three are image temporal integrations. ReSTIR replaces the
one gather trace with three reservoir passes: +2 dispatches relative to plain
gather, including another temporal reuse and a resolve even with zero spatial
neighbors. Standalone denoised RT replaces the three-pass SSR reconstruction
with its two-pass tail; standalone raw RT has none. Trace, classify, prepare,
color reduction/copies, probe/cache work and upsample fallback rays are additional
costs, not hidden "free denoising." No global GPU-time saving is asserted.

### 2.4 Existing PT denoiser and adjacent owners

`runtime/engine/renderer/passes/pathtracing/nrd_denoiser.cpp::NrdDenoiser::Denoise`
owns diffuse/specular histories and guide ping-pong, with one reproject and three
a-trous dispatches per channel (four or eight dispatches). Its own DX12 root
signature, RG imports and native shader compilation are not a wrapper around
NVIDIA's library.

`nrd_reproject_cs.hlsl` already separates specular history coordinates from surface
validation, uses depth/normal/roughness rejection, optional mean/sigma clamp,
motion-shortened history and disocclusion reconstruction.
`nrd_atrous_cs.hlsl` uses a 5x5 B3 kernel at increasing strides and
roughness-dependent specular radius. But `nrd_common.hlsli::NrdCurrent` divides PT
diffuse by albedo and PT specular by modulation; stage 2 reads actual PT-primary
guides. Its final pass recombines PT raw/diffuse/specular/residual. None of those
buffers or that residual contract describes the real-time E/pi or pre-BRDF
reflection buffer. Reuse algorithm structure, not this composite.

Other real-time temporal owners are outside this item's migration:
`lighting/env_mis_temporal_cs.hlsl` accumulates environment-MIS specular at the same
pixel with no reprojection, conditionally registered in `frame_lighting.cpp`;
`gi/radiance_cache_trace_cs.hlsl` blends SH probe history and
`gi/ddgi_probe_update_cs.hlsl` blends irradiance/distance moments. They should be
included in whole-frame lag/performance diagnostics, not silently rewritten here.

## 3. NVIDIA pattern and engine adaptation

Primary reference reviewed: NVIDIA-RTX/NRD official `README.md`, overview,
non-noisy inputs, noisy inputs and integration sections:
`https://github.com/NVIDIA-RTX/NRD/blob/master/README.md`.

NRD groups diffuse/specular reconstruction around common normal, roughness, view
depth and motion guides. REBLUR uses recurrent blur; RELAX uses a-trous filtering;
SIGMA is per-light shadow reconstruction. Its integration requires explicit motion
conventions, signal packing and material-factor handling. This design borrows that
separation, not SDK compatibility, published performance numbers or all NRD features.

Use a RELAX-like temporal + short-history clamp + edge-aware a-trous structure as
the engine-native backend; it has direct precedent in the committed PT shaders.
Use REBLUR's signal-aware history/radius principle rather than porting a second
complete family. Use SIGMA's per-light ownership principle for future shadow
clients. Shadow kernels remain specialized: bounded visibility is not radiance.

### 3.1 Signal contracts

Each registered client declares signal class, dimensions/lattice, raw input,
availability, composition weight, history epoch, and optional hit metadata.
Every history belongs to one class, resolution and producer route.

| Client | Filtered quantity | Metadata kept separate | Composition boundary |
|---|---|---|---|
| Diffuse | Linear E/pi; no second albedo demodulation | Availability; history count/moments; companion miss fraction for legacy screen mode | Existing full-resolution gather result and AO/miss texture |
| Specular reflection | Linear incoming radiance before deferred environment BRDF | Producer availability, SSR/RT source class, blend confidence, actual roughness, optional hit kind/position | Existing resolved RGB + blend alpha, before deferred BRDF |
| Shadow | Per-light diffuse/specular weighted visibility pair, each in [0,1] | Stable light identity/revision, receiver guides, emitter revision, blocker/penumbra metadata when available | Multiply each corresponding unshadowed LTC lobe exactly once |

Do not put history count in the externally visible reflection alpha, reinterpret
miss fraction as shadow visibility, or treat a valid black ray as missing.
Reflection eligibility/weight is set by the current producer; reconstruction
must not resurrect an ineligible pixel from old alpha. A sky reflection is valid
with zero hit distance. A failed trace with no usable source is not a sky sample.

### 3.2 Common guides, coordinates and disocclusion

The guide interface reads current reversed-Z depth, oct-normal and roughness from
the existing GBuffer, plus the existing pixel-space velocity. Reconstruct receiver
position with the engine's matrix convention; retain positive linear view depth
for diagnostics/threshold scaling. Sky is an explicit state, not a tiny-depth
surface.

For surface history on an unjittered lattice, use
`previousUV = currentUV - velocityPixels / renderSize`. For jittered guide histories,
define J as the UV projection offset of the jittered matrix relative to the
unjittered matrix (including D3D Y sign), and use
`previousUV = currentUV - velocityPixels / renderSize + Jprevious - Jcurrent`.
Do not infer the sign from a pixel-jitter field's name. Test a stationary plane
through a complete jitter cycle. Do not add jitter twice or reuse SSGI's camera-only previous-VP
coordinate for moving receivers. The half-resolution gather lattice stays
`fullPixel=2*halfPixel`, not the center of an averaged 2x2 block; preserve odd-size
mapping from `SurfacePixel` and the current upsample.

Use four bilinear history taps. Reject each tap independently for offscreen,
sky mismatch, invalid epoch, receiver-plane/depth separation and normal mismatch;
renormalize surviving weights. Apply roughness compatibility for specular and
shadow specular lanes, not as an unnecessary diffuse material edge.
Initial static-plane gate: existing `SurfaceCompatible` plane tolerance and .9
normal cosine; initial specular roughness difference < .1 follows PT precedent.

A 2D motion vector does not supply previous world position/depth of a deforming or
depth-moving object. Do not claim world-position equality on dynamic objects.
Until previous-depth/object transport metadata exists, conservatively reject a
tap that cannot pass receiver compatibility and use current-frame reconstruction.
This may reduce reuse; it is preferable to inventing absent guides.

Per-signal invalidation augments shared geometry rejection: changed GDF regions
for diffuse, producer/source changes for reflections, light/emitter revisions
for shadow. Camera cuts, resize, skipped frames and route/quality-mode switches
start fresh histories. Light changes reset affected screen histories initially;
regional lighting invalidation is not required for this increment.

Stage 1 reads current GBuffer guides directly and writes only its guide history.
Once two clients exist, use one full-render-resolution guide preparation and
previous-guide pair; half-resolution clients sample the declared full-resolution
lattice, not a separately averaged guide. Geometry decoding/position reconstruction
can be shared; history accept masks and specular virtual coordinates cannot be
globally identical because their policies differ.

### 3.3 Temporal, clamp and spatial policies

Store **pre-spatial temporal** radiance as long history, plus luminance first/second
moments and history count. Maintain a short current-responsive history for the
clamp (maximum four accepted frames). Spatial output is for this frame's consumer,
not a recursively blurred history.

For a valid current sample and accepted history:
`count=min(oldCount+1,cap)`, `alpha=max(1/count,minCurrentWeight)`.
For missing current input, reuse only independently validated available history;
do not increment count. With neither available, emit unavailable, not valid black.
Sky/eligibility transitions cannot inherit surface histories.

Compute 3x3 current luminance statistics only from available,
geometry-compatible samples. Update fast luminance with weight
`1/min(fastCount+1,4)`, initialized from that neighborhood mean, and clamp long
history luminance to fast luminance +/- k times current neighborhood sigma.
Use k=3 while static and .5 for motion above one render pixel, taking the
motion-dependent envelope idea from the existing PT policy. Scale nonzero history
RGB by clamped/original luminance to retain chroma; zero-luminance history stays
zero until the current-sample blend. This is a native scalar clamp adaptation,
not a byte copy of PT's componentwise clamp. Preserve constant fields exactly;
record clamp distance and rejected-tap counts. With no available current taps,
skip the clamp rather than collapsing history to a false zero-variance envelope.

Initial policies are implementation starting values with the acceptance gates
below, not a claim of published NRD defaults:

| Policy | Diffuse | Reflection | Shadow |
|---|---|---|---|
| Long-history cap | 32 accepted frames; min current .05 | `4 + 12*r*r`; mirror `r<=.04` uses current sample in stage 1 | 8 per-light frames |
| Motion response | Cap 2 frames when motion >1 render pixel | Cap 2; no stage-1 virtual-coordinate reuse | Cap 2 plus light-motion reset |
| Spatial | 5x5 B3, strides 1 and 2; receiver-plane/normal stops | One 5x5 B3, stride 1, roughness-scaled radius; bypass mirror | Specialized 3x3 bounded pair filter, no cross-light taps |
| Disoccluded pixel | Dense radius-2 current compatible taps | Radius 1 for r<.15, otherwise 2; never borrow ineligible/source-incompatible texels | Current compatible taps within the same light/lobe |

The ordinary spatial kernel multiplies normalized B3 weights, symmetric
receiver-plane falloff and normal similarity. Specular adds roughness and
hit-kind/distance gates when those fields exist. Avoid a noisy radiance edge stop
that freezes isolated Monte Carlo bright samples; do not transplant SSR's Karis
normalization into diffuse E/pi. Radius and history limits are class parameters,
not separate copies of the algorithm.

Stage-1 reflection alpha is current producer weight, not an accumulated count or
roughness proxy. RGB temporal/spatial filtering excludes ineligible taps; preserve
current alpha at the output. Later SSR coverage reconstruction gets its own bounded
coverage lane and tests rather than silently blurring confidence as radiance.

### 3.4 Specular hit motion

Stage 1 deliberately uses conservative surface tracking. It cannot track a moving
reflected object with no hit motion; short history and clamp bound that limitation.

The hit-aware stage exports the **actual sampled** RT hit point (including the
biased ray origin), hit kind, and distance, and actual SSR hit UV/world point.
`rt_reflection_cs.hlsl::main` already computes `R` and `hitP`; exporting only t and
reconstructing a mirror ray is wrong when Glossy is on.

Follow the PT separation: validate the receiving surface at its surface-motion
coordinate, then select reflection history using a virtual-coordinate delta
blended toward surface motion by roughness squared. Reject invalid/behind-camera
hit projections and hit-kind discontinuity. Store prior hit metadata with the
specular history and reject incompatible hit distance/position; do not validate
receiver depth at the reflected object's UV.

Without reflected-object motion, an actual hit position is still not its previous
position. Keep conservative clamp/response for changing reflected geometry; a later
object-motion extension must not be implied by this stage's acceptance.

### 3.5 C++ ownership, graph and root budget

Proposed owner: `RealtimeDenoiser` under `runtime/engine/renderer/passes/`, with
common HLSL under `runtime/shaders/runtime/denoising/`. These are proposed paths,
not existing files. Own persistent guide and per-signal histories; use RG transient
scratch and return signal resources/SRVs to existing consumers. Producers remain
owned by their current subsystems.

Declare every actual texture/buffer read/write in the render graph, including
producer output, histories, hit metadata, guide preparation and lighting reads.
Import persistent histories as tracked resources. Do not reproduce the existing
SSR tail's dependency on common GBuffer read registration to order untracked
signal writes; `RegisterSSRDenoisePasses` documents an actual stale-buffer failure
from missing dependencies. Publish only an output successfully written this frame,
following `HasProducedResult` / `test_gi_resource_validity.cpp`.

Use the existing bindless compute root layout: b0 indices/small controls, b1 frame
CBV, b2 denoiser configuration/matrices/region data from the CB ring. Keep b0 at
<=24 DWORDs with a C++ size assertion. `runtime/engine/rhi/dx12/dx12_pso_creation.cpp`
allocates 24 b0 DWORDs and accounts for 33/64 DWORDs overall; SSGI temporal uses
15 and RT reflection uses 17 today. Do not grow global root/PerFrame layouts or
copy the PT private root signature for this feature. Put growing parameter tables
in b2 or a bound structured descriptor table record.

Use bindless capability gates consistently with existing SSGI/RT routes. No-DXR
systems may use the diffuse client but not RT/shadow clients. Off/capability-missing
paths must not allocate guides/histories, compile new runtime permutations, dispatch
new work, advance legacy seeds, or change published resource indices.

Initial history storage: RGB+count in RGBA16F and
`(luminanceMoment1,luminanceMoment2,fastLuminance,fastCount)` in RGBA16F,
two of each per active signal (32 bytes/pixel); shared previous-guide ping-pong in
RGBA32F world-position/packed-normal (32 bytes/full pixel); roughness/linear-depth
guide ping-pong in RG16F (8 bytes/full pixel), with sky sentinel in position.w.
Stage 1 allocates guides only for itself; stage 2 shares them. This is an explicit
quality-first budget, not a memory saving over legacy. Count is not inferred from
half-float radiance. Inspect and quantify fp16 HDR range and memory before shipping;
if fixture radiance exceeds fp16 range, use RGBA32F signal histories rather than
clipping valid energy. Actual hit metadata and shadow-layer memory are additional.

Do not fuse arbitrary clients into one giant dispatch. Share guide preparation,
helper source and policy layout first; differing half/full resolutions and shadow
light layers make forced fusion wasteful. Fuse equal-resolution lanes only after
GPU evidence shows a benefit, with identical signal math.

## 4. Staged delivery and acceptance

### 4.1 Branch discipline and controls

Continue on `feat/soft-shadows-area-lights` from the recorded baseline. Each stage
is a separately reviewable verified increment; do not promote defaults or combine
unrelated renderer fixes. Commit only with the owning user's authorization.
All proposed switches are registered at file scope before CLI parsing:

- `r.Denoiser.Realtime=0`: master; zero executes the exact legacy path.
- `r.Denoiser.Realtime.Specular=0`: specular client.
- `r.Denoiser.Realtime.Diffuse=0`: diffuse client.
- `r.Denoiser.Realtime.Restir=0`: permit the resolved ReSTIR diffuse client.
- `r.Denoiser.Realtime.HitTracking=0`: actual-hit/mixed reflection route.
- `r.Denoiser.Realtime.Shadows=0`: per-light area visibility client.

Client switches have no effect with master zero. Stage-1 entry additionally
requires `RTReflectionsStandalone()` and `r.RayTracing.Reflections.Denoise=1`;
otherwise preserve current routing, including mixed SSR/RT. No per-signal switch
silently enables another. Leaving all defaults untouched must be byte-identical.
Explicit master=0 with every other combination must also be byte-identical to
the baseline running the **same existing** CVars, including existing denoise on.

For every stage, capture fresh `da33b8a6` and candidate builds at identical scene,
camera, frame numbers, resolution, jitter/sample sequence, exposure, feature flags
and adapter. OFF acceptance: decoded image `max_code_delta=0`; compare
pre-TAA HDR/region data as well, and require no new passes/resources. Comparing the
candidate against its own master-off output alone is insufficient. Preserve the
already-dirty shared workspace; use isolated evidence/build locations, not
destructive Git operations.

### 4.2 Shared measurement protocol

No builds, engine runs, captures or GPU measurements were performed for this
document. The following are future implementation gates.

Use the now-fixed `--path-trace` output, no `--nrd`, as the converged reference,
with the same camera and emitter geometry/energy. Record backend identity,
matrix/FOV, exposure, bounce count, sample count and relevant CVars in the
manifest. First verify opaque silhouettes align at the same pixel coordinates.
Do **not** transplant the old half-angle FOV workaround in
`scripts/rendering/area-light-shadows/verify.py`; that inspected harness still
contains compensation for an earlier PT projection mismatch. Update stale
reference fixtures to the fixed camera contract before accepting their metrics.

Increase reference samples until doubling changes fixed-ROI mean by <1% and
reference RMSE is <= one fifth of the candidate's error. Snapshot PT reference at
fixed poses after a camera/light/geometry change, not a moving progressive PT
history. The denoiser cannot fix differences in SDF geometry, cache lighting or
finite-bounce transport; report that bias separately from reconstruction error.

Compare raw, current per-signal denoise, and proposed backend with identical ray
budgets and seeds, TAA off and on. Fix ROIs from geometry/fixture definitions before
looking at candidate error. Record linear HDR MSE/RMSE and mean bias, temporal
standard deviation on static pixels and motion-compensated residual on moving
surfaces, disocclusion error over time, trailing-edge ghost error, highlight peak/
p99 and edge spread. Include first valid frame and warm-up, not only convergence.
Use PNG metrics for byte identity/display quality only; use stamped region dumps
for energy and temporal claims.

Run at 640x360 for bounded quality diagnostics and 1920x1080 and 2560x1440 for
bandwidth/timing. Use existing `flicker/runner.py` capture/timing manifests, fixed
frame triggers and bounded process deadlines, not sleeps. Report warm-frame
median and p95 GPU time, total frame time, dispatch count and persistent/scratch
bytes. Compare repeated serial captures on the same idle adapter; require timing
confidence intervals to resolve any claimed saving.

All stages require finite outputs, correct resets/capability fallbacks, unchanged
current eligibility/composition, clean shader compilation and validation, and
OFF identity. ON must improve the named target below with no fixed-ROI HDR RMSE
regression >2%, mean bias increase >2 percentage points, or display PSNR loss
>0.2 dB against the current denoiser on the rest of the agreed matrix. On fixtures
whose baseline already reaches 35 dB against PT, preserve >=35 dB. Existing
transport bias below that threshold is not a license to hide denoiser regressions.
Numbers below are proposed release gates, not measurements.

### 4.3 Stage 1: standalone RT quality increment

Implement only the native common helpers, specular history lifecycle, temporal
kernel and one spatial kernel needed for standalone rays. Read the current
producer's RGB/weight unchanged. Replace the optional SSR tail only on the explicit
entry route from section 4.1. Do not export new hit fields, change glossy sampling,
enable cache stages, or touch PT/TAA math. All actual temporal velocity conversions
on the new route use render-pixel units correctly.

Use actual GBuffer roughness; validate previous receiver position/normal/
roughness per tap, short-history clamp and conservative surface motion.
Mirror pixels use current samples and bypass blur; rough pixels accumulate.
Two reconstruction dispatches replace two, plus necessary guide writes folded
into the temporal pass. No standalone guide-preparation dispatch yet.

Acceptance:

1. OFF identity versus `da33b8a6` at frames 0, 1, 4, 24, 32, 95 and 240 in
   `reflection-reference.uzproject`, existing glossy/standalone fixtures and a
   no-DXR run; include legacy RT denoise both 0 and 1, SSR on/off, and master-off
   with client switches set. Exact code delta zero in every case.
2. Reuse the scene/ROI conventions in
   `scripts/rendering/test_rt_reflections_stage2.py` and the standalone route
   exercised by `test_rt_reflections_stage3.py`. Correct any stale camera setup
   before PT comparison. Add a translated camera, moving reflected object,
   receiver-depth step and roughness boundary; retain frozen static seeds for
   controls and ordinary rotating seeds for temporal measurements.
3. At equal one-ray glossy budget and matched warm-up, static HDR RMSE no worse
   than current standalone SSR-denoised RT by >2%, and at least 20% lower
   disocclusion/trailing-edge HDR MSE over the first eight post-change frames.
   Static temporal standard deviation must not increase. Reflected-object
   motion must not increase eight-frame ghost error; reject/reduce history
   rather than claiming unavailable hit motion.
4. Mean reflected energy within 5% of the matched PT ROI where producer transport
   matches, p99 highlight >=90% of reference, and edge width no worse by more than
   one pixel. Valid sky remains visible; ineligible pixels keep zero alpha;
   constant nonzero and valid-black signals remain constant.
5. GPU denoise median/p95 <=1.10 times current standalone two-pass denoise;
   whole-frame p95 increase <=5%. Log memory explicitly. This stage is a quality
   win, not a pass-count/performance-saving claim.
6. Extend relevant renderer tests beside `test_nrd_stage*.cpp`,
   `test_ssgi_restir_gi.cpp` and `test_gi_resource_validity.cpp` for the new
   machine-consumed contracts: pixel-to-UV conversion, tap rejection, reset epochs,
   constant-field preservation, valid black and eligibility. GPU captures prove
   actual shader integration; CPU helper tests alone do not prove denoising.

Stage 1 is complete only with all six gates. Do not broaden into mixed SSR, diffuse,
or shadow work to compensate for a failed bounded increment.

### 4.4 Stage 2: diffuse adoption and real sharing

Adopt plain SDF final gather first, then legacy screen SSGI under the same diffuse
client with its companion miss-fraction lane. Replace old half-resolution spatial
and temporal dispatches with common temporal plus two spatial strides; keep the
existing four-tap upsample/output contract. Build shared full-resolution guides
once when specular and diffuse both run. Reset histories at each producer-mode
change. Leave ReSTIR on its old path unless its dedicated opt-in is enabled later.

Respect `r.GI.ValidSampleReject` as an independent explicit producer control.
The common backend must honor availability when supplied, without inferring it
from luminance. Test new backend with that control 0 and 1; do not switch the
global default or advertise availability correction while input metadata remains
disabled. Retain world-gather fallback reconstruction for unsampled full pixels.

Acceptance: shared protocol plus unchanged screen-validity composition and odd
resolutions; static diffuse temporal standard deviation reduced >=20% in one
predeclared noisy-gather fixture, PT HDR RMSE parity within 2% everywhere else,
and eight-frame motion/field-change ghost error no worse than legacy. GDF
invalidation must reject on the same affected frames, and cache/light-only changes
must reset affected screen history.

Performance gate: combined guide + diffuse/specular reconstruction p95 <=1.10
times the sum of the old enabled reconstruction costs; total frame p95 <=1.05
times baseline. Account for the extra diffuse spatial stride and guide preparation
explicitly. A shared guide decode is the consolidation here, not proof of fewer
dispatches. Reduce passes only if quality gates continue to hold.

### 4.5 Stage 3: actual-hit specular and mixed SSR/RT

Add opt-in producer metadata permutations for actual RT sampled hit point/kind and
SSR hit position. Introduce current source identity/coverage separately from RGB.
On the new route, merge raw SSR hits and RT hole filling **before** common
reconstruction; replace SSR resolve + temporal + post-spatial with common temporal
+ spatial, rather than stacking the backend after all three legacy filters.
Leave the OFF producer/permutation/resource allocation untouched.

Acceptance: shared protocol; SSR-only, mixed, RT-only and screen-hit handover
scenes, mirrors and roughness sweep .04/.15/.35/.7, reflected moving objects and
receiver motion. Actual-hit tracking must reduce camera-motion reflection ghost
MSE >=20% versus stage 2, without reducing peak/energy gates. Source handovers
cannot leave stale reflected silhouettes or turn valid sky into no-hit fallback.
Target three legacy reconstruction passes down to two on the mixed route; guide
preparation and metadata writes count separately. Mixed reconstruction + metadata
p95 must not exceed old resolve/temporal/spatial p95, with total-frame p95 <=1.05
times baseline. If not met, retain opt-in status; do not silently retain both chains.

### 4.6 Stage 4: resolved ReSTIR client

Keep reservoir generation, generalized/pairwise MIS, null-domain counting and M caps
unchanged. Only the resolved E/pi enters the common diffuse client. Fix its
availability adapter explicitly: unsupported receivers are unavailable while
canonical null outcomes and genuinely black contributions retain their statistical
meaning. Do not map reservoir M to independent denoiser sample count.

Start with the common motion-shortened history policy, not the legacy full
reservoir-history plus 64-frame radiance-history chain. Acceptance: preserve the
existing exact-domain expectations in `test_ssgi_restir_gi.cpp`; compare legacy
ReSTIR + legacy reconstruction, ReSTIR + common reconstruction, and plain gather
at equal total replay-ray budgets. Require >=20% lower change-response ghost MSE
with static PT HDR RMSE parity within 2%; report correlation and temporal variance.
The backend may replace, not add to, the old three reconstruction passes.
Reconstruction p95 <=1.10 times its legacy equivalent and frame p95 <=1.05 times
baseline. No change to `r.GI.ReSTIR` or neighbor-count defaults.

### 4.7 Stage 5: per-light area visibility

Extract visibility production into an opt-in pre-lighting compute permutation.
Retain the same 16 source samples and two lobe-weighted ratios first; export
separate diffuse/specular visibility and stable light identity. Deferred lighting
reads filtered visibility for that light and evaluates the same unshadowed LTC
lobes; the OFF deferred permutation remains untouched. No aggregate shared shadow
factor for multiple lights and no second shadow multiplication.

Allocate layers only for active shadowed lights; key history by stable identity
and revision, not current array slot. Reset on transform/shape/intensity changes
initially; remove stale history when a light leaves. Do not claim a SIGMA-equivalent
penumbra filter without adding actual blocker-distance/penumbra metadata.
Initial specialized bounded 3x3 shadow filtering shares receiver guides,
tap validation and lifecycle; later penumbra-aware radius is a separate verified
increment within this stage.

For that later penumbra-aware increment, trace nearest blocker distance. The current
visibility query uses `ACCEPT_FIRST_HIT_AND_END_SEARCH`, which is sufficient for
binary occlusion but cannot supply reliable nearest-blocker metadata. NVIDIA's
SIGMA input guidance explicitly excludes that optimization for blocker distance.

Acceptance: rect/disk/tube, two overlapping lights, reordered light lists,
moving occluder, glossy receiver and light-motion fixtures; reuse the physical
emitter fixtures in `area-light-shadows/verify.py` after removing stale PT camera
compensation. At the same 16-ray budget, pre-TAA penumbra temporal standard
deviation reduced >=30%, PT HDR shadow-ROI RMSE improved >=10% versus TAA-only
baseline, with full-light mean bias change <=2 percentage points and unchanged
fully visible result. TAA-on motion ghost error cannot worsen. Report extraction
and denoise cost; total frame p95 <=1.10 times TAA-only baseline. Reducing source
samples is a subsequent opt-in, never bundled into this proof.

## 5. Risks, traps and completion boundary

- **Repeated temporal lag:** cache -> reservoir -> signal history -> TAA can all
  delay response. Record each stage's count/reject/clamp diagnostics and measure
  a lighting step. Do not "fix" the chain by increasing all history caps.
- **TAA interaction:** leave TAA unchanged initially. Compare both TAA-off signal
  output and final TAA-on output; a quieter input can still be over-clamped by TAA
  or lose energy through its HDR reconstruction. No second additive reflection
  composite, denoising final color, or global TAA history relaxation in this scope.
- **Missing guides:** current receiver depth/normal/roughness/motion exist; previous
  receiver geometry is not shared today, RT actual hit point is not exported,
  reflected-object motion is absent, and shadow visibility/light identity is not
  a texture input. None may be invented from reflection alpha or t alone.
- **Geometry versus lighting changes:** shared disocclusion does not detect all
  changed illumination. Preserve GDF/cache invalidation and add explicit per-client
  epochs, including surface-cache resolve/lighting revisions reaching its consumers.
- **Spatial leakage:** world-plane tests matter on sloped surfaces and narrow
  openings. Preserve current half-lattice and fallback behavior; an averaged guide
  at silhouettes can validate the wrong surface.
- **Root and descriptor pressure:** assert <=24 b0 DWORDs, keep growing data in
  CBV/structured records, declare RG resources and measure per-light layer memory.
  A custom PT root signature is not spare capacity in the shared real-time layout.
- **Default identity:** a mathematically equivalent common helper can still change
  DXC arithmetic or permutation selection. Keep legacy OFF shaders/dispatch order
  intact; no eager allocation, global CVar default changes or refactoring of their
  arithmetic. OFF byte identity is measured against the recorded baseline, not
  asserted from a branch condition.
- **Performance:** shared source is maintenance consolidation, not automatic GPU
  fusion. Guide preparation is extra bandwidth; moments, fast history and shadow
  layers can increase memory even if reconstruction dispatch count falls.
- **Reference validity:** old harness comments and camera compensation are stale
  relative to the requested fixed PT reference. Verify correspondence rather than
  tuning denoiser settings against misregistered images.
- **Scope:** this plan does not rewrite SH/DDGI/surface-cache world accumulation,
  PT `--nrd`, environment-MIS accumulation, transparency or volumetrics. Record
  their contribution to residual error/lag; do not expand Phase E silently.

Phase E unification is complete when the enabled diffuse, reflection and area-shadow
clients share the specified guide/lifecycle/validation/filter framework, old
reconstruction is bypassed only for selected clients, every staged gate is recorded,
and every OFF configuration remains byte-identical to `da33b8a6`. Default promotion
and removal of legacy code require a separate explicit decision after this work.
