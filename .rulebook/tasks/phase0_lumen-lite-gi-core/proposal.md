# Proposal: phase0_lumen-lite-gi-core

Source: docs/analysis/lighting/uzengine/lumen-lite-correction-plan.md (2026-07-26)

## Why

Sponza global illumination is visually broken vs the Intel Sponza reference. `--diag` on 2026-07-26 proved that **no GI source runs by default**: `r.SSGI`, `r.GTAO`, `r.RadianceCache`, `r.RayTracing.GI`, and `r.BrixelizerGI` all default off, the scene never enables them, and the `InteriorProbeVolume` entity was removed from the scene. The only active indirect is sky-SH ambient multiplied by a sun-shadow heuristic — spatially constant, no bounce, no contact occlusion.

Additionally, both world-space probe tracers (`radiance_cache_trace_cs.hlsl`, `ddgi_probe_update_cs.hlsl`) have a physics bug at ray hit points: they return `sunColor * NdL(-rayDir) * constantGroundAlbedo` with **no shadow test, a fake normal, constant albedo, and no multi-bounce**. This injects unshadowed sunlight into enclosed interiors (light leak) and makes bounce achromatic. The user's goal is "realistic lighting as close to Lumen as possible without copying Unreal code" — Lumen's core insight is that ray hits must return cached **lit** surface radiance; ours return a constant.

## What Changes

Executes Phases 0–3 of the lumen-lite correction plan (Phase 4 screen-probe gather and Phase 5 stochastic SSR are follow-up tasks):

1. **Reactivation + diag hygiene** — enable SSGI + GTAO for Sponza; register GI diag targets unconditionally (status `SKIPPED` when the pass is off) so disabled passes are visible; fix the `gbuffer.ao` diag target aliasing `gbuffer.albedo` stats; fix the D3D12MA shutdown assertion (allocations not freed on destruction); verify motion vectors + TAA history under camera motion; archive the already-implemented `phase1_fix-sponza-lighting-calibration` task.
2. **Ground-truth harness** — path-traced golden captures of Sponza at 3 poses + documented PSNR comparison loop; baseline numbers recorded in the plan doc.
3. **Surface-cache-lite hit lighting** (the core) — in both probe tracers, via a shared `gi_hit_lighting.hlsli`: real normals (GDF central-difference gradient / DXR triangle geometric normal), sun occlusion ray at the hit (GDF sphere-trace / second RayQuery), per-instance average albedo+emissive table (StructuredBuffer built from material constants at load), and multi-bounce feedback (sample previous-frame probe grid at the hit, per Majercik 2019 "infinite bounces"). Enable `r.RadianceCache=1` for Sponza and `r.RayTracing.GI=1` on DXR-capable hardware.
4. **Composite correctness** — SSGI blends into the indirect cascade by screen-validity (currently ADDED on top of probe/sky irradiance — double count); stop tinting scene-radiance GI sources by `skyTintDiffuse`; GTAO applied once to the unified indirect diffuse; irradiance units audited and documented.

All shader math cited via `@ref` to published papers (Majercik 2019/2021, McGuire 2017, Ramamoorthi & Hanrahan 2001, Jimenez 2016, Heitz 2018, Fdez-Aguera 2019). No Unreal source is copied — architecture concepts only (Wright & Narkowicz GDC 2022).

## Impact
- Affected specs: docs/specs/rendering/gi-system.md, docs/specs/rendering/ibl-pipeline.md
- Affected code: runtime/shaders/runtime/gi/ (probe tracers + new shared hlsli), runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl, runtime/engine/renderer/gi/ (radiance_cache, ddgi_system, ssgi_renderer), runtime/engine/renderer/diag/ (pass inspector targets), runtime/engine/renderer/frame/frame_constants.cpp, sample/content/scenes/sponza.uzscene, D3D12MA shutdown path
- Breaking change: NO — CVar-gated, additive; existing scenes without the CVars keep current behavior
- User benefit: Sponza (and any interior scene) receives colored, shadowed, multi-bounce indirect lighting approaching the Intel reference; disabled GI passes become visible in diagnostics instead of silently absent
