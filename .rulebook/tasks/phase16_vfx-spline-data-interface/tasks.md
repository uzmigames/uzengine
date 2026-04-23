## 1. Implementation
- [ ] 1.1 Audit `runtime/engine/` for an existing spline primitive; if absent, implement minimal Catmull-Rom spline (points, tangents, arc-length LUT)
- [ ] 1.2 Define `SplineDataInterface` struct: StructuredBuffer<SplinePoint> layout + bindless index per emitter
- [ ] 1.3 Add HLSL include `spline_di.hlsli` with `SplineSampleAtU(idx, u)`, `SplineSampleArclen(idx, s)`, `SplineNearest(idx, pos)` helpers
- [ ] 1.4 Upload spline control points to the GPU once per frame for any spline referenced by active emitters
- [ ] 1.5 Implement `SpawnAlongSplineModule` — arc-length stratified sampling in Spawn CS
- [ ] 1.6 Implement `ForceTowardSplineModule` — spring force with configurable stiffness/max-distance in Update CS
- [ ] 1.7 Implement `FollowSplineModule` — per-particle `u` attribute advances by lifetime fraction, writes position
- [ ] 1.8 Register modules in `module_registry` + module compositor permutations
- [ ] 1.9 Editor UI: "Spline Target" picker in Parameters panel (entity with SplineComponent)
- [ ] 1.10 `.uzfx` serialization for spline-module configs
- [ ] 1.11 Sample assets: `effects/tests/spline_distribute.uzfx`, `spline_attract.uzfx`, `spline_follow.uzfx`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
