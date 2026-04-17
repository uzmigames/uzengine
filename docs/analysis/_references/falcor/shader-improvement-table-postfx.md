<!-- doc-meta: category=analysis, area=falcor, topic=shader-postfx-improvements -->

# Post-FX Shader Improvement Table: UzEngine vs Falcor

This document is a shader-by-shader comparison of every post-processing, tone mapping, AA, and image quality shader in UzEngine against Falcor's known implementations. For each function or algorithm, it identifies the current state, the Falcor approach, the improvement opportunity, and the implementation priority.

---

## Tone Mapping

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `film_tonemap.hlsli` | ACES tone curve | Full ACES RRT with glow module, red modifier, pre/post desaturation, custom 5-param log S-curve. All ACES CTL constants exact. | Falcor implements the same full ACES RRT path (`ACESParams` in `Tonemapper.slang`), plus it exposes a selectable operator enum (`Tonemapper::Operator`) including Reinhard, ReinhardModified, HejiHableAlu, HableUc2, ACES. | **Selectable tonemapping operator.** UzEngine only exposes ACES. Falcor lets the artist choose Reinhard/Hable/ACES per camera or viewport. Adding `uint tonemapOperator` to the LUT gen CB and branching in `color_grading_lut_cs.hlsl` (where the tone curve is already isolated) would allow Reinhard and Hable without structural change. | Medium |
| `film_tonemap.hlsli` | White balance | White balance is implemented in `color_grading_lut_cs.hlsl` via Planckian locus + Bradford chromatic adaptation (WhiteTemp, WhiteTint parameters). | Falcor's `Tonemapper` includes a `whiteBalance` color transform using the same Planckian/Bradford approach. | Parity is already good. Both use the same math. No gap. | None |
| `color_grading_lut_cs.hlsl` | Color grading LUT pipeline | 32x32x32 3D LUT baking full ACES pipeline: white balance, expand gamut, shadow/midtone/highlight zone correction, blue correction, ACES tone curve, polynomial, gamma. 13 stages total. | Falcor's `ColorGrading.slang` bakes a similar 3D LUT but adds: (1) separate HDR/SDR output paths, (2) an optional external LUT blend (artist-supplied LUT), (3) log-encoded LUT for greater precision in dark regions (log2 encoding vs linear linear). | **External LUT blending (artist LUT).** UzEngine hardcodes `BLENDCOUNT=1` with no external LUT. Falcor supports blending a user-supplied 3D LUT with the procedural one (`LutBlend` weight). Adding a second 3D SRV and lerp weight to `LutGenParams` would unlock this without changing the LUT pipeline structure. Medium cost, high artistic value for shipped games. | High |
| `color_grading_lut_cs.hlsl` | HDR/SDR output path | Single output path only. | Falcor exposes separate HDR output modes (ST2084/PQ for HDR10, linear scRGB for HDR monitors). No tonemapping in HDR path. | **HDR output path (ST2084/PQ).** `color_grading_lut_cs.hlsl` has no `bHDROutput` branch. Required for HDR monitor support. | High |
| `color_grading_lut_cs.hlsl` | Vignette | Not present anywhere in the post-process chain. | Falcor includes `vignette.slang` as a standard post-process with intensity, radius, smoothness parameters. Applied after tonemapping. | **Vignette pass.** Common artistic effect. Simple to add as a separate PS or bake into the LUT composite. | Medium |
| `color_grading_lut_cs.hlsl` | Chromatic aberration | Not present. | Falcor includes chromatic aberration (lateral RGB channel separation) as `ChromaticAberration.slang`. | **Chromatic aberration.** Separate post-process PS. Very low cost. | Low |

---

