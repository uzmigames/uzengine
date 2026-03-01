# Phase 3 — Save/Load System

## Context

The engine can load and render scenes, but has no persistence. There is no way for players to save game progress. This task implements a complete save/load system: binary msgpack serialization, slot management, metadata tracking, and integration with game.dll state callbacks.

## Goal

A running `UzEngine.exe --play` that can:
- Save game state to numbered slots (0–9) + autosave slot via Lua API
- Load saved state back into memory, restoring all scene transforms and script state
- Store save metadata (timestamp, scene name, thumbnail JPEG)
- Provide debug inspection of save file contents via IPC
- Support autosave on scene transitions

## Spec Reference

[Save System Spec](../../../docs/specs/save-system.md)

## Approach

1. Define `SaveManager` singleton class and `SaveWriter`/`SaveReader` interfaces
2. Integrate msgpack-c++ from vcpkg (lightweight serialization)
3. Implement binary save file format with header + compressed msgpack blob
4. Implement C++ API: `write_slot()`, `read_slot()`, `delete_slot()`, `list_slots()`
5. Implement Lua API: `ctx.saves:save(slot)`, `ctx.saves:load(slot)`, etc.
6. Add `uz_game_save_state` / `uz_game_load_state` callbacks to game.dll C ABI
7. Capture viewport JPEG as save thumbnail
8. Add IPC debug handlers: `saves.list_slots`, `saves.inspect_slot`, etc.
9. Update `.uzproj` config schema to support save-related settings

## Out of Scope

- Cloud saves (Steam Cloud) — Phase 4
- Save versioning / migration — Phase 4
- Encrypted saves — Future
- Cross-platform cloud sync — Future

## Dependencies

- msgpack-c++: Lightweight binary serialization
- zstd: Optional save file compression
- libjpeg-turbo: Already available, used for thumbnails
