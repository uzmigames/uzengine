## 1. Phase 1 — Immediate Shader/Parameter Fixes (30 min, HIGHEST impact)

- [x] 1.1 Fix GTAO AO Power: Changed to 0.7 (compromise: 1.0 caused visible GTAO dithering noise). `runtime/shaders/runtime/postprocess/gtao_cs.hlsl:107` — change `#define GTAO_AO_POWER 0.3f` to `#define GTAO_AO_POWER 1.0f`. Reference: Jimenez 2016 "GTAO" — AmbientOcclusionPower=2.0*0.5=1.0. Verification: AO darkening under sphere contacts must be visibly stronger (pow(0.5,1.0)=0.5 vs current pow(0.5,0.3)=0.81).
- [x] 1.2 Re-enabled micro-shadows with max(0.3) clamp to prevent GTAO dithering artifacts. `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:~1038-1040` — remove the stale "Micro-shadow disabled — causes visual artifacts" TODO comment. Replace with the micro-shadow formula: `float microShadow = saturate(NdotL + 2.0*ao*ao - 1.0);` applied as multiplier on direct light `shadow *= microShadow;`. The ao value comes from the GTAO texture already sampled at line ~960-968. Requires `gtaoSrvIndex != 0xFFFFFFFF` guard. Reference: Chan 2018 "Material Advances in Call of Duty: WWII".
- [x] 1.3 Fixed SSR MaxRoughness 0.4→0.8 in both trace and resolve passes. `runtime/engine/renderer/frame/frame_lighting.cpp` — find the SSR trace root constants struct (SsrTraceConstants) and locate the maxRoughness or equivalent field. Change from 0.4 to 0.8. UE5 default: `r.SSR.MaxRoughness=0.8`. Current 0.4 excludes roughness 0.4-0.8 surfaces (wood, stone, rough metal) from reflections entirely.
- [x] 1.4 PCF over-blur correction tested — too aggressive with receiverBias=1.0, kept disabled. `runtime/shaders/runtime/include/shadow_common.hlsli:~425` — uncomment the line `// shadow = shadow * shadow;` which applies Square(Shadow) PCF over-blur correction. Reference: ShadowProjectionPixelShader.usf:89-92. This sharpens soft PCF edges for more defined shadow boundaries.
- [x] 1.5 Clean build verified. Screenshots at default position captured.
- [x] 1.6 Visual verification at z=700 — exposure balanced after bias=0.7 fix.

## 2. Phase 2 — Parameter Calibration Verification (15 min)

- [x] 2.1 GTAO_AO_INTENSITY kept at 0.5 — balanced with POWER=0.7. `gtao_cs.hlsl:99` — current value is 0.5. After AO_POWER fix to 1.0, AO will be stronger. If AO is too aggressive (visibility near 0 for flat surfaces), reduce GTAO_AO_INTENSITY from 0.5 to 0.4. Reference default INTENSITY=1.0 but combined with POWER=1.0 and our scene lighting, 0.4-0.5 should be balanced.
- [x] 2.2 SkyLight intensity adjusted to 0.8/0.8, lowerHemisphere brightened. `runtime/demos/pbr_test/pbr_test.cpp:~370` — current intensity=0.6, indirect=0.7. After GTAO power fix, AO will darken contacts more. The ambient may now be at the right level. If scene looks too dark in shadow areas, increase intensity to 0.7-0.8. If still washed, keep 0.6.
- [x] 2.3 AutoExposure bias=0.7, maxBrightness=2.0 — z=700 view now properly exposed. `runtime/engine/renderer/passes/postprocess/auto_exposure.cpp:31` — current bias=0.3. After all fixes, the scene luminance distribution changes. Capture histogram (log output) and verify bias produces correct 18% middle gray targeting. Adjust if needed.
- [x] 2.4 Visual verification at default + z=700 — both properly exposed with contrast.

## 3. Phase 3 — SSR Quality Improvements (2-4 hours)

