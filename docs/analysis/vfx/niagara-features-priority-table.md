<!-- doc-meta
type: reference
subsystem: vfx
tags: [particles, priority, roadmap]
created: 2026-03-30
-->

# Niagara Missing Features — Priority Quick Reference

## Top 15 Features by Impact × Feasibility

| Rank | Feature | Type | Hours | Impact | Feasible | Tier | Key Use Cases |
|------|---------|------|-------|--------|----------|------|---------------|
| 1 | **Mesh Renderer** | Renderer | 40–50 | ★★★★★ | ★★★★☆ | P0 | Chunks, swarms, explosions, mechanical FX |
| 2 | **Light Renderer** | Renderer | 35–45 | ★★★★★ | ★★★★☆ | P0 | Magic, fire, neon, dynamic environment |
| 3 | **Decal Renderer** | Renderer | 30–40 | ★★★★☆ | ★★★★☆ | P0 | Splats, bullet holes, scorch, blood |
| 4 | **Skeletal Mesh DI** | Data Interface | 50–70 | ★★★★☆ | ★★★☆☆ | P1 | Character FX, blood, equipment trails |
| 5 | **Audio Spectrum DI** | Data Interface | 35–50 | ★★★★☆ | ★★★☆☆ | P1 | Music reactivity, beat sync, visualizers |
| 6 | **Neighbor Grid 3D** | Data Interface | 40–60 | ★★★★☆ | ★★★☆☆ | P1 | Flocking, collision avoidance, N-body |
| 7 | **Vector Field DI** | Data Interface | 20–30 | ★★★☆☆ | ★★★★★ | P1 | Smoke flow, wind drift, whirlpools |
| 8 | **Landscape DI** | Data Interface | 30–45 | ★★★☆☆ | ★★★☆☆ | P1 | Dust clouds, impact effects, terrain-aware |
| 9 | **Render Target 2D/3D DI** | Data Interface | 40–50 | ★★★★☆ | ★★★☆☆ | P1 | Painting, feedback loops, texture-based sim |
| 10 | **Camera DI + Distance Sort** | Data Interface | 20–30 | ★★★☆☆ | ★★★★★ | P2 | Translucent sorting, screen-space calc |
| 11 | **Async GPU Trace** | Data Interface | 35–50 | ★★★☆☆ | ★★☆☆☆ | P2 | GPU collision, ray-terrain, ray-mesh |
| 12 | **Sparse Volume Texture** | Data Interface | 25–40 | ★★★☆☆ | ★★★☆☆ | P2 | Baked volumetric, clouds, density fields |
| 13 | **Dynamic Material Params** | Binding | 20–30 | ★★★★☆ | ★★★★★ | P1 | Glow, dissolve, vertex deformation |
| 14 | **Component Renderer** | Renderer | 45–70 | ★★★☆☆ | ★★☆☆☆ | P2 | Custom entities, gameplay integration |
| 15 | **Collision Query DI** | Data Interface | 40–60 | ★★☆☆☆ | ★★☆☆☆ | P3 | General physics collision, scene geometry |

## Estimated Project Timeline

### Phase 4a (2.5 weeks, P0 only)
- Mesh Renderer
- Light Renderer  
- Dynamic Material Parameters
- **Total: 85–95 hours**

### Phase 4b (2–3 weeks, quick wins)
- Vector Field DI
- Camera DI
- Landscape DI
- **Total: 70–105 hours**

### Phase 4c (4–6 weeks, advanced features)
- Decal Renderer
- Audio Spectrum DI
- Skeletal Mesh DI
- Neighbor Grid 3D
- **Total: 155–225 hours**

### Phase 4d (6–10 weeks, nice-to-have)
- Render Target DI
- Async GPU Trace
- Sparse Volume Texture
- Component Renderer
- Collision Query DI
- **Total: 185–270 hours**

**Grand Total: 495–695 hours (12–17 weeks FT)**

## Implementation Dependencies

```
Phase 4a (foundation)
├── Mesh Renderer (required for complex geometry)
├── Light Renderer (required for dynamic lighting)
└── Dynamic Material Parameters (multiplies material expressiveness)

Phase 4b (quick wins)
├── Vector Field DI (simple texture sampling)
├── Camera DI (sorting foundation)
└── Landscape DI (environment integration)

Phase 4c (advanced)
├── Decal Renderer (requires camera sorting from 4b)
├── Audio Spectrum DI (independent audio pipeline)
├── Skeletal Mesh DI (requires component binding system)
└── Neighbor Grid 3D (requires spatial hashing infrastructure)

Phase 4d (optional)
├── Render Target DI (feedback loop infrastructure)
├── Async GPU Trace (requires DXR or conservative SDF)
├── Sparse Volume Texture (requires virtual texture system)
├── Component Renderer (requires component pool)
└── Collision Query DI (requires physics API bindings)
```

## What Remains MISSING (Post-Phase 4)

After Phase 4d, these UE5 Niagara features would still be unimplemented:

1. **Stateless Niagara** (event-driven, no persistent state)
2. **Multiple Emitter LOD Scalability** (performance-based quality levels)
3. **Niagara Baker System** (bake simulations to disk, streaming)
4. **Sim Cache System** (record/playback simulations)
5. **GPU Readback/Feedback** (readback particle data to CPU for analysis)
6. **Bounds Calculation** (auto AABB from particle positions)
7. **Warmup/Deterministic Simulation** (prime the sim with N frames instantly)
8. **Advanced Modules**: Spring forces, turbulence with octave, point force, line attraction, surface conforming
9. **Global Distance Field Integration** (query world SDF)
10. **Temporal/Frame-Rate-Dependent Effects** (motion blur, TAA interaction)

These are lower-priority but would complete the Niagara feature parity.

## Quick-Start Implementation Order

**If you only have time for 3 features (highest impact immediately)**:
1. ✅ **Mesh Renderer** — visual quality jump
2. ✅ **Light Renderer** — dynamic lighting magic
3. ✅ **Decal Renderer** — surface effects completeness

**If you have 2 weeks**:
Above + Vector Field DI + Dynamic Material Parameters

**If you have 1 month**:
Above + Skeletal Mesh DI + Landscape DI + Audio Spectrum DI

**If you have 3+ months**:
All of Phase 4a–c, start 4d based on use cases
