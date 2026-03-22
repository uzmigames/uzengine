## 1. Phase 1 — Immediate Shader/Parameter Fixes (30 min, HIGHEST impact)

- [ ] 1.1 Fix GTAO AO Power: `runtime/shaders/runtime/postprocess/gtao_cs.hlsl:107` — change `#define GTAO_AO_POWER 0.3f` to `#define GTAO_AO_POWER 1.0f`. Reference: Jimenez 2016 "GTAO" — AmbientOcclusionPower=2.0*0.5=1.0. Verification: AO darkening under sphere contacts must be visibly stronger (pow(0.5,1.0)=0.5 vs current pow(0.5,0.3)=0.81).
- [ ] 1.2 Re-enable micro-shadows: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:~1038-1040` — remove the stale "Micro-shadow disabled — causes visual artifacts" TODO comment. Replace with the micro-shadow formula: `float microShadow = saturate(NdotL + 2.0*ao*ao - 1.0);` applied as multiplier on direct light `shadow *= microShadow;`. The ao value comes from the GTAO texture already sampled at line ~960-968. Requires `gtaoSrvIndex != 0xFFFFFFFF` guard. Reference: Chan 2018 "Material Advances in Call of Duty: WWII".
- [ ] 1.3 Fix SSR MaxRoughness: `runtime/engine/renderer/frame/frame_lighting.cpp` — find the SSR trace root constants struct (SsrTraceConstants) and locate the maxRoughness or equivalent field. Change from 0.4 to 0.8. UE5 default: `r.SSR.MaxRoughness=0.8`. Current 0.4 excludes roughness 0.4-0.8 surfaces (wood, stone, rough metal) from reflections entirely.
- [ ] 1.4 Enable PCF over-blur correction: `runtime/shaders/runtime/include/shadow_common.hlsli:~425` — uncomment the line `// shadow = shadow * shadow;` which applies Square(Shadow) PCF over-blur correction. Reference: ShadowProjectionPixelShader.usf:89-92. This sharpens soft PCF edges for more defined shadow boundaries.
- [ ] 1.5 Clean build (runtime + demo) and visual verification at default camera position. Compare before/after screenshots.
- [ ] 1.6 Visual verification at --camera-z 700 (higher altitude, tests AO distance fade and shadow cascade blending).

## 2. Phase 2 — Parameter Calibration Verification (15 min)

- [ ] 2.1 Verify GTAO_AO_INTENSITY balance: `gtao_cs.hlsl:99` — current value is 0.5. After AO_POWER fix to 1.0, AO will be stronger. If AO is too aggressive (visibility near 0 for flat surfaces), reduce GTAO_AO_INTENSITY from 0.5 to 0.4. Reference default INTENSITY=1.0 but combined with POWER=1.0 and our scene lighting, 0.4-0.5 should be balanced.
- [ ] 2.2 Verify SkyLight intensity balance: `runtime/demos/pbr_test/pbr_test.cpp:~370` — current intensity=0.6, indirect=0.7. After GTAO power fix, AO will darken contacts more. The ambient may now be at the right level. If scene looks too dark in shadow areas, increase intensity to 0.7-0.8. If still washed, keep 0.6.
- [ ] 2.3 Verify AutoExposure bias: `runtime/engine/renderer/passes/postprocess/auto_exposure.cpp:31` — current bias=0.3. After all fixes, the scene luminance distribution changes. Capture histogram (log output) and verify bias produces correct 18% middle gray targeting. Adjust if needed.
- [ ] 2.4 Visual verification: capture screenshots at 3 positions (default, z=700, z=1500). All must show proper contrast, visible AO, visible shadows, no over/under exposure.

## 3. Phase 3 — SSR Quality Improvements (2-4 hours)

