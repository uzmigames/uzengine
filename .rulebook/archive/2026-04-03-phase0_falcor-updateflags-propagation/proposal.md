# Proposal: phase0_falcor-updateflags-propagation

## Why
UzEngine rebuilds and re-uploads all constant buffers every frame regardless of whether any data changed. For a static scene (no moving lights, no animated camera, no changing fog), this is pure CPU waste: every CB field is written, every upload is issued, every GPU resource transition happens. Falcor tracks dirty state with an `UpdateFlags` bitmask per component — fields are only written to the CB when the corresponding flag is set, and the CB upload is skipped entirely when no flags are set. On typical game scenes where most lights, fog parameters, and camera parameters are static between frames, this saves significant per-frame CPU budget.

## What Changes
- `uint32_t dirtyFlags` added to each ECS component that feeds constant buffers: `TransformComponent`, `DirectionalLightComponent`, `PointLightComponent`, `CameraComponent`, `HeightFogComponent`, `VolumetricCloudsComponent`, `PostProcessComponent`
- Per-component flag bit definitions (e.g., `TRANSFORM_DIRTY = 1`, `LIGHT_COLOR_DIRTY = 2`, `LIGHT_INTENSITY_DIRTY = 4`)
- Property setters on each component set the relevant dirty flag; the flag is cleared after the CB field is written
- `per_frame_params.cpp` checks dirty flags before writing each CB section; skips entirely when the section's flags are all clear
- Frame-level `SceneUpdateFlags` aggregates all component dirty flags; the CB ring allocation is skipped when no flags are set
- `--stats` output includes "CB updates skipped per frame" counter

## Impact
- Affected specs: `docs/specs/renderer.md`
- Affected code: all ECS component headers (TransformComponent, light components, CameraComponent, fog/clouds/postprocess), `per_frame_params.cpp`, application stats overlay
- Breaking change: NO — components that were mutated every frame continue to write CBs every frame; the optimization is transparent
- User benefit: CPU-side constant buffer upload cost drops to near-zero for static scenes; measurable frame time improvement in scenes with many static lights; enables future background CB preparation on job threads
