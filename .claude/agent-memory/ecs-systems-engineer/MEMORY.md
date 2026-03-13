# ECS Systems Engineer — Persistent Memory

## Project: UzEngine
C++23 game engine. ECS via EnTT. game.dll C ABI boundary. LuaJIT scripting.

## Key Paths
- `src/engine/ecs/components.h` — all ECS component structs
- `src/engine/scripting/` — lua_binding, script_runner, lua_sandbox (no LuaJIT linked)
- `src/game/` — game.dll implementation (owns LuaJIT, physics, timers)
- `docs/specs/` — authoritative specs for all systems

## Architecture Rules
- game.dll C ABI: never cross boundary with C++ types (vtables, STL, RTTI)
- GameContext._reserved[128] must always be zeroed
- LuaJIT linked ONLY in game.dll — never uz_engine.lib or render.exe
- Timer drain happens BEFORE node script on_update each frame
- EventBus events dispatched on Game Logic thread only

## Component Naming
- Prefix: `C` (e.g., CTransform, CMeshRenderer, CScriptRef)
- Plain data structs only — no virtuals, no side-effect constructors

## .uzscene Format
- INI-like, res:// paths, id=0 is null/invalid, parent=0 = scene root
- Script extension determines kind: .lua → SCRIPT_LUA, .cpp → SCRIPT_CPP

## Asset Registry (phase1-asset-browser)
- `src/engine/editor/asset_registry.h/.cpp` — AssetType, ImportStatus, AssetEntry, AssetRegistry
- `src/engine/editor/ipc_asset_handlers.h/.cpp` — asset.list/import/getInfo/createMaterial/Scene/Shader
- `assets/templates/` — material.uzmat, scene.uzscene, shader_unlit.hlsl (use {{NAME}} placeholder)
- Hash: FNV-1a mix of file_size + mtime. .import.json sidecar next to source file
- IPC pattern: capture pointer in lambda, null-check first, make_error() helper
- Deferred: phase2-asset-importer-pipeline, phase2-vscode-asset-browser-panel

## ECS/Scene Roadmap Analysis (2026-03-12)
- Full analysis at `.claude/memory/analysis_ecs_scene.md`
- Build order: World → Components → Events → Subsystems → Property System → Behaviors → Scene Loader
- Property System is HARD GATE for scene loading (type resolution + deserialization)
- Scenes CAN exist without rendering (World is pure simulation)
- Minimum playable scene: World + Components + Property(MVP) + SceneLoader + Behaviors + Renderer(1.3.0) + Input
