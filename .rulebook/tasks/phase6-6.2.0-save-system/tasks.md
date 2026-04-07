## 1. Save Manager

- [ ] 1.1 Create `runtime/engine/save/save_manager.h` — SaveManager: Save(slot, display_name) → Result<void>. Load(slot) → Result<void>. Delete(slot). ListSlots() → vector<SlotMetadata>. SlotMetadata: slot_index, display_name, scene_name, timestamp_ms, play_time_seconds, thumbnail_path.
- [ ] 1.2 Create `runtime/engine/save/save_manager.cpp` — Save path: `%APPDATA%/UzEngine/<project>/saves/`. File naming: `slot_N.uzsave`. Create directory on first save. List by scanning directory for .uzsave files.

## 2. .uzsave Format

- [ ] 2.1 Create `runtime/engine/save/save_format.h` — .uzsave binary: magic 'USVA' (4B), version (uint32), flags (uint32, bit 0=zstd compressed), header_size (uint32), then metadata msgpack, then data msgpack.
- [ ] 2.2 Create `runtime/engine/save/save_writer.h/.cpp` — SaveWriter: BeginSave(), WriteTransforms(registry), WriteLuaTables(lua_vm), WriteGameState(game_dll_blob), CaptureThumbnail(renderer), Finalize(path). Outputs .uzsave + .uzsave.jpg.
- [ ] 2.3 Create `runtime/engine/save/save_reader.h/.cpp` — SaveReader: Open(path), ReadMetadata() → SlotMetadata, ReadTransforms() �� vector<TransformData>, ReadLuaTables() → msgpack::object, ReadGameState() → vector<uint8_t>. Validate magic + version.

## 3. Transform Serialization

- [ ] 3.1 Implement transform snapshot — For each entity with TransformComponent: store entity_id, position, rotation, scale. Only save entities with modified transforms (dirty flag or diff against scene defaults).
- [ ] 3.2 Implement transform restore — On load: match saved entity_ids to current scene entities. Apply position/rotation/scale. Warn on missing entities (scene changed since save).

## 4. Lua Table Serialization

- [ ] 4.1 Add msgpack serialization for Lua tables — Traverse Lua table recursively: serialize numbers, strings, bools, nested tables. Skip functions, userdata, threads (log warning). Output as msgpack map keyed by entity_id.
- [ ] 4.2 Add Lua table deserialization — On load: for each entity with ScriptComponent, set instance table fields from saved msgpack data.

## 5. Game State (DLL)

- [ ] 5.1 Add save/load callbacks to GameContext — ctx->game_save_state(buffer, max_size) → actual_size. ctx->game_load_state(buffer, size). Game.dll writes/reads opaque blob. Engine stores it in .uzsave without interpreting.

## 6. Autosave

- [ ] 6.1 Create `runtime/engine/save/autosave.h/.cpp` — Autosave: interval_seconds (300), max_rotating_slots (3). Timer-based: on interval elapsed, save to next rotating slot (slot -1, -2, -3 cycling). Disabled during loading screens.

## 7. Thumbnail Capture

- [ ] 7.1 Add thumbnail to save — On save: capture current frame to 320x180 JPEG via readback + libjpeg-turbo encode. Save as slot_N.uzsave.jpg alongside .uzsave. ListSlots includes thumbnail_path.

## 8. Lua Save API

- [ ] 8.1 Create `runtime/engine/scripting/api/lua_saves.h/.cpp` — Register `uz.saves` in LuaVM. save(slot, name), load(slot), delete_slot(slot), list_slots() → array of metadata tables, get_metadata(slot) → table.
- [ ] 8.2 Add custom data to Lua saves — `uz.saves.set_custom(key, value)`, `uz.saves.get_custom(key)` for game-specific data stored in save.

## 9. Testing

- [ ] 9.1 Unit tests for .uzsave format — write/read round-trip, magic/version validation.
- [ ] 9.2 Unit tests for transform serialization — save/restore positions match.
- [ ] 9.3 Unit tests for Lua table serialization — nested tables, type handling, skip functions.
- [ ] 9.4 Unit tests for SaveManager — save, load, delete, list slots.
- [ ] 9.5 Unit tests for autosave — timer trigger, rotating slot cycling.

## 10. Documentation

- [ ] 10.1 Update `docs/engine/20-gameplay/` with save system architecture.
- [ ] 10.2 Add `uz.saves` to `docs/lua-api-reference.md`.
- [ ] 10.3 Document .uzsave format specification.

## 11. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 11.1 Update or create documentation covering the implementation
- [ ] 11.2 Write tests covering the new behavior
- [ ] 11.3 Run tests and confirm they pass