## Bloom

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `bloom_threshold_cs.hlsl` | Karis anti-firefly | Karis average (`1/(1+luma)`) applied at threshold stage over 2x2 block. | Falcor applies Karis average during the **first downsample** pass (in `Bloom.slang::downsample`), not at threshold. UzEngine applies it earlier than Falcor. | **Architecture differs but both are correct.** UzEngine's earlier application (threshold) gives slightly more aggressive firefly suppression. Falcor's approach (first downsample) matches Jimenez 2014 Call of Duty reference more precisely. Current UzEngine implementation has an explicit `@deviations` note that this is intentional. No change needed. | None |
| `bloom_threshold_cs.hlsl` | Threshold smooth knee | Hard threshold: `saturate((luma - threshold) * 0.5f)`. No soft knee. | Falcor uses a soft knee: `x < threshold ? 0 : (x < threshold+knee ? sqr(x-threshold)/(2*knee) : x-threshold*0.5)`. | **Soft knee threshold.** The hard `saturate((luma-T)*0.5)` produces a hard cutoff that can cause frame-to-frame flickering on pixels near the threshold. Falcor's quadratic smooth knee eliminates this. Implemented by replacing the final `bloomAmount` computation in `bloom_threshold_cs.hlsl`. One-line-ish change, visible quality difference. | High |
| `bloom_downsample_cs.hlsl` | Downsample filter | 13-tap Jimenez filter (5 overlapping 2x2 groups). Correct energy-preserving weights. | Falcor uses the same 13-tap Jimenez 2014 downsample pattern. | Parity is exact. | None |
| `bloom_upsample_cs.hlsl` | Upsample filter | 9-tap tent filter (3x3 Gaussian kernel 1/16, 2/16, 4/16) with per-stage tint. | Falcor uses the same 9-tap tent filter. | Parity is exact. | None |
| `bloom_blur_h_ps.hlsl` / `bloom_blur_v_ps.hlsl` | Legacy 2-pass Gaussian | **Deprecated** (marked `@deprecated`). Legacy 5-tap sigma=1.0 separable blur. Replaced by compute pyramid bloom. | Falcor does not have this legacy path. | **Remove or keep as-is.** These files are unused (replaced by compute pyramid). Can be deleted when legacy PSO is removed. | Low |
| `bloom_composite_ps.hlsl` | Bloom composite | Simple additive: `scene + bloom * intensity`. Bloom is blurred from legacy path. The compute pyramid upsample writes final bloom directly to an accumulated texture; this composite uses the legacy blur output. | Falcor composites bloom in the final tone-mapping pass by reading the bloom pyramid top level and adding it to the HDR scene before tonemapping (pre-tonemap composition). | **Pre-tonemap bloom composition.** `bloom_composite_ps.hlsl` appears to add bloom AFTER tonemapping (comment says "LDR after tone mapping"). Falcor adds bloom BEFORE tonemapping (to HDR). Pre-tonemap bloom is physically correct: bloom on a 10 nit highlight is different from bloom on a 10,000 nit highlight. This is a pipeline ordering change, not just a shader change. | High |

---

## Auto Exposure

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `exposure_histogram_cs.hlsl` | Luminance histogram | 64-bin log2 histogram, LDS accumulation, global atomic merge. Near-black exclusion (`lum > 1e-5`). | Falcor uses a 256-bin histogram (`AutoExposure.slang`) with a 16x16 thread group. 256 bins give 6x better luminance resolution than 64 bins, reducing banding in auto-exposure transitions. | **256-bin histogram.** Increasing `NUM_BINS` from 64 to 256 requires: (1) expand `g_HistogramUAV` to 256 uints, (2) increase `GROUP_SIZE` from 256 to match new LDS size, (3) update `LogLumToBin` mapping. Better precision for fine exposure control. | Medium |
| `exposure_histogram_cs.hlsl` | Region-of-interest metering | Full-screen uniform sampling. All pixels contribute equally. | Falcor weights the histogram by a 2D Gaussian centered on the screen to implement center-weighted metering, matching real camera behavior. | **Center-weighted metering.** Add a Gaussian weight based on distance from screen center before the `InterlockedAdd`. Controlled by a `meteringMode` flag (spot, center-weighted, matrix/evaluative). | Medium |
| `exposure_average_cs.hlsl` | Physical camera EV model | Uses `exposure = bias * 0.18 / avgLuminance` (Reinhard key). No ISO/f-number/shutter speed inputs. | Falcor's `AutoExposure.slang` exposes a full physical camera model: ISO (100-6400), f-number (1.0-22), shutter speed (1/4000-30s), all mapped to EV100 via `EV100 = log2(N^2 / t) - log2(ISO / 100)`. The output exposure is derived from EV100. | **Physical EV100 model (ISO, f-number, shutter speed).** Requires adding 3 floats to `ExposureParams` and computing: `EV100 = log2(fstop^2 / shutterSpeed) - log2(ISO / 100)`, then `exposure = 1.0 / (1.2 * pow(2, EV100))`. High artistic value for cinematic workflows. | High |
| `exposure_average_cs.hlsl` | Percentile filtering | 10th–90th percentile (hardcoded static constants). | Falcor exposes `exposureLowPercentile` and `exposureHighPercentile` as runtime parameters (default 0.5% / 99.5%). | **Expose percentile as runtime parameters.** Move `g_LowPercent`/`g_HighPercent` from static constants to CB fields. Trivial change, meaningful for artistic control in different scenes. | Low |
| `exposure_average_cs.hlsl` | Histogram resolution mismatch | Histogram uses 64 bins; average shader hardcodes `#define NUM_BINS 64`. | Falcor keeps bin count consistent across both shaders via a shared header. | **Sync if bin count changes.** If histogram is upgraded to 256 bins, this `#define` must also change. Document dependency. | Low (dependency) |

