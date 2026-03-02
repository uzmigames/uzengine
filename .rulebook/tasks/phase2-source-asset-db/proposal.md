# Proposal: phase2-source-asset-db

## Why

When a glTF, FBX, or texture source file changes, the engine has no way to know which cooked assets need rebuilding, or which scenes reference the changed asset. Without a source asset database, every cook is a full rebuild of all assets — prohibitively slow at project scale. Additionally, without dependency tracking, renaming a mesh breaks all scene references silently.

A SQLite-backed source asset database tracks: source file paths, their import timestamps and content hashes, the cooked asset paths they produce, and the dependency graph between source files. Incremental import rebuilds only assets whose source changed or whose dependency changed. A `redirector` table handles renames/moves with backward compatibility.

## What Changes

- **`src/engine/assets/source_asset_db.h/cpp`** — `SourceAssetDb` singleton using SQLite3
- **`src/engine/assets/asset_cooker.h/cpp`** — `AssetCooker` that queries `SourceAssetDb` before reimporting
- **`src/runtime/ipc/handlers/asset_handlers.cpp`** — `asset.cook`, `asset.get_dependencies`, `asset.get_status` IPC handlers
- **Schema** — `source_assets`, `cooked_assets`, `dependencies`, `redirectors` SQLite tables
- **Tests** — incremental rebuild, dependency invalidation, redirector resolution, concurrent access safety

## Impact

- Affected specs: `docs/specs/asset-pipeline.md` (incremental import section)
- Dependencies: `phase2-gltf-import`, `phase2-fbx-import`, `phase2-texture-tools` (importers must exist)
- Breaking change: NO — existing importers continue to work; DB is additive tracking layer
- User benefit: < 5s incremental import for 1-file changes in a 10GB project; automatic dependency-driven rebuilds
