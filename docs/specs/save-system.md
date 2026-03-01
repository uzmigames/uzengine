# Spec: Save/Load System

> **Status**: Planned (Phase 3)
> **Format**: Binary msgpack (runtime) + JSON (debug)
> **Storage**: `%APPDATA%/UzEngine/<project_name>/saves/` (Windows)
> **File extension**: `.uzsave`
> **Slots**: 0–9 numbered + autosave slot (-1)

---

## Overview

The save/load system provides persistent game state management. Save data encompasses:
- **Scene state**: Node property overrides (transforms, animation states, custom variables)
- **Script state**: Per-node Lua instance tables (`self` data)
- **Game progression**: Scene visit flags, trigger activation states, custom data from `game.dll`
- **Settings**: Audio volume, graphics quality, keybindings — stored separately as `settings.uzsave`

The system is designed for **AI-driven game development** — save/load is AI-readable (via debug JSON mode) and supports multiple simultaneous editors observing game state.

---

## Architecture

```
UzEngine.exe (--play mode)
│  knows nothing about save format
│
game.dll  (C++, owns all save data)
│  ├── On_Save_State (called by engine)
│  │     ├── Serialize node transforms via SaveWriter
│  │     ├── Serialize Lua instance tables
│  │     ├── Write game-specific data (health, inventory, flags)
│  │
│  └── On_Load_State (called by engine)
│        ├── Deserialize node transforms
│        ├── Restore Lua instance tables
│        ├── Restore game-specific data
│
SaveManager (static, singleton, initialized in game.dll startup)
│  ├── write_slot(slot_id, lambda)  → encode to msgpack
│  ├── read_slot(slot_id, lambda)   → decode from msgpack
│  ├── list_slots()                 → metadata only (timestamp, scene, thumbnail)
│
SaveFile (.uzsave)
│  ├── Header (magic, version, compression)
│  ├── Metadata (timestamp, scene_name, slot_index)
│  └── Msgpack blob (node data, Lua tables, game data)
│
SaveThumbnail (.uzsave.jpg)
│  └── 320×180 JPEG from viewport at save time
```

---

## File Format

### `.uzsave` Binary Format

```c
struct SaveFileHeader {
    uint32_t magic;            // 0x55535641 ("USVA" = UzEngine Save)
    uint32_t version;          // Current: 1
    uint32_t flags;            // Bit 0: compressed (zstd)
    uint64_t timestamp_ms;     // Save creation time (milliseconds since epoch)
    uint32_t reserved[2];      // Future use
};

struct SaveFileMetadata {
    uint32_t slot_index;       // 0-9 or 255 for autosave
    uint32_t scene_name_len;   // strlen(scene_name)
    char scene_name[256];      // Scene file path (res://scenes/level1.uzscene)
};

// Followed by msgpack-encoded blob:
// {
//   "transforms": [{ "node_id": 1, "position": [...], "rotation": [...], "scale": [...] }],
//   "lua_tables": [{ "node_id": 5, "self": {...} }],
//   "game_data": <opaque to engine>
// }
```

**Compression**: Optional zstd compression (flag bit 0). Recommended for >1MB saves.

### `.uzsave.jpg` Thumbnail

A 320×180 JPEG captured from the 3D viewport at save time. Used for save slot UI in the pause menu.

---

## Save Data Structure

### What Gets Saved

```lua
-- Conceptual msgpack structure (not actual code, for illustration)
{
  version = 1,
  timestamp = 1709424000000,  -- milliseconds
  scene = "res://scenes/level1.uzscene",

  -- Node overrides (only nodes with modified properties)
  transforms = {
    {
      node_id = 3,
      position = { 5.0, 0.0, 3.0 },
      rotation = { 0.0, 45.0, 0.0 },
      scale = { 1.0, 1.0, 1.0 }
    }
  },

  -- Lua script instance data (per-node state)
  lua_tables = {
    {
      node_id = 5,
      self = {
        health = 75,
        ammo = 30,
        inventory = { "key_red", "key_blue" },
        animation_state = "idle"
      }
    }
  },

  -- Game progression flags
  progression = {
    scenes_visited = { "res://scenes/level1.uzscene", "res://scenes/level2.uzscene" },
    triggers_activated = { 101, 205, 412 },
    quest_flags = { "main_quest_01_started", "side_quest_02_complete" }
  },

  -- Opaque game.dll state (serialized by game code)
  game_state = <binary or base64 encoded data>
}
```

