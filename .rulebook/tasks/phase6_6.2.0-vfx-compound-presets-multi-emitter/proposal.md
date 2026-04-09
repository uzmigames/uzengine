# Proposal: phase6_6.2.0-vfx-compound-presets-multi-emitter

## Why
The standalone demo supports compound presets (Fire=3 emitters, Explosion=3, Magic=3)
where multiple emitters at the same position create layered effects (flame+smoke+sparks).
The Application only supports a single emitter at a time, so these rich composite effects
are impossible. Burst mode (Insert key) is also missing — all emission is continuous.

## What Changes
- Dynamic multi-emitter entity management in vfx_viewport_app (create/destroy on preset switch)
- 3 compound preset definitions (Fire, Explosion, Magic) with per-emitter startDelay
- Ctrl+Tab cycling between Single and compound modes
- Ctrl+1..8 emitter selection for editor overlay
- Burst mode (burstMode/burstPending fields, Insert key toggle)

## Impact
- Affected code: vfx_viewport_app.cpp/.h, particle_emitter.h, vfx_emitter_component.h, scene_vfx_sync.cpp, emitter_presets.h
- Breaking change: NO (additive)
- User benefit: Full standalone-quality compound VFX effects in the engine Application
