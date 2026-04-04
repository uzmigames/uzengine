# Proposal: phase2_ssr-prev-frame-fix

## Why
SSR was causing visible temporal flickering/trembling on all reflective surfaces. Root cause: the SSR trace shader reads the current frame's HDR scene color buffer, which already contains SSR from the previous frame (via temporal accumulation blended in deferred lighting). This creates a feedback loop where SSR amplifies its own output frame-over-frame, causing instability.

UE5 breaks this loop by reading the **previous frame's TAA-resolved output** as SSR input instead of the live HDR buffer. The previous frame's output is temporally stable (already filtered by TAA), so SSR produces consistent results without feedback.

## What Changes
- New `m_ssrPrevSceneColor` texture (RGBA16F, full render resolution) with SRV, created/destroyed alongside existing SSR resources in `scene_temporal_resources.cpp`
- `SSR_CopyPrevColor` render graph pass copies `m_hdrSceneColor` to `m_ssrPrevSceneColor` at end of `RegisterLightingPasses()` (after deferred lighting + atmosphere)
- SSR trace dispatch reads `m_ssrPrevSceneColorSrv` instead of `m_hdrSceneColorSrv`
- `m_ssrHasPrevColor` gate prevents SSR on frame 0 (no valid previous data)
- SSR trace uses purely spatial IGN jitter (no temporal frame hash variation)
- SSR temporal pass restored to stable settings (alpha=0.05, simple min/max AABB)
- MaxRoughness CVar wired to dispatch constant (was hardcoded 0.8, now reads CVar)
- Bloom threshold debug bypass fixed (was writing raw color instead of color*scale)
- Bloom defaults changed to UE5 parity: threshold=-1 (uniform), intensity=0.04

## Impact
- Affected specs: `docs/specs/screen-space-effects.md`
- Affected code: `scene_renderer_impl.h`, `scene_temporal_resources.cpp`, `frame_lighting.cpp`, `frame_gi_passes.cpp`, `frame_postprocess.cpp`, `scene_renderer_init.cpp`, `bloom_pass.cpp`, `bloom_threshold_cs.hlsl`, `ssr_trace_cs.hlsl`, `ssr_temporal_cs.hlsl`, `deferred_lighting_ps.hlsl`
- Breaking change: NO — SSR produces same visual result but without flickering
- User benefit: Stable reflections on metallic surfaces, no temporal instability