- [ ] 3.1 Wire Hi-Z mip chain to SSR trace: `runtime/engine/renderer/frame/frame_lighting.cpp:~600-640` — the SsrTraceConstants currently passes raw `depthSrvIdx` (the full-res depth buffer). The Hi-Z pyramid already exists (`hiz_pyramid_pass.h`, creates min-depth mip chain). Change SSR trace to use the Hi-Z SRV index instead of raw depth. This allows the trace shader to sample coarser mip levels for distant samples, increasing effective trace range without more samples.
- [ ] 3.2 Verify Hi-Z SRV availability: check that `m_hizMipCount > 0` and `m_hizTextureSrv != UINT32_MAX` before wiring. If Hi-Z is not available, fall back to raw depth (current behavior).
- [ ] 3.3 Add SSR temporal accumulation: create a history texture pair (RGBA16F, same resolution as SSR output). After SSR resolve, blend current frame with history: `result = lerp(history, current, 0.1)`. Use motion vectors to reproject history. This smooths out single-frame noise from the stochastic trace. Pattern: follow TAA history ping-pong in `frame_postprocess.cpp:132-199`.
- [ ] 3.4 Add SSR temporal history texture creation in `scene_lifecycle.cpp` (next to existing SSR texture creation). Two RGBA16F textures, same size as SSR resolve output.
- [ ] 3.5 Visual verification: metallic and semi-rough surfaces must show stable, noise-free reflections. Chrome sphere should reflect scene. Checker floor should show sphere reflections. Roughness 0.5-0.7 surfaces should show blurred reflections (not black).

## 4. Phase 4 — Cloud Pipeline Fix (4-8 hours)

- [ ] 4.1 Identify cloud compute dispatch location: find where `CloudPass::Execute()` or equivalent is called with `WaitIdle()` or `Flush()`. This is the GPU drain point.
- [ ] 4.2 Create render graph pass for cloud compute: register cloud compute as a proper `rgBuilder.AddPass("Cloud_Raymarch", ...)` with declared resource dependencies (depth SRV read, cloud output UAV write).
- [ ] 4.3 Remove WaitIdle: after render graph integration, the cloud compute runs as part of the normal command list submission without explicit GPU synchronization.
- [ ] 4.4 Verify async compute compatibility: if cloud compute can overlap with shadow rendering or GBuffer fill, mark the pass as async-capable in the render graph.
- [ ] 4.5 Performance verification: measure frame time before/after. The WaitIdle removal should reduce frame time by eliminating the full GPU drain. Target: at least 10% frame time improvement on scenes with clouds.

## 5. Testing

- [ ] 5.1 Visual regression: capture before/after screenshots for EACH phase. Store in `.screenshots/` with descriptive names (e.g., `phase1_before.png`, `phase1_after.png`).
- [ ] 5.2 AO validation: with GTAO_AO_POWER=1.0, verify sphere-ground contact shows visible darkening. Verify flat surfaces (floor) don't go fully black (AO should be ~0.85-0.95 for flat).
- [ ] 5.3 Micro-shadow validation: with micro-shadow enabled, verify textured surfaces (stone, brick, rattan) show subtle contact darkening in surface crevices when NdotL is low.
- [ ] 5.4 SSR validation: verify chrome sphere reflects nearby spheres and floor. Verify checker floor reflects sphere silhouettes. Verify rough surfaces (roughness=0.6) show blurred reflections, not black.
- [ ] 5.5 Shadow validation: with PCF over-blur correction, verify shadow edges are more defined than before. Compare cascade 0 (near) and cascade 3 (far) shadow quality.
- [ ] 5.6 No rendering artifacts: verify no new shadow acne, no SSR ghosting, no AO halo artifacts, no exposure flashing.
- [ ] 5.7 Multi-position verification: screenshots at default, --camera-z 700, --camera-z 1500 must all look correct.
- [ ] 5.8 Performance: frame rate must not drop below 30 FPS at 1080p on RTX 4090 after all changes.

## 6. Documentation

- [ ] 6.1 Update `docs/analysis/renderer/strategic-render-plan.md` with completed items and remaining work.
- [ ] 6.2 Add session summary to `.rulebook/PLANS.md` documenting what was changed and why.
- [ ] 6.3 Update CVar documentation: document all changed default values with before/after and rationale.
