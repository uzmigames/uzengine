# Proposal: phase6_6.1.0-vfx-update-cb-missing-fields

## Why
The standalone VFX demo has 6 simulation features (point force, vector field, terrain
collision, collision spawn, velocity inheritance, emitter seed) whose CB fields exist
in ParticleUpdateConstants but are never populated in the Application's frame_vfx_inline.cpp.
This means 4 presets (Attract, Repel, VectorFlow, HillyTerrain) cannot be ported.
Additionally, colorRampLutSrvIndex defaults to 0 instead of UINT32_MAX, causing the
billboard PS to sample from descriptor 0 instead of disabling the LUT.

## What Changes
- Add ~20 new fields to ParticleEmitter + VfxEmitterComponent
- Wire those fields through SyncVfxEmitters and into the update/spawn CBs
- Add 4 new presets (Attract, Repel, VectorFlow, HillyTerrain) to emitter_presets.h
- Fix colorRampLutSrvIndex default to UINT32_MAX in billboard CB
- Add new modules to VfxEditorOverlay

## Impact
- Affected specs: particle_simulation_pass.h, particle_render_pass.h
- Affected code: particle_emitter.h, vfx_emitter_component.h, scene_vfx_sync.cpp, frame_vfx_inline.cpp, frame_particles.cpp, emitter_presets.h, vfx_editor_overlay.cpp, vfx_viewport_app.cpp
- Breaking change: NO (additive fields with defaults)
- User benefit: All 25 standalone presets available in the Application