- [x] 3.1 Wired Hi-Z mip 0 to SSR trace with fallback to raw depth.
- [x] 3.2 Verified Hi-Z SRV availability check (m_hizMipCount > 0 && srvIndex != UINT32_MAX).
- [x] 3.3 Add SSR temporal accumulation: full pipeline implemented. New `ssr_temporal_cs.hlsl` compute shader with motion-vector reprojection + 3x3 neighborhood variance clipping (Schied 2017 / Salvi 2016 / Stachowiak 2015). Blend alpha 0.05 for stable pixels, 1.0 for disoccluded. Pass registered after SSR_Resolve in `frame_lighting.cpp`. Pipeline wired in `pipeline_library.h`, `pipeline_postprocess.cpp`, `pipeline_lifecycle.cpp`. Lighting shader now reads from temporal history instead of raw resolve output (`frame_setup.cpp` redirected).
- [x] 3.4 Add SSR temporal history texture creation in `scene_lifecycle.cpp` (next to existing SSR texture creation). Two RGBA16F textures (SSR_History_0/1) with ping-pong, matching TAA pattern. Destroy in DestroySsrResources(). Members added to `scene_renderer.h`.
- [x] 3.5 Visual verification: SSR temporal running, no crashes, reflections stable. Chrome sphere should reflect scene. Checker floor should show sphere reflections. Roughness 0.5-0.7 surfaces should show blurred reflections (not black).

## 4. Phase 4 — Cloud Pipeline Fix (4-8 hours)

- [x] 4.1 Identified: scene_render_frame.cpp:66-88. WaitIdle required to prevent DEVICE REMOVED. find where `CloudPass::Execute()` or equivalent is called with `WaitIdle()` or `Flush()`. This is the GPU drain point.
- [ ] 4.2 DEFERRED: Cloud render graph migration requires careful barrier work to avoid DEVICE REMOVED. register cloud compute as a proper `rgBuilder.AddPass("Cloud_Raymarch", ...)` with declared resource dependencies (depth SRV read, cloud output UAV write).
- [ ] 4.3 Remove WaitIdle: after render graph integration, the cloud compute runs as part of the normal command list submission without explicit GPU synchronization.
- [ ] 4.4 Verify async compute compatibility: if cloud compute can overlap with shadow rendering or GBuffer fill, mark the pass as async-capable in the render graph.
- [ ] 4.5 Performance verification: measure frame time before/after. The WaitIdle removal should reduce frame time by eliminating the full GPU drain. Target: at least 10% frame time improvement on scenes with clouds.

## 6. Unit Tests — CVar System + Render Pass Utilities

- [x] 6.1 `tests/engine/core/test_cvar.cpp` — 39 tests: RegisterFloat/Bool/Int, Get typed accessors, Set clamping (above/below/at boundaries), Set unknown no-op, Find existing/nonexistent (mutable + const), GetFloat/GetInt/GetBool missing-CVar fallbacks, Reset single/missing/default-field-unchanged, ResetAll 3-CVar round-trip, GetAll contains registered names, DuplicateRegistration idempotent (same pointer + value unchanged + after-set preserved), default clamped at registration (above max, below min), pointer stability after rehash, UZ_CVAR_FLOAT/BOOL/INT macro registration. All pass.
- [x] 6.2 `tests/engine/renderer/test_fsr_pass.cpp` — 16 tests: FsrRcasCon packs exp2(-sharpness) for 0.0/0.2/1.0/2.0, clamps above-2/below-0 inputs, monotonically decreasing attenuation, zeros out con0[1-3]; FSRInput value-initialization (zero fields, false enableRCAS), manual field roundtrip; r.FSR and r.FSR.Sharpness CVars exist with correct defaults (false/0.0, range [0,1]/[0,2]); FSRPass default-constructed IsInitialized=false. All pass.
- [x] 6.3 `tests/engine/renderer/test_smaa_pass.cpp` — 10 tests: r.SMAA CVar exists, stored as 0.0 (false), clamped to [0,1], GetBool=false; SMAASettings default enabled=false, writable; SMAAPass default-constructed IsInitialized=false, settings.enabled=false, safe destruction. All pass.
- [x] 6.4 `tests/engine/renderer/test_fsr2_upscaler.cpp` — 25 tests: GetType=FSR2, GetName non-empty/contains "FSR2", GetMinScreenPercentage positive/<max/≈33%, GetMaxScreenPercentage=100, screen-percentage range in valid bounds; UpscalerType FSR2 distinct from None/TAA/TSR/DLSS; default construct/Shutdown/Shutdown-twice/OnResize-before-init all safe; ITemporalUpscaler interface contract; stub-build CVar absence (r.FSR2/Quality/Sharpness not in registry), GetFloat/GetBool/GetInt return provided defaults. All pass.

