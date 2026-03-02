# Proposal: phase2-audio-dsp-chains

## Why

The audio bus system currently passes audio directly from source voices to the bus submix without any effects processing. Real game audio requires per-bus DSP chains: reverb for environmental ambience (caves, halls), parametric EQ for radio voice filtering, compressor/limiter for bus dynamics control. XAudio2 supports custom XAPO (XAudio2 Audio Processing Objects) which can be chained per submix.

## What Changes

- **`src/engine/audio/audio_dsp.h/cpp`** — XAPO wrappers: `UzReverbXapo`, `UzEqXapo`, `UzCompressorXapo`, `UzLimiterXapo`
- **`src/engine/audio/audio_bus.h/cpp`** — `AudioBus` gains `add_effect(effect_type, params)`, `remove_effect`, `set_effect_param` API
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.audio:bus("sfx"):add_effect("reverb", {room=-12, hf_ratio=0.5})`
- **`.uzproj` audio section** — declarative per-bus effect chain definition
- **Tests** — XAPO registration, parameter update, effect chain ordering, Lua API

## Impact

- Affected specs: `docs/specs/audio-system.md` (DSP chains section)
- Dependencies: `phase2-audio-system` (XAudio2 bus submixes exist)
- Breaking change: NO — additive; existing buses work without effects
- User benefit: environmental audio variety; radio voice filter; dynamic bus compression — all without per-voice code
