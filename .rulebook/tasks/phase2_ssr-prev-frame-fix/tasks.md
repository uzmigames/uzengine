# SSR Previous-Frame Color Fix — Eliminate Temporal Flickering

## Problem
SSR reads current frame's `m_hdrSceneColor` which already contains SSR from the previous frame (via temporal accumulation → deferred lighting blend). This creates a feedback loop that causes visible trembling/flickering on all reflective surfaces.

## Root Cause (from UE5 analysis)
UE5 reads the **previous frame's TAA-resolved output** as SSR scene color input, NOT the current frame's HDR buffer. This breaks the feedback loop because the TAA output is temporally stable.

## Solution
Copy `m_hdrSceneColor` at end-of-frame (after lighting, before SSR next frame) into a dedicated `m_ssrSceneColorPrev` texture. SSR trace reads from this copy instead of the live HDR buffer.

## Checklist

### Phase 1: Previous Frame Color Buffer
- [ ] 1.1 Add `m_ssrSceneColorPrev` texture (RGBA16F, full res) + SRV to `scene_renderer_impl.h`
- [ ] 1.2 Create/destroy in `scene_temporal_resources.cpp` alongside existing SSR resources
- [ ] 1.3 At end of frame (after lighting+atmosphere, before postprocess), copy `m_hdrSceneColor` → `m_ssrSceneColorPrev`
- [ ] 1.4 Wire `m_ssrSceneColorPrevSrv` as `sceneColorSrvIdx` in SSR trace dispatch (`frame_gi_passes.cpp`)
- [ ] 1.5 Gate on `m_ssrHasPrevColor` flag (false on frame 0 → skip SSR, true from frame 1+)

### Phase 2: Stabilize Trace
- [ ] 2.1 Verify IGN jitter is purely spatial (no `timeParams` dependency) — DONE in earlier commit
- [ ] 2.2 MaxRoughness CVar: update hardcoded `-2.0/0.8` in `frame_gi_passes.cpp` to use `cv_SSRMaxRoughness->value`

### Phase 3: Re-enable and Test
- [ ] 3.1 Set `r.SSR` default back to `true` in all 3 locations
- [ ] 3.2 Capture damaged_helmet at 3 camera angles — verify zero flickering
- [ ] 3.3 Capture material_showcase — verify SSR adds ground reflections at sphere edges
- [ ] 3.4 Remove duplicate `r.SSR` CVar from `frame_postprocess.cpp` (keep only `frame_lighting.cpp`)

## References
- UE5: `ScreenSpaceReflections.usf`, reads `PrevViewInfo.TemporalAAHistory.RT[0]`
- UE5: `DeferredShadingRenderer.cpp:3502` — `QueueTextureExtraction(SceneTextures.Color.Resolve, &PrevFrameViewInfo.ScreenSpaceRayTracingInput)`
- Stachowiak 2015 "Stochastic Screen-Space Reflections"
- Karis 2014 "High Quality Temporal Supersampling"
