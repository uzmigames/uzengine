# Phase 1 — Play Mode (Editor vs Runtime Separation)

## Context

Currently `UzEngine.exe` always runs in a single mode. This task implements the `--editor` / `--play` CLI distinction: the engine can run as a lightweight editor process (no full game loop) or as a full game runtime (play mode, spawned by the editor).

## Goal

- `UzEngine.exe --editor --project=./` — editor mode: serves IPC, scene management, streams viewport, no game simulation
- `UzEngine.exe --play --project=./ --scene=main.uzscene` — play mode: full game loop, physics, scripts, streams to secondary viewport port
- The VSCode extension spawns and kills the play process independently of the editor process

## Spec Reference

[Engine Vision — Play Mode](../../../docs/engine-vision.md#37-play-mode-separate-process)

## Dependencies

- **phase1-scene-system** (scene loading from file)
