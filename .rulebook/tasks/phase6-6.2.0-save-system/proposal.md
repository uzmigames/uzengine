# Proposal: phase6-6.2.0-save-system

## Why
Any playable game needs state persistence — saving player progress, loading checkpoints, autosave. The save system serializes ECS state (node transforms, component data) and Lua script instance tables to msgpack binary format. Slot-based storage with metadata (timestamp, thumbnail, scene name) enables save/load UI.

## What Changes
- SaveManager singleton: save/load/delete/list slots
- .uzsave binary format: header + msgpack blob (optional zstd compression)
- Node transform serialization (only modified transforms)
- Lua table serialization (per-node instance data)
- Game state opaque blob (game.dll defines structure)
- Autosave with rotating slots
- Save thumbnail capture (320x180 JPEG from viewport)
- Lua API: uz.saves.save, load, list_slots, get_metadata

## Impact
- New directory: `runtime/engine/save/`
- Depends on: Scene system (transforms), Lua VM (tables), Game DLL (game state)
- Thirdparty: msgpack-c (add to setup.ps1), optional zstd
- Spec: save-system.md
- Breaking change: NO