**Rules**:
- Only **modified** transforms are stored (not entire scene)
- Lua tables are **full copies** (serialized as msgpack map)
- Scene progression is **additive** (flags accumulated across playtime)
- `game_state` is **opaque** to the engine (game.dll defines its own structure)

---

## C++ API

### SaveManager Class

Located in `src/runtime/save_manager.h`:

```cpp
class SaveManager {
public:
    // Static singleton access
    static SaveManager& get();

    // Write save slot via callback
    // Lambda receives SaveWriter& — write data via w.write_*()
    // On completion: encodes msgpack, writes to disk, captures thumbnail
    Result<void> write_slot(int slot_id, std::function<void(SaveWriter&)> writer_fn);

    // Read save slot via callback
    // Lambda receives SaveReader& — read data via r.read_*()
    Result<void> read_slot(int slot_id, std::function<void(SaveReader&)> reader_fn);

    // Delete a save slot
    Result<void> delete_slot(int slot_id);

    // Check if slot exists
    bool slot_exists(int slot_id) const;

    // Get metadata (timestamp, scene, thumbnail path)
    struct SlotMetadata {
        uint64_t timestamp_ms;
        std::string scene_name;
        std::string thumbnail_path;  // .uzsave.jpg
    };
    Result<SlotMetadata> get_metadata(int slot_id);

    // List all populated slots
    std::vector<int> list_slots() const;

    // Autosave (slot = -1)
    Result<void> autosave(std::function<void(SaveWriter&)> writer_fn);

    // DEBUG: Dump slot as JSON to stdout (for inspection)
    Result<std::string> inspect_slot_json(int slot_id);
};
```

### SaveWriter Class

```cpp
class SaveWriter {
public:
    // Write individual values
    void write_value(const char* key, bool val);
    void write_value(const char* key, int32_t val);
    void write_value(const char* key, float val);
    void write_value(const char* key, const std::string& val);
    void write_value(const char* key, const Vec3& val);

    // Write arrays
    void write_array(const char* key, const std::vector<int>& vals);
    void write_array(const char* key, const std::vector<std::string>& vals);
    void write_array(const char* key, const std::vector<Vec3>& vals);

    // Write entire node transforms from scene
    void write_node_transforms(Scene& scene);

    // Write Lua instance tables from LuaVM
    // (only serializable values: numbers, strings, bools, tables)
    void write_lua_tables(LuaVM& vm);

    // Start a nested object (for structured data)
    SaveWriter& start_object(const char* key);
    SaveWriter& end_object();
};
```

### SaveReader Class

```cpp
class SaveReader {
public:
    // Read individual values (returns default if key absent)
    bool read_value(const char* key, bool default_val = false);
    int32_t read_value(const char* key, int32_t default_val = 0);
    float read_value(const char* key, float default_val = 0.0f);
    std::string read_value(const char* key, const std::string& default_val = "");
    Vec3 read_value(const char* key, const Vec3& default_val = Vec3::zero());

    // Read arrays
    std::vector<int> read_array_int(const char* key);
    std::vector<std::string> read_array_string(const char* key);
    std::vector<Vec3> read_array_vec3(const char* key);

    // Read entire node transforms into scene
    void read_node_transforms(Scene& scene);

    // Read Lua instance tables into LuaVM
    void read_lua_tables(LuaVM& vm);

    // Check if key exists
    bool has_key(const char* key) const;
};
```

### Usage Pattern

**In `game.cpp`** (called by engine on save/load):

