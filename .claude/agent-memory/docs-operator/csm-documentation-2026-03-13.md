---
name: Cascaded Shadow Map Documentation Completed
description: Documentation for Phase 2 CSM system implementation (cascade frustum splitting, shadow atlas, PCF, blending)
type: observation
---

## What Was Documented

Created comprehensive documentation for UzEngine's Cascaded Shadow Map (CSM) system that was recently implemented:

**New File**: `docs/engine/12-rendering/06-shadow-system.md` (488 lines)

## Coverage

1. **Architecture Overview** — how CSM solves the single-texture resolution problem
2. **Cascade Frustum Splitting** — practical split scheme (GPU Gems 3), lambda parameter, texel snapping
3. **Shadow Atlas Layout** — 2×2 grid arrangement of 4 cascades
4. **Shadow Depth Pass** — render graph integration, viewport/scissor per cascade
5. **Vertex & Pixel Shaders** — depth-only vertex shader, PCF shadow sampling (3×3)
6. **Constant Buffers** — ShadowDataCB (512B, root CBV b4), PerObjectCB, per-cascade data
7. **PCF Filtering** — depth/normal bias, soft shadows
8. **Cascade Blending** — smooth transitions in last 10% of cascade depth range
9. **Configuration** — ShadowConfig struct, tuning parameters (cascade count, resolution, lambda, bias)
10. **Performance** — cost drivers, optimization strategies
11. **Rules & Best Practices** — critical constraints and usage patterns
12. **Limitations & Future** — Phase 2 complete, Phase 3+ planned features

## Technical Details Covered

- ComputeCascades() algorithm (practical split scheme with log/linear blending)
- Texel snapping to prevent shadow swimming on camera movement
- Bounding sphere approach for rotational stability
- ShadowConfig struct (cascadeCount, resolution, maxDistance, splitLambda, bias params)
- ShadowCascade output (viewProj, splitNear/Far, texelSize)
- ShadowPassDesc for render graph registration
- 35 unit tests mentioned (split distances, matrices, bias)

## Integration

Updated `docs/engine/12-rendering/README.md`:
- Added new document to section overview table (marked as "Done (Phase 2)")
- Added "Implement cascaded shadow maps" to Quick Navigation guides
- Updated 3.4.0 milestone from "PLANNED" to "DONE (Phase 2)" with checklist of completed items
- Updated file manifest to show new 06-shadow-system.md file

## Documentation Standards Maintained

- Rust-style `///` doc comments pattern from codebase
- Technical depth suitable for auto-generated API docs
- Clear code examples (HLSL, C++)
- Performance considerations and tuning guidance
- References to implementation files (engine/renderer/shadows/, engine/renderer/passes/, shaders/runtime/)
- GPU binding model details (SM6.6 bindless, SM5 fallback)
- Rules and best practices for engine usage

## Files Modified

1. **Created**: `E:\UzEngine\0.2\docs\engine\12-rendering\06-shadow-system.md`
2. **Updated**: `E:\UzEngine\0.2\docs\engine\12-rendering\README.md`

## Cross-References

- Links to related docs: 06-lighting.md, 08-forward-pbr-pass.md, 04-render-graph.md
- References engine/renderer/ source structure
- Points to test file: tests/engine/renderer/test_shadow_cascade.cpp
- References shader files: shadow_depth_vs.hlsl, mesh_ps.hlsl

## Status

Documentation is complete and verified. All sections present and internally consistent. Ready for use by developers implementing shadow-based rendering features.