## 5. Testing

- [x] 5.1 Visual regression: screenshots captured at each phase (phase1_v1-v5, phase3_hiz, ssr_temporal, final_default/z700/z1500).
- [x] 5.2 AO validation: GTAO_AO_POWER=0.7 produces visible contact darkening without noise. Flat surfaces stay bright.
- [x] 5.3 Micro-shadow validation: enabled with max(0.3) clamp. No visible dithering artifacts.
- [x] 5.4 SSR validation: SSR trace+resolve+temporal pipeline running. MaxRoughness=0.8 covers most materials.
- [x] 5.5 Shadow validation: PCF over-blur tested but too aggressive, kept disabled. Shadows still well-defined via Poisson 16-tap.
- [x] 5.6 No rendering artifacts: no crashes, no shadow acne, no exposure flashing across all test positions.
- [x] 5.7 Multi-position verification: default, z=700, z=1500 all render correctly with proper exposure.
- [x] 5.8 Performance: ~28 FPS at 1080p on RTX 4090 (acceptable — clouds + SSR temporal add cost).

## 5b. Per-Material Specular Parameter (desaturation fix)

- [x] 5b.1 Add `specular` and `envMultiplier` to MaterialInterface/Material/MaterialInstance. specular (0-1, default 0.5) controls F0=0.08*specular. envMultiplier (0-2, default 1.0) controls per-material IBL intensity. Both packed into materialFlags bits[8:15]/[16:23].
- [x] 5b.2 GBuffer PS unpacks specular from materialFlags, writes to RT3.a (emissive alpha channel).
- [x] 5b.3 Deferred lighting PS reads specular from RT3.a, uses F0=0.08*specular instead of hardcoded 0.04. Removed `specDielectricScale` hack and `metallic=0.0` DIAG line.
- [x] 5b.4 fbx_test mushroom set to specular=0.25 (F0=0.02, organic material).

## 5c. Auto-Exposure Stability + Shadow Strength Fix

- [x] 5c.1 Fixed auto-exposure oscillation causing gold sphere flickering: MinBrightness 0.3→0.1, MaxBrightness 0.6→4.0 (40x dynamic range vs 2x). Root cause: 2x exposure range caused rapid oscillation when bright metallic specular shifted avg luminance.
- [x] 5c.2 Fixed adaptation speeds to match reference: SpeedUp 1.5→3.0, SpeedDown 0.7→1.0 (exposure_average_cs.hlsl). Faster adaptation prevents visible exposure hunting.
- [x] 5c.3 Added exposure bias +1.0 EV to compensate for bright sky dominating histogram with wider range. Without bias, widened range over-darkened ground-level objects.
- [x] 5c.4 Increased shadow ambient floor: skyLightMinAmbient 0.15→0.25 (frame_setup.cpp). Shadow regions now have visible ambient fill instead of near-black.
- [x] 5c.5 Visual verification: gold sphere consistent across 3 camera positions, FBX mushroom stable, shadows softer.

## 6. Documentation

- [x] 6.1 Analysis docs committed (11 files, 6800+ lines in docs/analysis/renderer/).
- [x] 6.2 Task progress tracked in tasks.md throughout session.
- [ ] 6.3 Update CVar documentation: document all changed default values with before/after and rationale.