```cpp
void uz_game_save_state(GameContext* ctx, const char* slot_id_str) {
    int slot_id = atoi(slot_id_str);  // "-1" = autosave, "0"-"9" = numbered slots

    SaveManager::get().write_slot(slot_id, [&](SaveWriter& w) {
        // Write transforms
        w.write_node_transforms(ctx->scene);

        // Write Lua instance data
        w.write_lua_tables(ctx->lua_vm);

        // Write game-specific state
        w.write_value("player_hp", player_hp);
        w.write_value("level_flags", level_flags);
        w.write_array("visited_scenes", visited_scenes);
    });
}

void uz_game_load_state(GameContext* ctx, const char* slot_id_str) {
    int slot_id = atoi(slot_id_str);

    SaveManager::get().read_slot(slot_id, [&](SaveReader& r) {
        // Restore transforms
        r.read_node_transforms(ctx->scene);

        // Restore Lua instance data
        r.read_lua_tables(ctx->lua_vm);

        // Restore game-specific state
        player_hp = r.read_value("player_hp", 100.0f);
        level_flags = r.read_value("level_flags", 0);
        visited_scenes = r.read_array_string("visited_scenes");
    });
}
```

---

## Lua API

Game scripts access save/load via `ctx.saves` object:

```lua
-- Save to slot 0
ctx.saves:save(0)

-- Load from slot 0
ctx.saves:load(0)

-- Delete slot 5
ctx.saves:delete(5)

-- Check if slot exists
if ctx.saves:slot_exists(2) then
    ctx:log("Slot 2 is populated")
end

-- Get metadata
local meta = ctx.saves:get_metadata(0)
if meta then
    ctx:log("Saved at: " .. os.date("%Y-%m-%d %H:%M:%S", meta.timestamp / 1000))
    ctx:log("Scene: " .. meta.scene_name)
    -- meta.thumbnail_path can be displayed in UI
end

-- Autosave (slot = -1)
ctx.saves:autosave()

-- List populated slots (returns array of slot IDs)
for _, slot_id in ipairs(ctx.saves:list_slots()) do
    ctx:log("Slot " .. slot_id .. " exists")
end
```

When `ctx.saves:load(slot_id)` is called, the engine:
1. Calls `uz_game_load_state(ctx, str(slot_id))`
2. Game.dll reads save file and restores all state
3. Scene is reset and reloaded
4. Lua scripts are re-initialized from saved instance tables

---

## Configuration (`.uzproj`)

```toml
[game]
# Auto-save on scene transition
autosave_on_scene_load = true

# Save directory (defaults to %APPDATA%/UzEngine/<project_name>/saves/)
# Useful for testing: override to a temp directory
# save_directory = "/tmp/saves"

# Number of autosave slots to keep (rotating buffer)
autosave_slots = 5

# Screenshot dimensions for thumbnails
save_thumbnail_width = 320
save_thumbnail_height = 180
```

---

## IPC Handlers (Editor Mode)

Available in `--editor` mode for testing and debugging:

| Handler | Args | Returns | Purpose |
|---------|------|---------|---------|
| `saves.list_slots` | — | `[{id, timestamp, scene, thumbnail_path}]` | List all saves |
| `saves.delete_slot` | `{slot_id: int}` | `{ok: bool}` | Delete a slot |
| `saves.inspect_slot` | `{slot_id: int}` | `{json_string: string}` | Dump msgpack as JSON |
| `saves.get_metadata` | `{slot_id: int}` | `{timestamp, scene, thumbnail_path}` | Get save metadata |

---

## Save/Load Flow

### Saving

```
User presses Save (Pause Menu)
  ↓
Game enqueues SceneCommand::SAVE_GAME(slot_id)
  ↓
Main thread processes command:
  ↓
Engine calls uz_game_save_state(ctx, slot_id_str)
  ↓
game.dll calls SaveManager::write_slot(slot_id, [](SaveWriter& w) { ... })
  ↓
SaveManager:
  • Encodes all w.write_*() calls as msgpack
  • Optionally compresses with zstd
  • Writes header + msgpack blob to .uzsave file
  • Captures viewport as JPEG to .uzsave.jpg
  • Returns Result::Ok()
  ↓
UI shows "Game saved to slot X"
```

### Loading

