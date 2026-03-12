# Rendering — Frame Pipeline

> **Status**: Planned (Milestone 2.0)
> **Source**: Spec in `docs/ENGINE.md` §23.12
> **Depends on**: Render World (§12-01), Visibility (§12-02), RHI (§03), Shader Pipeline (§05)

## Purpose

The complete per-frame rendering pipeline, from input polling through GPU submission to present. Defines the ordered sequence of all engine systems within a single frame.

## Full Frame Pipeline

```
Frame N:
    │
    ├── 1. Input Tick
    │      OS events → modifier chains → trigger evaluation → callbacks
    │
    ├── 2. Game Tick
    │      PrePhysics → Physics → PostPhysics → Update → PostUpdate
    │      Behaviors run, components update, events fire
    │
    ├── 3. Transform Propagation
    │      Dirty SceneComponents → recompute world matrices (parent→child)
    │
    ├── 4. Sync Point
    │      PrimitiveComponent → RenderProxy (create/update/destroy)
    │      CameraComponent → ViewState
    │
    ├── 5. Visibility
    │      Frustum cull (CPU) → GPU occlusion cull → LOD select
    │
    ├── 6. Shadow Pass
    │      Per shadow-casting light: frustum cull → shadow map render
    │
    ├── 7. GBuffer Pass (Deferred)
    │      Opaque geometry → Albedo, Normal, Metallic/Roughness, Emissive, Depth
    │
    ├── 8. Lighting Pass
    │      Read GBuffer + shadow maps → lit result
    │      GI: light probes / screen-space / DXR (future)
    │
    ├── 9. Transparent Pass (Forward)
    │      Back-to-front sorted, forward shading
    │
    ├── 10. Post-Processing
    │       TAA → SSAO → SSR → Bloom → DoF → Motion Blur → Tonemap → DLSS/FSR
    │
    └── 11. Present
            Swapchain present → start Frame N+1
```

## Deferred Rendering — GBuffer Layout

| Target | Format | Content |
|--------|--------|---------|
| GBuffer 0 | RGBA8 | Albedo (RGB) + AO (A) |
| GBuffer 1 | RGB10A2 | World Normal (RGB) + packed flags (A) |
| GBuffer 2 | RGBA8 | Metallic (R) + Roughness (G) + Emissive mask (B) + Material ID (A) |
| Depth | D32_FLOAT | Reversed-Z depth |

### Why Deferred

- Decouples geometry complexity from light count — 100 lights cost the same as 1 for opaque geometry
- GBuffer data is reused by SSAO, SSR, and other post-processing
- Forward shading is reserved for transparents only (which need sorted blending)

## Post-Processing Chain

Each effect reads from the previous output and writes to the next:

1. **TAA (Temporal Anti-Aliasing)** — jittered samples accumulated over frames, ghosting rejection
2. **SSAO (Screen-Space Ambient Occlusion)** — half-res compute, bilateral blur
3. **SSR (Screen-Space Reflections)** — hierarchical ray march against HZB
4. **Bloom** — downsample chain (mip pyramid) + Gaussian blur + upsample composite
5. **Depth of Field** — circle-of-confusion from depth, separated near/far blur
6. **Motion Blur** — per-pixel velocity from motion vectors
7. **Tonemap** — ACES filmic or customizable LUT, gamma correction
8. **Upscaling** — DLSS / FSR / native (passthrough)

## Implementation Phases

| Phase | What Renders |
|-------|-------------|
| **Phase 1 (MVP)** | Forward shading, single light, no post-processing |
| **Phase 2** | Deferred GBuffer, multiple lights, shadow maps, basic post (bloom + tonemap) |
| **Phase 3** | Full post chain (TAA, SSAO, SSR), GPU culling, HZB |
| **Phase 4+** | Virtual geometry, visibility buffer, DLSS/FSR, DXR GI |

## Rules

1. **ALWAYS use deferred for opaque, forward for transparent** — mixing causes double-lighting bugs
2. **ALWAYS use reversed-Z depth** — better precision at distance (1.0 = near, 0.0 = far)
3. **NEVER add post-processing effects without LOD/quality settings** — every effect must have Off/Low/Medium/High/Ultra
