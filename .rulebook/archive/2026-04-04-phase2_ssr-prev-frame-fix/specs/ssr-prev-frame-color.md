# Spec: SSR Previous-Frame Color Input

## Context
Screen-space reflections read a scene color buffer to fetch the color at ray hit points. Reading the current frame's HDR buffer creates a feedback loop because that buffer already contains SSR from the previous frame.

## Given/When/Then

### Scenario: SSR reads previous frame color
**Given** the SSR trace pass is dispatched  
**When** the trace shader samples `sceneColorSrvIdx`  
**Then** the SRV must point to `m_ssrPrevSceneColor` (copy of previous frame's post-lighting HDR), NOT `m_hdrSceneColor` (live buffer)

### Scenario: First frame has no previous color
**Given** the engine starts or resizes  
**When** `m_ssrHasPrevColor` is false  
**Then** SSR passes are skipped entirely (no SSR output until frame 1)

### Scenario: Previous color is copied at end of lighting
**Given** deferred lighting and atmosphere passes have completed  
**When** `SSR_CopyPrevColor` pass executes  
**Then** `m_hdrSceneColor` is copied to `m_ssrPrevSceneColor` via `CopyTexture()` with proper barrier transitions (RenderTarget→CopySrc, ShaderResource→CopyDst, then restore)

### Scenario: SSR trace jitter is deterministic
**Given** the SSR trace computes ray march step offset  
**When** IGN jitter is computed  
**Then** the jitter depends only on pixel position `IGN(DTid.xy)`, NOT on `timeParams` or frame index

### Scenario: Bloom does not amplify specular hotspots
**Given** bloom post-processing is active  
**When** bloom threshold is -1 (uniform mode)  
**Then** all HDR pixels contribute equally to bloom (no selective extraction of bright specular dots)

## References
- UE5 `DeferredShadingRenderer.cpp:3502` — `QueueTextureExtraction(SceneTextures.Color.Resolve, &PrevFrameViewInfo.ScreenSpaceRayTracingInput)`
- UE5 `ScreenSpaceReflections.usf` — reads `PrevViewInfo.TemporalAAHistory.RT[0]`
- UE5 `Scene.cpp:423` — `BloomThreshold = -1.0f` (uniform bloom)
- Stachowiak 2015 "Stochastic Screen-Space Reflections"
- Karis 2014 "High Quality Temporal Supersampling"