```
User selects Load (Pause Menu)
  ↓
Game enqueues SceneCommand::LOAD_GAME(slot_id)
  ↓
Main thread processes command:
  ↓
Engine calls uz_game_load_state(ctx, slot_id_str)
  ↓
game.dll calls SaveManager::read_slot(slot_id, [](SaveReader& r) { ... })
  ↓
SaveManager:
  • Reads .uzsave file
  • Decompresses if needed
  • Decodes msgpack blob
  • Passes SaveReader to lambda
  ↓
game.dll reads all state via r.read_*()
  ↓
Scene is reloaded with restored transforms and Lua state
  ↓
UI returns to gameplay with restored state
```

---

## Autosave

Autosave is triggered in two ways:

1. **Explicit call**: `ctx.saves:autosave()` in game code (e.g., on level start, after boss defeat)
2. **Automatic**: If `.uzproj` has `autosave_on_scene_load = true`, engine autosaves when scene transition completes

Autosave uses slot -1 (special constant `AUTOSAVE_SLOT = -1`). Multiple autosaves rotate through numbered "autosave slots" to keep a rolling buffer:

```cpp
// If autosave_slots = 5, rotates through slots [-1], [-2], [-3], [-4], [-5]
// Oldest autosave is overwritten when buffer is full
SaveManager::get().autosave([&](SaveWriter& w) { ... });
```

This allows players to recover from mistakes without losing recent autosaves.

---

## Error Handling

All save/load operations return `Result<T>`:

```cpp
auto result = SaveManager::get().write_slot(0, [&](SaveWriter& w) { ... });
if (!result) {
    ctx->log_error("Save failed: " + std::string(result.error()));
    // Handle error: insufficient disk space, permission denied, etc.
}
```

Common errors:
- `SaveError::DiskFull` — not enough space
- `SaveError::PermissionDenied` — cannot write to save directory
- `SaveError::InvalidSlot` — slot ID out of range (0–9)
- `SaveError::CorruptedFile` — msgpack decode failed on load

---

## Storage Locations

| Path | Purpose |
|------|---------|
| `%APPDATA%/UzEngine/<project_name>/saves/` | Save slots 0–9 |
| `%APPDATA%/UzEngine/<project_name>/saves/slot_0.uzsave` | Slot 0 data |
| `%APPDATA%/UzEngine/<project_name>/saves/slot_0.uzsave.jpg` | Slot 0 thumbnail |
| `%APPDATA%/UzEngine/<project_name>/settings.uzsave` | Game settings (separate file) |

On other platforms:
- **Linux**: `~/.local/share/UzEngine/<project_name>/saves/`
- **macOS**: `~/Library/Application Support/UzEngine/<project_name>/saves/`

---

## Test Coverage

### Unit Tests

- [ ] `SaveWriter` / `SaveReader` msgpack round-trip (write → encode → decode → read → same data)
- [ ] File I/O: write to disk, read back, verify integrity
- [ ] Compression: with/without zstd, verify decompressed matches original
- [ ] Node transform serialization: all position/rotation/scale combinations
- [ ] Lua table serialization: nested tables, arrays, circular reference guards
- [ ] Error cases: corrupt file, missing file, permission denied, disk full

### Integration Tests

- [ ] Save from game script, load, verify scene state restored
- [ ] Lua instance tables survive save/load cycle
- [ ] Game.dll hot-reload does not lose save data
- [ ] Autosave on scene transition
- [ ] Multiple concurrent save operations (thread-safe)

---

## Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 3** | Core save/load (msgpack serialization, node transforms, Lua tables, IPC inspect handlers) |
| **Phase 4** | Save versioning / migration (support upgrading old saves), cloud saves (Steam Cloud integration) |

---

## Related Docs

- [Lua Scripting](lua-scripting.md) — how Lua instance data is serialized
- [Scene System](scene-system.md) — node types and transforms
- [Engine Design Decisions](../engine/design-decisions.md) — Section 4.5: Why msgpack for saves
- [Game.dll Contract](../specs/game-dll-abi.md) — `uz_game_save_state` / `uz_game_load_state` callbacks
