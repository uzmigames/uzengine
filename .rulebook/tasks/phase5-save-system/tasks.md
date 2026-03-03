## 1. SaveManager Infrastructure

- [ ] 1.1 Create `src/runtime/save_manager.h` — `SaveManager` singleton, `SaveWriter`/`SaveReader` interfaces
- [ ] 1.2 Create `src/runtime/save_manager.cpp` — save file I/O, msgpack encoding/decoding
- [ ] 1.3 Add msgpack-c++ to vcpkg.json dependencies
- [ ] 1.4 Add zstd to vcpkg.json dependencies
- [ ] 1.5 Define `SaveError` enum with `DiskFull`, `PermissionDenied`, `InvalidSlot`, `CorruptedFile`
- [ ] 1.6 Implement `Result<T>` wrapper for error handling (or use existing if available)

## 2. Save File Format & Storage

- [ ] 2.1 Create `src/runtime/save_file.h` — binary format structs (`SaveFileHeader`, `SaveFileMetadata`)
- [ ] 2.2 Implement `SaveManager::write_slot()` — encode msgpack, write header + blob, optional compression
- [ ] 2.3 Implement `SaveManager::read_slot()` — read header, decompress if needed, decode msgpack
- [ ] 2.4 Implement `SaveManager::delete_slot()` — remove .uzsave and .uzsave.jpg files
- [ ] 2.5 Implement `SaveManager::slot_exists()` — check if .uzsave file exists
- [ ] 2.6 Implement `SaveManager::list_slots()` — return array of populated slot IDs
- [ ] 2.7 Implement `SaveManager::get_metadata()` — read timestamp, scene name, thumbnail path from header
- [ ] 2.8 Create save directory on first use: `%APPDATA%/UzEngine/<project_name>/saves/` (use SHGetKnownFolderPath on Windows)

## 3. SaveWriter Implementation

- [ ] 3.1 Implement `SaveWriter::write_value()` overloads (bool, int32, float, string, Vec3)
- [ ] 3.2 Implement `SaveWriter::write_array()` overloads (int array, string array, Vec3 array)
- [ ] 3.3 Implement `SaveWriter::write_node_transforms()` — iterate scene nodes, save position/rotation/scale
- [ ] 3.4 Implement `SaveWriter::start_object()` / `end_object()` — nested msgpack maps
- [ ] 3.5 Implement `SaveWriter::write_lua_tables()` — serialize LuaVM instance tables (via Lua stack API)

## 4. SaveReader Implementation

- [ ] 4.1 Implement `SaveReader::read_value()` overloads with defaults (bool, int32, float, string, Vec3)
- [ ] 4.2 Implement `SaveReader::read_array_*()` methods (return vector<T>)
- [ ] 4.3 Implement `SaveReader::has_key()` — check if key exists in msgpack map
- [ ] 4.4 Implement `SaveReader::read_node_transforms()` — restore transforms into scene
- [ ] 4.5 Implement `SaveReader::read_lua_tables()` — restore Lua instance tables into LuaVM
- [ ] 4.6 Ensure graceful defaults (missing keys return safe defaults, no crashes)

## 5. Lua API Integration

- [ ] 5.1 Create `src/engine/scripting/lua_save_api.h/cpp` — register `ctx.saves` global
- [ ] 5.2 Implement `ctx.saves:save(slot_id)` — call engine's save mechanism
- [ ] 5.3 Implement `ctx.saves:load(slot_id)` — call engine's load mechanism
- [ ] 5.4 Implement `ctx.saves:delete(slot_id)` — delete slot
- [ ] 5.5 Implement `ctx.saves:slot_exists(slot_id)` → boolean
- [ ] 5.6 Implement `ctx.saves:get_metadata(slot_id)` → {timestamp, scene, thumbnail_path}
- [ ] 5.7 Implement `ctx.saves:list_slots()` → array of slot IDs
- [ ] 5.8 Implement `ctx.saves:autosave()` → save to autosave slot (-1)
- [ ] 5.9 Test Lua API from game script (via IPC lua.eval in editor mode)

## 6. Game.dll C ABI Callbacks

