# Proposal: phase3-audio-hrtf-binaural

## Why
Standard XAudio2 panning produces flat left/right stereo that sounds unrealistic over headphones because it ignores how sound physically wraps around a human head and reflects off the outer ear. HRTF (Head-Related Transfer Function) models this psychoacoustic effect to give accurate vertical and front/back positioning that stereo panning cannot achieve. This is a required feature for VR experiences and greatly improves spatial audio for standard headphone play. Without HRTF the engine cannot compete with Unity or Unreal for headphone-centric titles.

## What Changes
- Integrate OpenAL Soft as an optional audio backend alongside XAudio2, specifically for its built-in MIT KEMAR HRTF dataset and HRTF convolution pipeline
- Add `audio_backend` engine config option: `xaudio2` (default) or `openal_hrtf`
- Add `hrtf_enabled` bool flag on `CAudioSource` component; when true, routes the source through the HRTF convolution path
- Load HRTF dataset from `assets/audio/hrtf/default.mhr` (MIT KEMAR format); allow user-provided datasets by placing `.mhr` files in the same directory
- Update `AudioSystem::update_3d()` to compute HRTF azimuth/elevation angles from listener-relative source direction and pass to OpenAL Soft's `AL_EFFECT_EAXREVERB` or direct HRTF filter
- Fallback: if the output device reports fewer than 2 channels or OpenAL Soft fails to initialize, silently fall back to standard XAudio2 X3DAudio panning with a console warning
- Lua API: `ctx.audio:set_hrtf(source_entity, true/false)` to enable per-source at runtime
- Inspector checkbox `Use HRTF` on `CAudioSource`

## Impact
- Affected specs: `docs/specs/audio-system.md`
- Affected code: `src/audio/audio_system.cpp`, `src/audio/audio_system.h`, `src/audio/openal_backend.cpp` (new), `src/scripting/lua_audio.cpp`, `src/scene/components.h`
- Breaking change: NO (opt-in per source; default backend unchanged)
- User benefit: VR and headphone users hear accurate 3D elevation and front/back audio positioning. A sniper shot above and behind the player is clearly distinguishable from one at ear level using headphones alone.
