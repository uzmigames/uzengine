# Proposal: phase3-audio-reverb-zones

## Why
All sounds in the engine play completely dry with no environmental acoustic simulation. Footsteps, gunshots, and voices sound identical whether the listener is in a tight stone cave, a large concert hall, or an open outdoor field. Reverb zones — scene volumes that apply a DSP reverb effect to sounds produced or heard within them — are standard in every commercial game engine and are the primary tool for environmental audio design. Without this system, game audio sounds flat and disconnected from the visual environment.

## What Changes
- Add `CAudioReverbZone` ECS component with fields: trigger volume shape (box/sphere with size), reverb preset enum, wet/dry mix (0.0–1.0), and transition blend distance (meters from zone edge)
- Built-in reverb preset library: `Dry`, `SmallRoom`, `MediumRoom`, `LargeRoom`, `ConcertHall`, `Cave`, `Auditorium`, `Outdoor`, `Underwater` — each is a named `XAUDIO2FX_REVERB_PARAMETERS` struct constant
- Create one XAudio2 submix voice per active reverb zone; audio sources send a copy of their signal to the reverb submix when inside the zone
- `AudioReverbSystem` tracks listener entity position each frame; for each `CAudioReverbZone` computes overlap and sets submix send level to `wet_mix * overlap_factor`
- For overlapping zones, blend linearly by normalized distance to each zone center and sum reverb contributions (capped at 2 simultaneous zones to limit DSP cost)
- Zone transition blend distance creates a smooth crossfade as the listener walks through the zone boundary rather than a hard cut
- Lua API: `ctx.audio:set_reverb_preset(entity, "Cave")` to change preset at runtime for animated zone effects
- Editor gizmo: transparent colored volume with zone name label in scene view; Inspector shows preset dropdown and wet/dry slider

## Impact
- Affected specs: `docs/specs/audio-system.md`, `docs/specs/scene-system.md`
- Affected code: `src/audio/audio_system.cpp`, `src/audio/audio_system.h`, `src/audio/audio_reverb_system.cpp` (new), `src/scripting/lua_audio.cpp`, `src/scene/components.h`
- Breaking change: NO
- User benefit: Environment acoustic design is a drag-and-drop volume placement in the scene editor. A cave level sounds like a cave; a cathedral sounds like a cathedral. No custom DSP code required from the game developer.
