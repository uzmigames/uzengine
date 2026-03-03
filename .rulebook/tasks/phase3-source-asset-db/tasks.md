## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-gltf-import`, `phase2-fbx-import`, `phase2-texture-tools` are complete — importers exist
- [ ] 1.2 Read `docs/specs/asset-pipeline.md` — understand current import flow, cooked asset locations
- [ ] 1.3 Add `sqlite3` to `vcpkg.json` — amalgamation build, no external dependency
- [ ] 1.4 Add `xxhash` to `vcpkg.json` — fast content hashing for source files

## 2. Database Schema

- [ ] 2.1 Define `source_assets` table: `path TEXT PK`, `content_hash TEXT`, `import_timestamp INTEGER`, `importer_version INTEGER`
- [ ] 2.2 Define `cooked_assets` table: `cooked_path TEXT PK`, `source_path TEXT FK`, `cook_key TEXT` (hash of source_hash + importer settings)
- [ ] 2.3 Define `dependencies` table: `dependent_path TEXT`, `dependency_path TEXT`, `PRIMARY KEY (dependent, dependency)` — tracks which source files depend on which
- [ ] 2.4 Define `redirectors` table: `old_path TEXT PK`, `new_path TEXT` — maps old paths to new paths after rename/move
- [ ] 2.5 Define schema version table: `schema_meta` with `version INTEGER` — for migration

## 3. SourceAssetDb Implementation

- [ ] 3.1 Create `src/engine/assets/source_asset_db.h/cpp`
- [ ] 3.2 Implement `SourceAssetDb::open(db_path)` — opens or creates SQLite file at `project/.uzcache/assets.db`
- [ ] 3.3 Implement `SourceAssetDb::close()` — checkpoint WAL, close connection
- [ ] 3.4 Implement `SourceAssetDb::get_source_record(path) -> SourceRecord?`
- [ ] 3.5 Implement `SourceAssetDb::upsert_source(path, content_hash, importer_version)`
- [ ] 3.6 Implement `SourceAssetDb::needs_reimport(path) -> bool` — compares current file hash with stored hash
- [ ] 3.7 Implement `SourceAssetDb::get_dependents(path) -> vector<string>` — returns all files that depend on this source
- [ ] 3.8 Implement `SourceAssetDb::set_dependencies(path, deps[])` — replaces dependency entries for a source file
- [ ] 3.9 Implement `SourceAssetDb::add_redirector(old_path, new_path)` — insert into redirectors table
- [ ] 3.10 Implement `SourceAssetDb::resolve_redirector(path) -> string` — follows redirector chain (max 5 hops)
- [ ] 3.11 Implement schema migration: on open, compare `schema_meta.version`, run pending migrations in order

## 4. Content Hashing

- [ ] 4.1 Implement `hash_file(path) -> string` — XXH3_64bit on file contents, hex-encoded
- [ ] 4.2 Cache file hash in memory for session (avoid re-hashing unchanged large files)
- [ ] 4.3 Implement `CookKey::compute(source_hash, importer_settings_hash) -> string` — deterministic cook key

## 5. AssetCooker

- [ ] 5.1 Create `src/engine/assets/asset_cooker.h/cpp`
- [ ] 5.2 Implement `AssetCooker::cook(source_path)`:
  - Hash source file
  - Query `SourceAssetDb::needs_reimport(path)`
  - If clean: skip, return cached cooked path
  - If dirty: run appropriate importer, update DB records
- [ ] 5.3 Implement `AssetCooker::cook_all(project_root)` — scan all source files, cook dirty ones in dependency order
- [ ] 5.4 Implement `AssetCooker::cook_incremental(changed_paths[])` — cook changed + all transitively dependent files
- [ ] 5.5 Implement `AssetCooker::get_cooked_path(source_path) -> string?` — look up or cook on demand

## 6. Dependency Tracking Integration

- [ ] 6.1 Update `GltfImporter` — call `SourceAssetDb::set_dependencies(gltf_path, [texture_paths, material_paths])`
- [ ] 6.2 Update `FbxImporter` — same dependency registration
- [ ] 6.3 Update `TextureImporter` — register no additional dependencies (textures are leaf assets)
- [ ] 6.4 Update `AudioEventImporter` — register audio file paths as dependencies
- [ ] 6.5 Update `EntityTemplateImporter` — register mesh/material paths referenced in template

## 7. IPC Handler Integration

- [ ] 7.1 Create `src/runtime/ipc/handlers/asset_handlers.cpp`
- [ ] 7.2 Implement `asset.cook` handler — `{ source_path }` → trigger `AssetCooker::cook(path)` → return status
- [ ] 7.3 Implement `asset.cook_all` handler — `{ project_root }` → trigger full cook → stream progress events
- [ ] 7.4 Implement `asset.get_dependencies` handler — `{ source_path }` → return `{ deps: string[], dependents: string[] }`
- [ ] 7.5 Implement `asset.get_status` handler — `{ source_path }` → return `{ needs_reimport: bool, cooked_path: string?, cook_key: string }`
- [ ] 7.6 Register handlers under `#ifdef UZ_EDITOR`

## 8. FileWatcher Integration

- [ ] 8.1 Update FileWatcher — on source file change event: call `AssetCooker::cook_incremental([changed_path])`
- [ ] 8.2 Push `asset.reimported` IPC event after each successful cook: `{ source_path, cooked_path }`
- [ ] 8.3 Push `asset.cook_error` IPC event on failure: `{ source_path, error_message }`

## 9. Testing

- [ ] 9.1 Unit test: `needs_reimport` returns true for new file, false for unchanged file
- [ ] 9.2 Unit test: `get_dependents` returns correct transitive dependents after `set_dependencies`
- [ ] 9.3 Unit test: `resolve_redirector` follows chain `A → B → C`, max 5 hops, cycle detection
- [ ] 9.4 Unit test: `cook` skips reimport when cook_key matches — verifies importer not called
- [ ] 9.5 Unit test: `cook_incremental([texture.png])` — triggers reimport of all glTF files that depend on it
- [ ] 9.6 Integration test: modify source glTF → `cook_incremental` runs, cooked asset updated
- [ ] 9.7 Integration test: DB open → close → reopen — all records preserved (WAL checkpoint)

## 10. Documentation

- [ ] 10.1 Update `docs/specs/asset-pipeline.md` — "Source Asset Database" section with schema, cook flow, redirectors
- [ ] 10.2 Update `docs/guides/asset-pipeline-guide.md` — "Incremental Import" section
- [ ] 10.3 Update `docs/modules/assets/README.md` — document `SourceAssetDb`, `AssetCooker`
