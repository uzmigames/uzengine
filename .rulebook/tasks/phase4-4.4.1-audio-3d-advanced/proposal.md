# Proposal: phase4-4.4.1-audio-3d-advanced

## Why
With basic 2D audio playback from phase4-4.4.0, the next step is 3D spatial audio — sounds that come from specific positions in the world with distance attenuation, panning, and Doppler. This is critical for gameplay immersion: footsteps behind you, explosions at a distance, engines passing by. Also includes audio streaming for large files (music), hot-reload, and physics-based occlusion.

## What Changes
- X3DAudio integration for 3D positional audio (listener + emitter DSP)
- Distance attenuation curves (inverse, linear, custom)
- Doppler effect for moving sources
- Audio streaming for large files (>2MB decoded) to avoid memory bloat
- Audio hot-reload via FileWatcher (re-decode .uzaudio on asset change)
- Physics-based audio occlusion (raycast → low-pass filter)
- Audio event system (.uzevent): data-driven sound definitions with random/sequential clip selection

## Impact
- Depends on: phase4-4.4.0-audio-core, phase4-4.2.0-physics-jolt-core (for occlusion raycasts)
- Affected code: runtime/engine/audio/
- Breaking change: NO
- User benefit: Spatially accurate sound, music streaming, dynamic sound variation
