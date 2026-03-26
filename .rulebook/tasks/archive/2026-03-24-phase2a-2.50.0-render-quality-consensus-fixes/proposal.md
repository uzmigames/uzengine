# Proposal: Render Quality Consensus Fixes (Phase 2a)

## Why

11 independent specialist agents (UE5, Unity HDRP, Godot 4, CryEngine/Frostbite, shader expert, pipeline expert) analyzed UzEngine's renderer against industry standards. The unanimous finding: **algorithms are correct** (BRDF, IBL, shadows, tonemap all at parity with UE5/Unity/Godot), but **calibration errors and disabled features** cause poor visual quality.

Specific root causes identified with 100% agent consensus:

1. **GTAO_AO_POWER = 0.3** — Reference default is 1.0. Current value makes ambient occlusion nearly invisible (`pow(0.5, 0.3) = 0.81` vs `pow(0.5, 1.0) = 0.5`). Every surface looks flat.
2. **Micro-shadows disabled** by a stale TODO comment citing a "GTAO SRV conflict" that the pipeline reviewer confirmed is resolved. Without micro-shadows, surface detail has no contact darkening.
3. **SSR MaxRoughness = 0.4** — UE5/Unity/Godot all use 0.8. Surfaces with roughness 0.4-0.8 (wood, concrete, rough metal — most real-world materials) receive zero screen-space reflections.
4. **PCF over-blur correction disabled** — The `shadow * shadow` sharpening step (ShadowProjectionPixelShader.usf:89-92) is commented out, making shadows appear washed and undefined.
5. **SSR traces raw depth** instead of Hi-Z pyramid — causes shorter trace range and more artifacts at distance.
6. **Cloud compute runs outside render graph** with `WaitIdle()` — full GPU drain every frame, destroying frame pacing.

Analysis documents: `docs/analysis/renderer/consensus-analysis.md`, `docs/analysis/renderer/strategic-render-plan.md`, plus 5 expert reviews in `docs/analysis/renderer/review-*.md`.

## What Changes

### Shader Fixes (no new systems)
1. GTAO AO power correction — `gtao_cs.hlsl` constant change (0.3 → 1.0)
2. Micro-shadow re-enable — `deferred_lighting_ps.hlsl` remove stale TODO, add 2-line formula
3. PCF over-blur correction — `shadow_common.hlsli` uncomment `shadow * shadow`

### Pipeline Fixes
4. SSR MaxRoughness — `frame_lighting.cpp` constant change (0.4 → 0.8)
5. SSR Hi-Z wiring — wire existing Hi-Z pyramid SRV to SSR trace root constants
6. SSR temporal accumulation — add history blend using existing TAA infrastructure
7. Cloud render graph migration — move cloud compute into render graph, remove `WaitIdle()`

### Parameter Calibration
8. Verify exposure/skylight balance after GTAO fix (may need readjustment)

## Impact
- Affected specs: `docs/specs/rendering/screen-space-effects.md`
- Affected code: `runtime/shaders/runtime/postprocess/gtao_cs.hlsl`, `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`, `runtime/shaders/runtime/include/shadow_common.hlsli`, `runtime/engine/renderer/frame/frame_lighting.cpp`, `runtime/engine/renderer/frame/frame_postprocess.cpp`
- Breaking change: NO — all changes are calibration corrections or feature re-enablement
- User benefit: Dramatic visual quality improvement — proper AO, contact shadows, reflections on most materials, defined shadow edges
- Enables: Micro-shadow + GTAO visible = materials look grounded. SSR on rough surfaces = environment feels real.
