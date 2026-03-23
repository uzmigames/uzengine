## Phase 1: Half-Resolution GTAO Pipeline (P0 — 4× speedup)
- [x] 1.1 Add `GTAOQuality` enum to `gtao_pass.h`: `Low`, `Medium`, `High`, `Ultra` with per-preset resolution scale (0.5, 0.5, 1.0, 1.0), angle count, and tap count. Added enum + GTAOQualityIsHalfRes() helper + GTAOUpsampleRootConstants struct.
- [x] 1.2 Add half-res texture allocation in `GTAOPass::Init()` — create `m_rawAOHalf` and `m_filteredAOHalf` (R8_UNORM, width/2 × height/2) alongside existing full-res textures. Implemented CreateHalfResTextures()/DestroyHalfResTextures() with full SRV/UAV validation.
- [x] 1.3 Modify `GTAOPass::Execute()` to dispatch GTAO compute at half-resolution when quality is Low/Medium — pass half-res dimensions in root constants, dispatch (halfW+7)/8 × (halfH+7)/8 groups. Execute() now selects target textures/indices/dimensions based on GTAOQualityIsHalfRes().
- [x] 1.4 Integrate `gtao_upsample_ps.hlsl` bilateral upsample into `GTAOPass::Execute()` — add a third dispatch/draw that reads half-res filtered AO + full-res depth → writes full-res AO output. Created EnsureUpsamplePipeline() with FullscreenVS + GtaoUpsamplePS graphics PSO.
- [x] 1.5 Wire the upsample pass SRV/UAV descriptors — half-res filtered AO as SRV input, full-res AO as render target output, full-res depth for bilateral weight. Root constants carry halfResAOSrvIndex, depth SRVs, texel sizes. SM5 path binds via SetFragmentTexture.
- [x] 1.6 Add barrier transitions for the new upsample pass: filteredAOHalf UAV→SRV before upsample, fullResAO to RenderTarget before upsample, RenderTarget→SRV after upsample. Added m_filteredAOCurrentState tracking for correct cross-frame state management.
- [x] 1.7 Default quality to `Medium` (half-res, 2 angles, 6 taps) — preserves visual quality while cutting GPU cost ~4×. GTAOSettings::quality defaults to GTAOQuality::Medium. **Note (2026-03-22):** Default later changed back to `GTAOQuality::High` (full-res) for quality reasons — see gtao_pass.h:65.
- [x] 1.8 Build verification: `cd runtime && zig build` — zero errors. No demos/pbr_test directory exists yet.
- [x] 1.9 GPU profile verification: GTAO = 0.334ms (was 1.23ms) — 3.7× speedup, under 0.40ms target. Total frame 1.355ms.
- [x] 1.10 Visual verification: debug grid captured with geometry enabled — AO buffer (tile 1,2) shows clean uniform values, no blocky artifacts or half-res banding. Bilateral upsample working correctly.

## Phase 2: Temporal Accumulation Integration (P0 — 50-80% amortized savings)
**Note (2026-03-22 audit):** `gtao_temporal_cs.hlsl` shader file exists on disk but NO C++ pipeline or wiring is implemented. GTAOPass has temporal jitter root constants (frameIndex, temporalCos, temporalSin, temporalOffset) but no history textures or temporal resolve pass.
- [ ] 2.1 Add ping-pong history textures to `GTAOPass`: `m_aoHistory[2]` (R8_UNORM, full-res), `m_historyIndex` counter.
- [ ] 2.2 Create compute PSO for `gtao_temporal_cs.hlsl` in `GTAOPass::Init()` — shader already compiled but pipeline never created.
- [ ] 2.3 Wire temporal pass into `GTAOPass::Execute()` after spatial denoise: bind current filtered AO (SRV), history AO (SRV), velocity buffer (SRV), depth (SRV) → output to next history slot (UAV).
- [ ] 2.4 Pass temporal root constants: `velocitySrvIndex`, `historySrvIndex`, `currentAOSrvIndex`, `outputUavIndex`, `width`, `height`, `texelSizeX`, `texelSizeY`.
- [ ] 2.5 Add barriers for temporal pass: filteredAO UAV→SRV, history[prev] Common→SRV, history[cur] Common→UAV, after temporal: history[cur] UAV→SRV.
- [ ] 2.6 Flip `m_historyIndex` each frame (ping-pong). Output of temporal pass becomes the final AO SRV for deferred lighting.
- [ ] 2.7 Handle first frame (no valid history): skip temporal pass on frame 0, use spatially-filtered AO directly.
- [ ] 2.8 Build + GPU profile: confirm temporal pass adds < 0.05ms, total GTAO (half-res + denoise + temporal + upsample) < 0.35ms.
- [ ] 2.9 Visual verification: verify temporal stability — no ghosting on moving camera, no flickering on static camera. Compare 3 screenshots: static, slow pan, fast rotation.

