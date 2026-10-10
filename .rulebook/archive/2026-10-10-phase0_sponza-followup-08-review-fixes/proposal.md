# Proposal: phase0_sponza-followup-08-review-fixes

## Why

An external static review (2026-09-08) of the Sponza-parity changes found seven defects and
several misleading claims that the earlier follow-ups had left in place. Each item was checked
against the code before acting; all seven are real.

1. `MaterialPipeline::BuildGBufferPipelineDesc` returned a descriptor whose `inputElements`
   pointed into a local `VertexLayoutResult` (elements stored inline) — dangling on the SM5
   (non-bindless) path. An earlier hoist fixed the other builders but not this one.
2. Masked material-graph materials applied a constant Opacity twice: `scene_sync` copies the
   graph constant into `cmd.opacity` (for the shadow / distance-field / translucent consumers) and
   `gbuffer_ps.hlsl` multiplied that by the graph's own opacity output (0.7 → 0.49).
3. `twoSided` reached the draw command (flag bit 3, `cmd.twoSided`) but the graph GBuffer PSO
   always culled back faces.
4. The DDGI writer treated a zero diffuse reflectance as "no data" and substituted 0.18 — black is
   a valid reflectance and the composite writes zero for fully metallic surfaces on purpose.
5. The Karis weight in `GatherSdfReflections` was described as "the correct estimator"; it is a
   variance-reducing bias that depends on the absolute radiance scale.
6. The simplified post chain runs with TAA off too; Bloom was given viewport dimensions while
   reading the render-resolution HDR (wrong when ScreenPercentage ≠ 100).
7. The tonemap pass returned on an invalid pipeline *after* `BeginRenderPass`.

Plus: comments and a guard script that still blamed bloom for the close-up wobble (the task
itself records that `r.Bloom=0` changed nothing), a `rev N` comment used to force a TU recompile,
a guard that validated dead code after an unconditional `return`, and "HDR-linear" wording for
tonemapped-PNG luminance comparisons in the DDGI parity task.

## What Changes

- `material_pipeline.{h,cpp}`: the standard vertex layout is a member (`m_standardLayout`);
  `BuildGBufferPipelineDesc` takes `twoSided` and sets `RHICullMode::None` for two-sided graphs
  (`twoSided` is already part of the graph content hash, so the PSO cache splits correctly).
- `gbuffer_ps.hlsl`: on the material-graph path the alpha test uses the graph's opacity alone.
- `ddgi_probe_update_cs.hlsl`: reflectance validity comes from the field's distance sentinel;
  black stays black. `test_ddgi_energy.ps1` guards it (17 checks).
- `frame_postprocess.cpp`: bloom dimensions follow its source (TAA output → viewport, HDR →
  render resolution); the tonemap pipeline is validated before the render pass opens; the bloom
  comment states that bloom was not the wobble's cause.
- `pipeline_lifecycle.cpp`: plain `#include "engine_shaders.h"` again (no `rev N` marker).
- `test_taa_bloom_input.ps1` rewritten against the live block only (13 checks).
- `sdf_reflections.hlsli` comment and followup-06 wording: Karis = biased mitigation; energy
  validation added to open item 4.1. followup-01: "HDR-linear" → display-referred proxy; 3.1 marked
  partial (no GPU ratio harness).

## Impact
- Affected specs: none
- Affected code: `runtime/engine/renderer/material/material_pipeline.{h,cpp}`,
  `runtime/shaders/runtime/gbuffer/gbuffer_ps.hlsl`, `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl`,
  `runtime/shaders/runtime/gi/sdf_reflections.hlsli`, `runtime/engine/renderer/frame/frame_postprocess.cpp`,
  `runtime/engine/renderer/core/pipeline/pipeline_lifecycle.cpp`, `scripts/rendering/test_taa_bloom_input.ps1`,
  `scripts/rendering/test_ddgi_energy.ps1`, Rulebook tasks followup-01/-06/-07 wording
- Breaking change: NO (Sponza ships no two-sided graph and one masked graph without a constant opacity, so its captures are unchanged)
- User benefit: no dangling pointer on the SM5 path, masked/two-sided graphs behave as authored, DDGI keeps dark surfaces dark, honest documentation