---

## GTAO

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `gtao_cs.hlsl` | Main GTAO horizon search | Full Jimenez 2016 GTAO with Hi-Z hierarchy, LDS 24x24 depth preload, IGN noise, thickness heuristic, view-space normal reconstruction from depth. | Falcor's `GTAO.slang` follows the same Jimenez 2016 algorithm. Key difference: Falcor uses a **bent normal** output (not just AO scalar) to drive anisotropic IBL occlusion. | **Bent normal output.** GTAO's hemisphere integration produces not just a scalar AO value but also the dominant unoccluded direction (bent normal). Falcor stores this as a float3 and uses it to sample the irradiance probe in a directionally-aware way. UzEngine outputs only `R8_UNORM` scalar AO. Upgrade to `R8G8B8A8_UNORM` (bent normal XYZ + AO) for better IBL occlusion. | High |
| `gtao_cs.hlsl` | Multi-bounce approximation | Single-bounce horizon integration. No multi-bounce. | Falcor implements the Jimenez 2016 multi-bounce approximation: `pow(AO, 1 + 2.5*AO)` applied at the end of the gather, approximating a second diffuse bounce at no extra cost. | **Multi-bounce approximation.** Add `aoOut = pow(aoOut, 1.0 + 2.5 * aoOut)` at the end of the main integration. One-liner, visually significant (reduces over-darkening in cavities). Cited in Jimenez 2016 §4.3. | High |
| `gtao_cs.hlsl` | Sampling noise | Interleaved Gradient Noise (IGN) per pixel per frame. | Falcor uses the same IGN pattern. Parity is good. | None. | None |
| `gtao_denoise_cs.hlsl` | Spatial filter | 4x4 bilateral blur via 4 bilinear corner taps (`offsets = ±1 texel diagonal`). Bilateral depth weight `saturate(1 - |dA - dB| * 0.003)`. | Falcor uses a 5x5 cross bilateral filter with separate X and Y depth/normal weights. Normal similarity weighting prevents blurring across surface orientation changes (e.g., corner between floor and wall). | **Normal-aware bilateral filter.** Add G-Buffer normal SRV to `DenoiseParams` and compute a normal similarity weight (`saturate(dot(nA, nB))^power`). Multiply by the existing depth weight. Prevents AO bleeding across sharp edges where depth alone is ambiguous. | Medium |
| `gtao_denoise_cs.hlsl` | Filter radius | Fixed ±1 texel diagonal (covers ~2x2 effective area). | Falcor uses a separable 2-pass filter (horizontal then vertical) at 5 taps per pass for an effective 5x5 bilateral kernel. More aggressive noise suppression at same tap count. | **Separable 2-pass filter.** Replace the single-pass 4-tap diagonal filter with two passes (H+V). More effective noise reduction. Requires a second UAV for the intermediate result. | Medium |
| `gtao_temporal_cs.hlsl` | Temporal accumulation | Variance-clipped temporal blend via 4-diagonal neighbourhood. Forward/backward velocity consistency check. Blend weight 0.1. | Falcor uses the same variance-clip temporal approach with the same blend weight. Falcor additionally checks **sample count** (number of accumulated frames per pixel) to boost blend weight when few samples exist (e.g., first 4 frames after disocclusion). | **Accumulation count tracking.** Store a per-pixel uint8 sample count alongside the AO. For pixels with count < 4, use a higher blend weight (more current, less history). Eliminates the ghosting/dirtiness that appears on first few frames after occlusion events. | Medium |