- [ ] 6.1 Update `include/game_api.h` — add `uz_game_save_state(GameContext*, slot_id_str)` signature
- [ ] 6.2 Update `include/game_api.h` — add `uz_game_load_state(GameContext*, slot_id_str)` signature
- [ ] 6.3 Implement engine-side call to `uz_game_save_state` when SceneCommand::SAVE_GAME is processed
- [ ] 6.4 Implement engine-side call to `uz_game_load_state` when SceneCommand::LOAD_GAME is processed
- [ ] 6.5 Add SceneCommand enum variants: `SAVE_GAME(slot_id)`, `LOAD_GAME(slot_id)`
- [ ] 6.6 Create example `game.cpp` showing how to use SaveWriter/SaveReader in callbacks
- [ ] 6.7 Test callbacks with a simple game.dll that writes/reads test data

## 7. Viewport Thumbnail Capture

- [ ] 7.1 Implement `SaveManager::capture_thumbnail()` — render viewport to off-screen texture
- [ ] 7.2 Convert texture to JPEG (320×180) using libjpeg-turbo
- [ ] 7.3 Save thumbnail as `.uzsave.jpg` alongside save file
- [ ] 7.4 Call thumbnail capture automatically in `write_slot()` before returning
- [ ] 7.5 Test that thumbnail is valid JPEG and correct dimensions

## 8. Autosave Mechanism

- [ ] 8.1 Add `AUTOSAVE_SLOT` constant (-1)
- [ ] 8.2 Implement `SaveManager::autosave()` — calls `write_slot(-1, ...)`
- [ ] 8.3 Implement rotating autosave buffer (config: `autosave_slots = 5` rotates through slots [-1...-5])
- [ ] 8.4 Implement autosave on scene transition (triggered by `SceneCommand::LOAD_SCENE`)
- [ ] 8.5 Read autosave trigger from `.uzproj` config: `autosave_on_scene_load = bool`
- [ ] 8.6 Test autosave with scene transitions

## 9. Project Configuration (`.uzproj`)

- [ ] 9.1 Update TOML schema — add `[game]` section
- [ ] 9.2 Add config keys: `autosave_on_scene_load`, `autosave_slots`, `save_thumbnail_width`, `save_thumbnail_height`
- [ ] 9.3 Update `ProjectConfig` struct to parse new keys
- [ ] 9.4 Apply config values in SaveManager (directory override, thumbnail dimensions, autosave slots)

## 10. IPC Debug Handlers

- [ ] 10.1 Register `saves.list_slots` IPC handler — returns `[{id, timestamp, scene, thumbnail_path}]`
- [ ] 10.2 Register `saves.delete_slot` IPC handler — takes `{slot_id}`
- [ ] 10.3 Register `saves.inspect_slot` IPC handler — returns msgpack contents as JSON string
- [ ] 10.4 Register `saves.get_metadata` IPC handler — returns metadata for a slot
- [ ] 10.5 Test handlers via MCP tool calls in editor mode

## 11. Testing & Validation

- [ ] 11.1 Unit tests: SaveWriter/SaveReader msgpack round-trip
- [ ] 11.2 Unit tests: File I/O (write, read, verify binary format)
- [ ] 11.3 Unit tests: Compression round-trip (zstd encode/decode)
- [ ] 11.4 Unit tests: Node transform serialization (all combinations of position/rotation/scale)
- [ ] 11.5 Unit tests: Lua table serialization (nested tables, arrays, edge cases)
- [ ] 11.6 Integration tests: Save from game.dll, load back, verify scene restored
- [ ] 11.7 Integration tests: Autosave on scene transition
- [ ] 11.8 Integration tests: Multiple save operations (thread-safety)
- [ ] 11.9 Error case tests: corrupt file, missing file, permission denied, disk full simulation

## 12. Documentation

- [ ] 12.1 Confirm `docs/specs/save-system.md` matches implementation
- [ ] 12.2 Update `docs/ipc-mcp.md` with `saves.*` handlers and example calls
- [ ] 12.3 Create example game script showing save/load usage
- [ ] 12.4 Add save system section to `docs/engine/hot-reload.md` (how Lua state survives game.dll reload)