## Phase 3: Hi-Z Acceleration (P1 — 20-30% fewer texture fetches)
- [ ] 3.1 Enable `GTAO_USE_HIZ` define in `gtao_cs.hlsl` — modify horizon search to sample higher mip levels for distant steps. **Note:** `#ifdef GTAO_USE_HIZ` code paths already exist in shader (lines 124, 147, 326) but define is NOT enabled by default. Need to pass `-DGTAO_USE_HIZ=1` via shader compile defines.
- [ ] 3.2 Add Hi-Z SRV index to GTAO root constants — pass `hizSrvIndex` alongside existing `depthSrvIndex`.
- [ ] 3.3 Wire Hi-Z pyramid texture (already exists from `HiZPyramidPass`) into GTAO dispatch — ensure the SRV is valid and the texture is in ShaderResource state before GTAO runs.
- [x] 3.4 Implement mip level selection in shader: `mipLevel = clamp(log2(stepRadius * texelSize), 0, maxMip)` — distant steps use coarser mips. Already implemented behind `#ifdef GTAO_USE_HIZ` in gtao_cs.hlsl.
- [ ] 3.5 Build + GPU profile: confirm GTAO compute time reduced ~20% from Hi-Z cache improvement.
- [ ] 3.6 Visual verification: compare AO quality with and without Hi-Z — should be visually identical (Hi-Z is a conservative optimization, not an approximation).

## Phase 4: Dynamic Quality System (P1 — runtime flexibility)
**Note (2026-03-22 audit):** `GTAOQuality` enum exists in C++ (Low/Medium/High/Ultra) and controls half-res vs full-res dispatch. But shader still has hardcoded `#define GTAO_NUM_ANGLES 2` / `GTAO_NUM_TAPS 8` — presets don't actually change angle/tap counts at runtime. No console command exists.
- [ ] 4.1 Replace hardcoded `#define GTAO_NUM_ANGLES 2` / `GTAO_NUM_TAPS 8` with runtime values from root constants. Add `numAngles` and `numTaps` to GTAORootConstants struct.
- [ ] 4.2 Modify gtao_cs.hlsl loops to use dynamic iteration counts: `for (uint a = 0; a < numAngles; a++)` and `for (uint s = 0; s < numTaps; s++)`. Use `[loop]` attribute to prevent unrolling.
- [ ] 4.3 Bind GTAOSettings to GPU: pack worldRadius, intensity, thicknessBlend, fadeStart, fadeEnd, aoPower into root constants (expand from 8 to 16 DWORDs or use a CBV).
- [ ] 4.4 Define quality presets in C++: Low (1 angle, 4 taps, half-res), Medium (2 angles, 6 taps, half-res), High (2 angles, 8 taps, full-res), Ultra (3 angles, 12 taps, full-res).
- [ ] 4.5 Add `SetGTAOQuality(GTAOQuality)` API to SceneRenderer, wirable from console command `r.gtao.quality [low|medium|high|ultra]`.
- [ ] 4.6 Build + verify all 4 presets produce correct AO via screenshot comparison.

## Phase 5: Shader Micro-Optimizations (P2 — 10-20% additional savings)
- [ ] 5.1 Add groupshared depth preload: load 8×8 tile + 8px border into `groupshared float sharedDepth[24][24]`, read from shared memory instead of texture during horizon search.
- [ ] 5.2 Replace `cos()`/`sin()` in `GetRandomVector()` with `sincos()` intrinsic (single instruction on GCN/RDNA).
- [x] 5.3 Replace `acos()` calls in `FindMaxHorizonAngle()` with `acosFast()` polynomial approximation. Already done — `acosFast()` (Penner 2011) is used in all horizon search paths (gtao_cs.hlsl lines 366, 367, 391). No raw `acos()` calls remain in hot paths.
- [ ] 5.4 Investigate wave intrinsics (SM6.0): use `WaveReadLaneAt()` for cross-lane depth sharing within a wave, reducing shared memory pressure.
- [ ] 5.5 Profile each micro-optimization individually: measure before/after GPU time, keep only those with measurable improvement.
- [ ] 5.6 Build + visual verification: confirm no quality regression from approximations.

## Phase 6: Validation & Cleanup
- [ ] 6.1 Remove dead code: delete `GTAOParamsCBV` struct from `gtao_pass.h` (allocated but never bound).
- [ ] 6.2 Final GPU profile at all 4 quality presets — log results in this file.
- [ ] 6.3 Visual comparison grid: 4-cell screenshot grid (Low/Medium/High/Ultra) at default camera — verify quality progression.
- [ ] 6.4 Update `docs/specs/rendering/screen-space-effects.md` GTAO section with new quality presets, temporal pipeline, and performance characteristics.
- [ ] 6.5 Run full test suite to verify no regressions in other passes.
