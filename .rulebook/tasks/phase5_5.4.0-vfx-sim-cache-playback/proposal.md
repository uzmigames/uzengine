# Proposal: phase5_5.4.0-vfx-sim-cache-playback

## Why
Complex VFX simulations (explosions, environmental effects) are expensive to compute
every frame. Niagara's Sim Cache system records particle state to disk and replays it
without running compute shaders — enabling: (1) cinematic playback of art-directed effects,
(2) deterministic replays for cutscenes, (3) sharing effects as baked data for lower-end
hardware. This is a Phase 5 feature because it requires the full simulation pipeline to
be stable first and is primarily a content pipeline / tooling feature.

## What Changes
- New `.uzvfxcache` binary format: header + per-frame snapshots of particle SoA data
- `VFXSimRecorder`: captures particle buffer state each frame to memory, then saves to disk
- `VFXSimPlayer`: loads cache, uploads per-frame snapshots to GPU, feeds render pass directly
- Integration with VFX editor: record/stop/playback controls
- Compression: zstd per-frame delta compression (only changed particles)

## Impact
- Affected specs: particle-system.md (add sim cache section)
- Affected code: vfx_sim_cache.h/.cpp (new), vfx_system.cpp, vfx editor UI
- Breaking change: NO (new optional feature, simulation still works without cache)
- User benefit: Deterministic VFX playback, reduced GPU cost for baked effects, cinematic tooling