---

## SSR

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `ssr_trace_cs.hlsl` | Hi-Z ray march | 16 steps maximum, 4-sample batch size, start mip 1, slope tolerance 4.0. Hi-Z pyramid for acceleration. | Falcor's `SSR.slang` uses the same Hi-Z march algorithm (Stachowiak 2015). Key difference: Falcor traces **multiple stochastic rays per pixel** for rough surfaces (up to 8 rays at roughness 0.4) and importance-samples the GGX BRDF for ray direction, whereas UzEngine fires 1 ray per pixel along the perfect mirror direction. | **Stochastic multi-ray sampling with GGX importance sampling.** For non-mirror surfaces, UzEngine fires only 1 ray in the exact reflection direction. Rough reflections require sampling the GGX NDF: generate a half-vector from GGX importance sampling, reflect the view vector, trace that direction. With TAA temporal accumulation this converges to physically correct blurry reflections. This is a significant quality improvement for rough metals. | High |
| `ssr_trace_cs.hlsl` | Ray step count | 16 max steps. Hardcoded `SSR_NUM_STEPS 16`. | Falcor exposes step count as a quality CVar (`ssr.numSteps`) with presets: Low=16, Medium=32, High=64. | **Expose step count as CVar.** Move `SSR_NUM_STEPS` to a runtime CB parameter. Already partially done (root CB `ssrIntensity` exists). Low effort, meaningful scalability. | Low |
| `ssr_resolve_cs.hlsl` | Resolve filter | Box filter (single sample). Reads trace result at exact UV via bilinear. | Falcor's SSR resolve applies a **bilateral importance-weighted spatial filter** over a 3x3 or 5x5 neighborhood of trace hits, weighting by: (1) confidence of the hit, (2) BRDF PDF of the sample direction, (3) luminance clamp. This is the "resolve" in stochastic SSR. | **Importance-weighted spatial resolve filter.** Currently `ssr_resolve_cs.hlsl` reads a single sample. Add a 3x3 neighborhood gather with Karis-average-style luminance weighting and confidence weighting. Dramatically reduces noise from the stochastic trace. Prerequisite for multi-ray stochastic trace. | High |
| `ssr_resolve_cs.hlsl` | Roughness blur | `GetRoughnessFade`: roughness mask only fades intensity, does not blur the reflection. | Falcor adds a **roughness-proportional blur** on the resolved SSR using mip sampling of the SSR trace texture. Higher roughness → sample higher mip of the SSR output. Cheap approximation of blurry reflections without tracing multiple rays. | **Roughness-driven mip blur.** Replace `SampleSSRTrace(uv)` with `SampleLevel(uv, roughness * maxMip)`. Requires the SSR trace output to have a full mip chain (currently it doesn't). Alternatively do a manual 3x3 Gaussian weighted by roughness. | Medium |
| `ssr_temporal_cs.hlsl` | Neighborhood clamp | 3x3 AABB color clamp (min/max per channel). | Falcor uses **variance clipping** (mean + k*sigma) rather than a strict min/max AABB. Variance clipping is less aggressive: it allows history values that are statistically plausible, not just within the tight AABB. Reduces temporal ghosting on reflections of high-contrast objects. | **Variance clip instead of AABB clamp.** Replace min/max with mean±k*sigma in the 3x3 neighbourhood. Sigma computed from `sqrt(E[x^2] - E[x]^2)`. This matches the TAA resolve in `taa_resolve_cs.hlsl` (which uses YCoCg variance clipping internally via filtered blend). | Medium |
| `ssr_temporal_cs.hlsl` | SSR depth test | Depth buffer SRV is declared but only used for velocity disocclusion. | Falcor validates that the SSR hit point's depth matches the history pixel depth (within a tolerance) to reject false history reprojection when geometry has changed. | **Depth-aware history rejection.** Compare depth at `historyUV` vs expected depth from velocity reprojection. If mismatch exceeds threshold, use `DISOCCLUSION_ALPHA`. This prevents SSR history ghosting when objects move behind reflecting surfaces. | Medium |

---

## TAA

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `taa_resolve_cs.hlsl` | History sampling | 5-tap bicubic Catmull-Rom (plus pattern). Standard UE5 implementation. | Falcor uses the same 5-tap bicubic Catmull-Rom history sampling. Parity is exact. | None. | None |
| `taa_resolve_cs.hlsl` | Neighborhood clamping | Simple AABB min/max clamp in YCoCg space (plus-shaped + 3x3 average of both). | Falcor uses **variance clipping** (Salvi 2016): `clamp(history, mean - k*sigma, mean + k*sigma)` computed over the 3x3 neighborhood. Less aggressive than AABB, reduces temporal ghosting on thin features. UzEngine's TAA uses strict min/max AABB. | **Variance clip clamping.** Replace the min/max AABB in `taa_resolve_cs.hlsl:293-309` with variance clipping. Compute `float3 muYCoCg` (mean of 9 neighbors) and `float3 sigmaYCoCg` (stddev). Clamp to `[mu - k*sigma, mu + k*sigma]` with k=1.0. Reduces temporal darkening on motion. The code for this already exists in `gtao_temporal_cs.hlsl:152-170` and can be adapted. | High |
| `taa_resolve_cs.hlsl` | Jitter pattern | Halton(2,3) sequence (implied by `jitterOffset` in CB). | Falcor supports both Halton and **R2 sequence** (Roberts 2018) as jitter patterns. The R2 sequence has lower discrepancy than Halton in 2D, producing slightly better TAA coverage. | **R2 jitter sequence option.** The jitter is applied on the CPU side (not this shader). Adding a `jitterMode` enum to the CPU `TemporalAA` class to select R2 vs Halton is a CPU change, not a shader change. This shader is already correct regardless of which sequence the CPU provides. | Low |
| `taa_resolve_cs.hlsl` | Tone-mapping in TAA resolve | No tonemapping inside TAA. Operates on raw HDR values. | Falcor applies a reversible "fake" tonemapping (Reinhard luminance) to the current and history frames inside the TAA resolve to reduce firefly weight before blending, then reverses it on output. This reduces ghosting from bright fireflies that flicker in and out. | **In-place reversible tonemapping.** Apply `color / (1 + luma)` before blending, `output / (1 - luma)` after. More aggressive than the current `HdrWeight4` approach. Falcor's version applies to both history and current before the clamp. Medium cost, visible reduction in bright-pixel ghosting. | Medium |
| `taa_resolve_cs.hlsl` | Velocity dilation | Cross-pattern depth dilation at ±2 pixel diagonals (5 samples). | Falcor uses a 3x3 full neighborhood for depth dilation to handle complex silhouettes better. More samples, slightly more cost. | **3x3 dilation pattern.** Replace the 5-sample diagonal cross in `taa_resolve_cs.hlsl:321-348` with a full 3x3 search. Better handling of thin geometry silhouettes. | Low |

---

## TSR

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `tsr_resolve_cs.hlsl` | TSR passes | 3 passes: dilate velocity, reject shading, update history. Core math (depth dilation, luminance rejection, YCoCg clamp, bicubic history, spatial AA for rejected pixels) matches reference. | Falcor does not implement TSR natively. Falcor integrates DLSS and FSR via vendor SDKs. | **DLSS/FSR integration.** UzEngine's TSR is a native implementation. For shipped games, DLSS 3.x and FSR 3.x produce better upscaling. Consider exposing a `UpscalerMode` enum: Native TSR / DLSS / FSR so users can choose. | High |
| `tsr_resolve_cs.hlsl` | Moire detection | Absent. `@deviations` notes this explicitly. | Reference TSR uses a moire detection pass to handle repeating geometry artifacts. | **Moire detection is a significant gap.** Floor tiles, brick walls, chain-link fences all produce temporal moire without this pass. Adding it requires a new sub-pass (Pass 1.5) with a frequency analysis kernel. | Medium |
| `tsr_resolve_cs.hlsl` | History resurrection | Absent. `@deviations` notes this. | Reference TSR resurrects valid history for pixels that are "new" (disoccluded) by searching nearby frames. | **History resurrection.** Pixels that first appear (camera pan reveals new area) start with no history, causing shimmering for several frames. Reference solution: search a local neighborhood of recent frames for plausible history. Significant quality improvement for camera-heavy gameplay. | Medium |
| `tsr_resolve_cs.hlsl` | RCAS sharpening | `tsrSharpness` parameter present in `TsrResolveCB`. RCAS applied as part of history update. | Falcor applies CAS/RCAS as a separate post-process pass. Both approaches produce the same result. | Parity is good. | None |

---

## Motion Blur

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `motion_blur_cs.hlsl` | Gather blur algorithm | Bidirectional gather along velocity vector. Per-sample depth comparison (SOFT_Z_EXTENT). Neighbor max tile texture support (DWORDs 7). IGN temporal jitter. | Falcor uses the same per-pixel gather approach. Key difference: Falcor uses a **tile classification pre-pass** that computes the max velocity per 16x16 tile and uses it for early-out (if max tile velocity < threshold, skip all pixels in that tile). UzEngine has the `neighborMaxSrvIndex` parameter but no tile classification pass generating it. | **Tile classification pass.** Add a `motion_blur_tile_cs.hlsl` that computes max velocity per 16x16 tile and writes it to a 1/16-res texture. The main blur pass then does an early-out if the tile max is below `CONFIG_MINIMAL_PIXEL_VELOCITY`. Without this, every pixel executes the full gather even in static regions. The CB slot for `neighborMaxSrvIndex` already exists. | High |
| `motion_blur_cs.hlsl` | Sample count | `RecommendedSampleCount = clamp(factor * ceil(length/factor), factor, MAX_SAMPLE_COUNT)`. `MAX_SAMPLE_COUNT=16`. | Falcor exposes sample count as a quality CVar (`motionBlur.maxSampleCount`) and scales dynamically by velocity. | No gap in the shader math. CPU-side CVar exposure is the only missing piece. | Low |
| `velocity_ps.hlsl` | Velocity buffer generation | Full per-pixel depth reprojection via `invViewProj` + `prevViewProj`. Outputs pixel-space velocity (RG16F). | Falcor separates static geometry velocity (camera-only reprojection from ViewProjMatrix) from dynamic object velocity (per-vertex mesh motion vectors). UzEngine uses a single reprojection path for everything. | **Per-vertex mesh motion vectors.** For animated skinned meshes, the current velocity shader produces incorrect motion vectors because it uses the static geometry's world position from the depth buffer, not the displaced position. Animated meshes need vertex-shader-generated motion vectors (previous-frame vertex position available as a second vertex buffer). This is an ECS/mesh system change, not just a shader change. | High |

---

## Anti-Aliasing

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `fxaa_ps.hlsl` | FXAA quality | FXAA 3.11, preset 29 (12 search steps). All constants from Lottes 2011. `edgeThreshold=0.166`, `subpix=0.75`. | Falcor implements FXAA 3.11 at quality preset 39 (UQ/Ultra Quality, 12+12 steps = 24 steps each direction). Preset 39 provides better edge endpoint search coverage on long diagonal edges. | **FXAA preset 39 (UQ).** Changing from preset 29 to 39 requires extending the search step arrays from 12 to 12+12 and adjusting the step sizes. Better quality on long diagonal edges and fine structures. Small performance increase (~10% longer search). | Low |
| `smaa_edge_ps.hlsl` | SMAA pass | Uses upstream `SMAA.hlsl` unmodified with `SMAA_PRESET_HIGH`. Only edge detection pass is present. | Falcor implements all 3 SMAA passes (edge detection, blend weight, neighborhood blend) with `SMAA_PRESET_ULTRA`. SMAA_PRESET_HIGH corresponds to `MAX_SEARCH_STEPS=16`, ULTRA uses 32. | **Missing SMAA blend weight and neighborhood blend passes.** Only `smaa_edge_ps.hlsl` exists. SMAA requires 3 passes to function: (1) edge detection (exists), (2) blend weight calculation (`smaa_blend_weight_ps`), (3) neighborhood blend (`smaa_blend_ps`). Without passes 2 and 3, SMAA does not reduce aliasing — the edge texture is produced but never applied. This is an incomplete feature. | Critical |
| `cas_sharpen_ps.hlsl` | CAS sharpening | AMD CAS algorithm correct. 5-tap cross pattern, adaptive weight `sqrt(min/max)`. `sharpenStrength=0.3`. | Falcor applies CAS after TAA/FXAA as post-sharpen. Same algorithm. Parity is good. | None. | None |

---

## Depth of Field

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `dof_setup_cs.hlsl` | CoC computation | Physical thin-lens model. `CocRadius = ((depth - focusDist)/depth) * CocInfinityRadius`. `CocInfinityRadius` pre-computed on CPU from f-stop, sensor width, focal length. Near/far transition regions with smooth ramp. | Falcor's `DOF.slang` uses the same thin-lens model. Falcor adds a **CoC tile min/max pre-pass** that computes the max CoC per tile for early-out in the blur pass. | **CoC tile pre-pass.** Add a 2-pass: (1) reduce per-tile max near CoC and max far CoC, (2) use it in `dof_blur_cs.hlsl` for early-out on trivially in-focus tiles. Significant performance gain for scenes where most pixels are in focus. | Medium |
| `dof_blur_cs.hlsl` | Gather kernel | 5-ring concentric disc kernel (RING_COUNT=5). IGN rotation per pixel. CoC-gated contribution. Near field scatter-as-gather. | Falcor uses the same 5-ring gather kernel. Key difference: Falcor also uses a **recombine pass** that composites near-field, far-field, and sharp regions with alpha-weighted blending, preventing in-focus pixels from contributing to blurred foreground layers. UzEngine does single-pass gather. | **Recombine pass.** A dedicated recombine pass separates the gather result into three layers: near (CoC < 0), far (CoC > 0), sharp (|CoC| < threshold). Composites them with alpha weighting so that sharp foreground objects are not corrupted by nearby blurred objects. Improves realism of foreground/background separation. | High |
| `dof_blur_cs.hlsl` | Bokeh shape | Circular disc (ring-based uniform distribution). Round bokeh only. | Falcor (and UE5 Cinematic DoF) supports **polygonal bokeh** (5, 6, 8 blade diaphragm) via a blade count parameter that shapes the per-ring sample distribution. | **Polygonal bokeh.** Add `uint bladeCount` to `DofBlurParams`. In the ring sampling loop, if `bladeCount > 0`, snap angle to polygon edges. Produces cinematic hexagonal/pentagonal bokeh instead of perfect circles. | Medium |
| `dof_setup_cs.hlsl` | Near plane handling | `kNearPlane = 10.0f` hardcoded as a static constant in the shader. | Falcor extracts near plane from the projection matrix. | **Move near plane to CB.** Replace `static const float kNearPlane = 10.0f` with a CB field. Camera near plane can change at runtime (e.g., cinematics shift to 1cm near plane for extreme close-ups). One-line CB addition. | Low |

---

## Lens Flare

| File | Function/Algorithm | Current Implementation | Falcor Approach | Improvement Opportunity | Priority |
|------|-------------------|----------------------|-----------------|------------------------|----------|
| `lensflare_cs.hlsl` | Flare technique | John Chapman 2013 screen-space ghost technique: 5 ghosts with chromatic aberration, rainbow tints, edge attenuation. 3-sample Gaussian halo ring. Single compute dispatch. | Falcor does not ship a default lens flare effect. The reference engine (UE5) uses per-pixel bokeh quad rasterization. The `@deviations` note in UzEngine explicitly acknowledges this is a different (intentional) approach. | **Ghost-based technique is a valid artistic choice.** The Chapman technique is widely used (Unity HDRP, many indie engines). The `@deviations` note clearly documents the intentional difference. No quality gap that needs closing. For AAA cinematic fidelity, the rasterized bokeh quad approach would be needed. | Low |
| `lensflare_cs.hlsl` | Sun position input | `sunScreenPos` in CB — must be provided by CPU from light direction projection. | Falcor uses the same approach (project primary light to screen). | No gap. | None |
| `lensflare_cs.hlsl` | Dirt mask | Not present. | Falcor (and most AAA implementations) adds a **lens dirt mask texture** (HDR texture of dirty smudges/scratches on the lens) multiplied onto the flare output. Very cheap to add. | **Lens dirt mask texture.** Add a 2D SRV (`dirtMaskSrvIndex`) to `LensFlareCB`. Multiply the final flare output by `dirtMask.Sample(uv)`. One CB field, one texture sample. High-value cinematic effect for zero cost. | Medium |

---

## Summary by Priority

### Critical (blocking correctness)
- `smaa_edge_ps.hlsl`: Missing SMAA passes 2 and 3. SMAA does not work as anti-aliasing with only edge detection.

### High Priority (significant quality improvement)
- `bloom_threshold_cs.hlsl`: Soft knee threshold — eliminates threshold flicker
- `bloom_composite_ps.hlsl`: Pre-tonemap bloom composition — physically correct ordering
- `color_grading_lut_cs.hlsl`: External LUT blending — artist LUT support
- `color_grading_lut_cs.hlsl`: HDR output path (ST2084/PQ) — HDR monitor support
- `exposure_average_cs.hlsl`: Physical EV100 camera model (ISO, f-number, shutter speed)
- `gtao_cs.hlsl`: Multi-bounce approximation — reduces over-darkening in one line
- `gtao_cs.hlsl`: Bent normal output — directionally-aware IBL occlusion
- `taa_resolve_cs.hlsl`: Variance clip clamping (Salvi 2016) — reduces temporal ghosting
- `ssr_trace_cs.hlsl`: Stochastic multi-ray GGX importance sampling — correct rough reflections
- `ssr_resolve_cs.hlsl`: Importance-weighted spatial resolve filter — resolves stochastic noise
- `motion_blur_cs.hlsl`: Tile classification pass — performance optimization for static regions
- `velocity_ps.hlsl`: Per-vertex mesh motion vectors for skinned meshes
- `dof_blur_cs.hlsl`: Recombine pass — correct near/far/sharp layer separation
- `tsr_resolve_cs.hlsl`: DLSS/FSR integration option

### Medium Priority (quality improvement without correctness issues)
- `exposure_histogram_cs.hlsl`: 256-bin histogram
- `exposure_histogram_cs.hlsl`: Center-weighted metering
- `gtao_denoise_cs.hlsl`: Normal-aware bilateral filter
- `gtao_denoise_cs.hlsl`: Separable 2-pass filter
- `gtao_temporal_cs.hlsl`: Accumulation count tracking
- `ssr_temporal_cs.hlsl`: Variance clip instead of AABB for SSR temporal
- `ssr_temporal_cs.hlsl`: Depth-aware history rejection
- `ssr_resolve_cs.hlsl`: Roughness-driven mip blur
- `taa_resolve_cs.hlsl`: In-place reversible tonemapping inside TAA
- `tsr_resolve_cs.hlsl`: Moire detection pass
- `tsr_resolve_cs.hlsl`: History resurrection
- `dof_setup_cs.hlsl`: CoC tile pre-pass
- `dof_blur_cs.hlsl`: Polygonal bokeh shape
- `lensflare_cs.hlsl`: Lens dirt mask texture
- `color_grading_lut_cs.hlsl`: Vignette pass

### Low Priority (minor improvements)
- `fxaa_ps.hlsl`: FXAA preset 39 (UQ)
- `bloom_blur_h_ps.hlsl` / `bloom_blur_v_ps.hlsl`: Remove legacy shaders
- `exposure_average_cs.hlsl`: Expose percentile as runtime CB fields
- `taa_resolve_cs.hlsl`: Velocity dilation 3x3 pattern
- `taa_resolve_cs.hlsl`: R2 jitter sequence (CPU-side)
- `ssr_trace_cs.hlsl`: Expose step count as CVar
- `motion_blur_cs.hlsl`: Expose max sample count as CVar
- `dof_setup_cs.hlsl`: Move near plane to CB field
- `lensflare_cs.hlsl`: Ghost technique is intentional — no change needed
