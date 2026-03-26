# VFX Advanced Modules, LOD, and Budget System

## Summary

Implement the advanced VFX features that bring the system to production quality: collision module (particle-world raycasts), light emission module (dynamic lights from particles), mesh render module (instanced 3D mesh particles), LOD system (distance + screen-size + budget-based), significance manager (priority-based emitter culling), and the full particle budget enforcement system. These features are what separates a tech demo from a shippable VFX system.

## Why

The core particle system (3.60-3.62) handles basic effects. Production games need:

1. **Collision**: Particles bouncing off walls, splashing in water, killing on ground impact
2. **Dynamic lights**: Fire emitting light, sparks illuminating surroundings
3. **Mesh particles**: 3D debris, snowflakes, leaves — not just billboards
4. **LOD**: Distant effects use fewer particles, saving GPU budget for close effects
5. **Budget enforcement**: Global limit prevents GPU overload when many effects are active simultaneously
6. **Significance manager**: When budget is tight, low-priority effects get culled first

## UE5 Niagara Reference (Clean-Room)

### Scalability System (FNiagaraSystemScalabilitySettings)
1. **Distance-based LOD**: Niagara reduces particle count based on distance-to-camera. Close = full quality, far = reduced spawn rate, very far = disabled.
2. **Screen-size culling**: If the emitter's bounding sphere covers fewer than N screen pixels, it's culled entirely.
3. **Significance manager**: Global ranking of all active Niagara systems. When budget exceeded, lowest-significance systems are disabled. Significance = priority × distance_score × visibility_score.
4. **Budget per quality level**: Different particle budgets for Low/Medium/High/Ultra quality settings.

### Collision Data Interface (NiagaraDataInterfaceCollisionQuery)
1. **GPU raycast**: Niagara uses depth buffer raycasts (screen-space) for cheap collision. Full physics raycasts are optional and expensive.
2. **Collision response**: Kill, Bounce (reflect velocity), Slide (project velocity onto surface plane).
3. **Depth buffer collision**: Sample scene depth buffer at particle screen position. If particle is behind depth, collision occurred. Cheap but limited to screen-visible geometry.

### Light Renderer
1. **Per-particle lights**: Each alive particle can emit a point light. Extremely expensive — limited to low particle counts (< 100 lights typically).
2. **Light clustering**: Niagara integrates with UE5's clustered forward lighting for efficient many-light rendering.

## Impact

- **New modules**: Collision, Light, MeshRender (3 new module implementations)
- **New systems**: LOD, significance manager, budget enforcement
- **Performance**: LOD system can reduce total particle count by 50-80% in typical scenes
- **Quality settings**: Particle budget scales with quality level

## Dependencies

- VFX Modules (3.61.0) — module base class and registry
- VFX Graph (3.62.0) — parameter system for LOD parameters
- GPU Particle Core (3.60.0) — particle budget allocation
- Depth buffer access (✅ exists) — for screen-space collision
- Light system (✅ exists) — for dynamic light emission
- Mesh instancing (✅ exists) — for mesh particle rendering

## Acceptance Criteria

1. CollisionModule: depth-buffer screen-space raycasts, Kill/Bounce/Slide responses, configurable bounce_factor
2. LightRenderModule: per-particle point lights with intensity_over_life curve, max 64 particle lights per emitter
3. MeshRenderModule: instanced mesh rendering (reuse GPU instancing system), configurable mesh/material/scale
4. LOD system: distance-based quality levels (Full/Reduced/Minimal/Culled) with configurable distance thresholds
5. Screen-size culling: emitters whose bounding sphere < min_screen_pixels are culled
6. Significance manager: ranks all active emitters by (priority × distance_score × visibility), culls lowest when over budget
7. Budget enforcement: global particle limit with per-quality-level scaling (Low=125K, Medium=250K, High=500K, Ultra=1M)
8. Integration: LOD affects spawn rate multiplier (e.g., Reduced=50% spawn, Minimal=25%)
9. Demo: collision particles bouncing off floor, mesh debris, distance LOD visualization
10. All unit tests pass, 95%+ coverage
