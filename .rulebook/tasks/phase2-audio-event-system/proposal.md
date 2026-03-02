# Proposal: phase2-audio-event-system

## Why

Direct `ctx.audio:play("res://sfx/gunshot.wav")` calls couple game logic to specific audio files. When the audio team changes a file path, swaps variations, or adds randomization, every Lua script must be updated. A data-driven audio event system — `.uzevent` files that declare sound variations, random pitch/volume ranges, bus routing, and cooldowns — decouples game logic from audio asset paths. Scripts call `ctx.audio:trigger("sfx/gunshot")` and the audio event system handles file selection and variation.

## What Changes

- **`src/engine/audio/audio_event.h/cpp`** — `AudioEvent` data struct and `AudioEventRegistry` singleton
- **`src/engine/assets/audio_event_importer.h/cpp`** — `.uzevent` parser using data-driven framework
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.audio:trigger(name)`, `ctx.audio:trigger(name, options)` Lua API
- **Hot-reload** — `.uzevent` file changes reload event definitions without game restart
- **Tests** — parse, trigger, variation selection, cooldown enforcement, Lua API

## Impact

- Affected specs: `docs/specs/audio-system.md` (event trigger section), `docs/specs/data-driven-framework.md` (`.uzevent` format)
- Dependencies: `phase2-audio-system` (XAudio2 voices exist), `phase0-data-driven-framework` (format spec)
- Breaking change: NO — additive; `ctx.audio:play` still works
- User benefit: audio variation without code changes; audio team owns `.uzevent` files; hot-reload changes while game runs
