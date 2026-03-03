# Proposal: phase3-world-streaming

## Why

Open-world games cannot load all geometry, audio, and entities simultaneously — a 10 km² world at full detail would require tens of gigabytes of VRAM. World streaming solves this by dividing the world into chunks (`.uzscene` fragments), loading chunks near the player, and unloading distant chunks. The `CStreamingVolume` component triggers load/unload based on proximity, while the async IO system ensures loads don't stall the game thread.

## What Changes

- **`ecs/components.h`** — `CStreamingVolume` component: `chunk_paths[]`, `load_radius`, `unload_radius`, `priority`
- **`src/engine/streaming/world_streamer.h/cpp`** — `WorldStreamer` ECS system: monitors streaming volumes, queues async loads
- **`src/engine/streaming/streaming_chunk.h/cpp`** — `StreamingChunk` state machine, async parse + spawn
- **Placeholder meshes** — low-res stand-in mesh shown while chunk loads
- **Memory budget** — streaming arena with LRU eviction
- **Tests** — trigger detection, state machine transitions, LRU eviction, pause-during-load safety

## Impact

- Affected specs: `docs/specs/scene-system.md` (streaming section), `docs/specs/async-streaming.md` (implementation)
- Dependencies: `phase0-async-streaming-design` (spec), `phase1-scene-system` (ECS), `phase2-source-asset-db` (cooked asset paths)
- Breaking change: NO — additive; single-scene games unaffected
- User benefit: open-world support; smooth LOD transitions; predictable memory usage
