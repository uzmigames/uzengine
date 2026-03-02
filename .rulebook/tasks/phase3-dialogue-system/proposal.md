# Proposal: phase3-dialogue-system

## Why

RPGs, adventure games, and narrative games require branching dialogue with subtitles, voice acting, localization, and gameplay callbacks. Without a dialogue system, each conversation is hand-coded in Lua with manual audio triggers, subtitle timers, and skip logic — producing fragile, duplicated code. A `.uzdialogue` data-driven format with a `DialogueManager` provides: branching with conditions, subtitle timing synchronized to voice, localization routing, music ducking during speech, and Lua callbacks for gameplay responses.

## What Changes

- **`src/engine/dialogue/dialogue_manager.h/cpp`** — `DialogueManager`: loads `.uzdialogue`, drives playback, fires Lua callbacks
- **`src/engine/assets/dialogue_importer.h/cpp`** — `.uzdialogue` parser
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.dialogue:start(path)`, `ctx.dialogue:choose(index)`, `ctx.dialogue:skip()`
- **UI integration** — `DialogueManager` renders subtitle lines to a `uz-modal` overlay via `ctx.ui`
- **Audio ducking** — music bus volume reduced during dialogue via bus snapshot
- **Tests** — parse, playback, branching, localization routing, skip, callbacks

## Impact

- Affected specs: new `docs/specs/dialogue-system.md`
- Dependencies: `phase2-audio-event-system` (voice acting triggers), `phase2-ui-system` (subtitle overlay), `phase3-localization` (subtitle routing)
- Breaking change: NO — additive; no existing API changed
- User benefit: AI-generatable dialogue files; consistent subtitle + VO sync; localization without code changes
