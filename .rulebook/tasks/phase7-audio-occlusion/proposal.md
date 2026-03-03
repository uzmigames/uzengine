# Proposal: phase3-audio-occlusion

## Why

In the current audio system, sounds pass through walls — a gunshot behind a thick concrete wall sounds just as loud and bright as one in the open air. Audio occlusion uses physics raycasts to detect obstructions between the listener and each audio source, then applies low-pass filtering and volume reduction proportional to the obstacle's physics material density. This requires both the audio system and the physics system to be operational.

## What Changes

- **`src/engine/audio/audio_occlusion.h/cpp`** — `AudioOcclusionSystem` that raycasts each `CAudioSource` against the physics world
- **`src/engine/audio/audio_bus.h/cpp`** — per-voice LPF (low-pass filter) integration via XAudio2 XAPO
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.audio:set_occlusion_enabled(bool)`, `ctx.audio:get_occlusion(source_id) -> factor`
- **`.uzproj` audio section** — `occlusion_update_rate_hz`, `occlusion_max_distance`, `occlusion_ray_count` config
- **Tests** — raycast hit reduces volume, LPF applied, update rate throttling, smooth lerp

## Impact

- Affected specs: `docs/specs/audio-system.md` (occlusion section)
- Dependencies: `phase2-audio-system` (CAudioSource, voice API), `phase3-physics-system` (Jolt raycast API)
- Breaking change: NO — additive; occlusion disabled by default, opt-in per source via `CAudioSource.occlusion_enabled`
- User benefit: immersive spatial audio; sounds behind walls sound muffled; significant atmosphere improvement for indoor environments
