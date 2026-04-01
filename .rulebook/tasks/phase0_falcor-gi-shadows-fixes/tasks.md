## GI + Shadows + Atmosphere Improvements (from Falcor analysis)
> Source: `docs/analysis/falcor/shader-improvement-table-shadows-gi-atmo.md`
> Estimate: 4 sessions

## Session 1: SSGI quality upgrade

- [ ] 1.1 `ssgi/ssgi_trace_cs.hlsl` — replace linear screen-space march with Hi-Z hierarchical traversal. Start at mip 0, step up when no intersection, step down when hit. @ref McGuire & Mara 2014.
- [ ] 1.2 `ssgi/ssgi_temporal_accum_cs.hlsl` — replace AABB min/max clamp with variance-guided clipping (μ ± γ·σ). @ref Schied 2017 (SVGF).
- [ ] 1.3 `ssgi/ssgi_spatial_denoise_cs.hlsl` — add edge-stopping weights on luminance variance (not just depth/normal). @ref Schied 2017.
- [ ] 1.4 Build + verify: SSGI should catch thin geometry (Hi-Z) and show less ghosting (variance clip).

## Session 2: PCF + PCSS quality + shadow improvements

- [ ] 2.1 `include/pcss.hlsli` — add per-pixel Poisson disk rotation using Interleaved Gradient Noise (IGN). Eliminate visible banding on shadow edges.
- [ ] 2.2 `include/pcss.hlsli` — implement `BlockerSearchPCSS` with area light source size parameter for physically-based penumbra.
- [ ] 2.3 `include/shadow_common.hlsli` — add exponential shadow map (ESM) sampling mode as alternative to PCF. `exp(-c * (receiver_depth - occluder_depth))`. @ref Annen et al. 2008.
- [ ] 2.4 `include/contact_shadows.hlsli` — add Hi-Z acceleration for contact shadow ray march (currently linear step).
- [ ] 2.5 Build + verify: shadow edges should show varied noise pattern (no banding). Contact shadows should be faster.

## Session 3: Atmosphere + clouds quality

- [ ] 3.1 `atmosphere/cloud_raymarch_cs.hlsl` — implement detail noise erosion: sample 3D detail noise at higher frequency, subtract from base density to add cloud edge detail. @ref Schneider & Vos 2015.
- [ ] 3.2 `atmosphere/cloud_temporal_cs.hlsl` — implement temporal reprojection: reproject previous frame's cloud output using motion vectors, blend with current frame. @ref Schneider & Vos 2015.
- [ ] 3.3 `atmosphere/god_rays_cs.hlsl` — replace single 64-sample radial blur with 3-pass progressive blur (16 samples × 3 passes at increasing scale). Correct light shaft shape.
- [ ] 3.4 `atmosphere/cloud_composite_ps.hlsl` — add cloud aerial perspective: apply atmospheric scattering to cloud color based on distance. @ref Hillaire 2020.
- [ ] 3.5 Build + verify: clouds should have detailed edges (not plastic). God rays should have correct shape.

## Session 4: SVGF denoiser (standalone pass — no SDK)

- [ ] 4.1 Create `postprocess/svgf_reproject_cs.hlsl` — temporal reprojection: load prev frame moments (μ, σ²), accumulate with current luminance. Discard on disocclusion. @ref Schied 2017.
- [ ] 4.2 Create `postprocess/svgf_filter_moments_cs.hlsl` — spatial filter on variance: 3×3 cross bilateral on depth + normal. Output filtered variance for à-trous.
- [ ] 4.3 Create `postprocess/svgf_atrous_cs.hlsl` — à-trous wavelet filter: 5 iterations with doubling step (1, 2, 4, 8, 16). Edge-stopping on depth, normal, luminance variance. @ref Schied 2017.
- [ ] 4.4 C++ `SVGFPass` — register 5-pass pipeline in render graph (reproject → filter moments → 4× à-trous). Manage history buffers.
- [ ] 4.5 Wire SVGF as optional denoiser for SSGI output (replace current simple spatial+temporal).
- [ ] 4.6 Build + verify: SSGI with SVGF should be significantly cleaner with less temporal lag.
