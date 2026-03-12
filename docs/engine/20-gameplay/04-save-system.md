# Save/Load System

> **Status**: Planned (Phase 3)
> **Source**: `engine/gameplay/saves/`
> **References**: UE5 SaveGame + Serialization System (clean-room), Godot ResourceSaver

## Purpose

Persistent game state across sessions. Provides:
- **Save slots** (numbered 0–N + autosave) with metadata
- **Automatic serialization** of entity components via Property System (§21)
- **Thumbnail capture** from backbuffer
- **Compression** via msgpack binary format
- **Graceful fallback** — missing keys use defaults, never crashes on bad save

## Architecture

```
SaveSlot (file on disk)
  ├── Metadata (timestamp, playtime, level, thumbnail)
  └── Scene State
      ├── Entity 1 → Components → Properties → Values
      ├── Entity 2 → Components → Properties → Values
      └── Entity N → ...
```

## Save Format (msgpack binary)

Compact binary format (not human-readable, but efficient and fast):

```
SaveFile structure:
  [Header]
    - Magic: "UZSG" (4 bytes)
    - Version: 1 (uint32)
  [Metadata]
    - Timestamp: int64 (unixtime)
    - PlayTime: float32 (seconds)
    - Level: string
    - ThumbnailSize: uint32
    - ThumbnailData: [ThumbnailSize] bytes (JPEG)
  [EntityData]
    - EntityCount: uint32
    - For each entity:
      - EntityID: uint64
      - ComponentCount: uint32
      - For each component:
        - ComponentType: string
        - PropertyCount: uint32
        - For each property:
          - PropertyName: string
          - Value: msgpack-encoded (any type)
```

## SaveManager API

```cpp
namespace uz::SaveSystem {

/// Metadata about a save slot
struct SaveSlotInfo {
    int slot;                       ///< 0–N (or -1 for autosave)
    uint64_t timestamp_unix;        ///< When save was created
    float play_time_seconds;        ///< Total playtime in seconds
    std::string level_name;         ///< Level name at save time
    std::string thumbnail_path;     ///< Path to thumbnail PNG
};

/// Save game data to slot (blocks UI briefly)
/// Returns false if I/O error
bool SaveGame(int slot, const World& world, StringView level_name);

/// Load game data from slot (blocks until complete)
/// Returns false if slot doesn't exist or corrupted
bool LoadGame(int slot, World& world);

/// Check if slot has a save
bool HasSave(int slot);

/// Delete save file
bool DeleteSave(int slot);

/// List all available saves
std::vector<SaveSlotInfo> GetSlotInfos();

/// Auto-save (periodic, called by engine)
void Autosave(const World& world, StringView level_name);

/// Get path to save directory
std::string GetSavePath();

/// Debug: dump save file contents to console
void DumpSaveFile(int slot);

}  // namespace uz::SaveSystem
```

## Component Serialization

Only components and properties marked with `UZ_SAVE` flag are serialized:

```cpp
struct Player {
    UZ_COMPONENT(Player);

    UZ_PROPERTY(float, health, 100.0f, "Current health", PropertyFlags::SaveGame);
    UZ_PROPERTY(float, mana, 50.0f, "Current mana", PropertyFlags::SaveGame);
    UZ_PROPERTY(int, level, 1, "Player level", PropertyFlags::SaveGame);

    // Transient data — NOT saved
    UZ_PROPERTY(float, aiming_sensitivity, 2.0f, "Mouse sensitivity", PropertyFlags::Transient);
    UZ_PROPERTY(int, animation_state, 0, "", PropertyFlags::Transient);
};

struct Transform {
    UZ_COMPONENT(Transform);

    UZ_PROPERTY(Vec3, position, Vec3::Zero(), "World position", PropertyFlags::SaveGame);
    UZ_PROPERTY(Quat, rotation, Quat::Identity(), "World rotation", PropertyFlags::SaveGame);
    UZ_PROPERTY(Vec3, scale, Vec3::One(), "Scale", PropertyFlags::SaveGame);
};
```

## Entity Marking

Only entities with `UZ_SAVE` flag are included in save:

```cpp
struct SaveMarker {
    UZ_COMPONENT(SaveMarker);
    // Just a marker component — indicates entity should be saved
};

// When spawning an entity to save:
auto player = world.Create();
world.Attach<Transform>(player);
world.Attach<Player>(player);
world.Attach<SaveMarker>(player);  // Mark for saving
```

## Save Slot Layout

On disk: `%APPDATA%/UzEngine/<project_name>/saves/`

```
saves/
├── slot_0.uzsg              (Save file: binary msgpack)
├── slot_0_thumbnail.jpg     (320x180 JPEG)
├── slot_1.uzsg
├── slot_1_thumbnail.jpg
├── autosave.uzsg
└── autosave_thumbnail.jpg
```

## Thumbnail Capture

At save time, the backbuffer is captured as JPEG (320x180):

```cpp
// In SaveGame():
//   1. Request backbuffer readback
//   2. Convert RGBA → JPEG (stb_image_write or libjpeg)
//   3. Write to slot_N_thumbnail.jpg
//   4. Include JPEG data in msgpack save file
```

Lua can access thumbnail:

```lua
local saves = uz.SaveSystem.GetSlotInfos()
for _, save_info in ipairs(saves) do
    local thumbnail_path = save_info.thumbnail_path
    ui_image:set_source(thumbnail_path)
end
```

## Lua API

```lua
-- Save to slot 0
if not uz.SaveSystem.SaveGame(0, scene, "level_01") then
    print("Save failed!")
end

-- Load from slot 0
if not uz.SaveSystem.LoadGame(0, scene) then
    print("Load failed!")
end

-- Get save metadata for UI
local saves = uz.SaveSystem.GetSlotInfos()
for _, save_info in ipairs(saves) do
    print(string.format(
        "Slot %d: %s, Playtime: %.1f hours",
        save_info.slot,
        save_info.level_name,
        save_info.play_time_seconds / 3600
    ))
end

-- Delete slot
uz.SaveSystem.DeleteSave(2)
```

## Autosave

Engine calls autosave periodically (every 5 minutes or on level transition):

```cpp
// In Engine::Update():
static float autosave_timer = 0.0f;
autosave_timer += dt;
if (autosave_timer >= 300.0f) {  // 5 minutes
    uz::SaveSystem::Autosave(world_, current_level_);
    autosave_timer = 0.0f;
}
```

Autosave uses special slot (-1) with rotating buffer (keeps last 3 autosaves):

```
saves/
├── autosave_0.uzsg       (oldest)
├── autosave_1.uzsg       (middle)
├── autosave_2.uzsg       (newest)
└── autosave_pointer.txt  (tracks which is next)
```

## Serialization Lifecycle

### Save Process

```
1. SaveGame(slot, world) called
   ↓
2. Iterator discovers all entities with SaveMarker
   ↓
3. For each entity:
   - Extract all components
   - For each component:
     - Find properties with SaveGame flag
     - Encode property values to msgpack
   ↓
4. Capture backbuffer → JPEG thumbnail
   ↓
5. Write metadata (timestamp, playtime, level, thumbnail)
   ↓
6. Write entity data
   ↓
7. Compress and write to disk
   ↓
8. Fire OnGameSaved() callback
```

### Load Process

```
1. LoadGame(slot, world) called
   ↓
2. Load msgpack file from disk
   ↓
3. Verify magic and version
   ↓
4. Load metadata
   ↓
5. For each entity in save:
   - Create entity in world
   - For each component in save:
     - Find component type by name
     - Create component instance
     - For each property in save:
       - Find property by name in PropertyInfo registry
       - Deserialize value via offset + type
       - Write to component memory
     - Attach component to entity
   ↓
6. Fire OnGameLoaded() callback
```

## Error Handling

Graceful fallback — no crashes:

```cpp
// Missing property? Use default
if (!prop_value_in_save) {
    use_property_default_value();
}

// Unknown component type? Skip it
if (!component_type_exists()) {
    UZ_LOG(Save, Warning, "Unknown component type: {}", comp_type);
    skip_component();
}

// Corrupted file? Recover
try {
    data = msgpack::unpack(buffer);
} catch (...) {
    UZ_LOG(Save, Error, "Save file corrupted: {}", path);
    return false;
}
```

## Comparison

| Feature | UE5 SaveGame | Unity PlayerPrefs | Godot ResourceSaver | **UzEngine** |
|---------|--------------|-------------------|---------------------|--------------|
| Format | Binary (Unreal serialization) | Registry / plist | .tres / .tscn (text) | msgpack (binary) |
| Slots | Manual (N files) | Key-value only | Manual (N files) | Built-in (0–N) |
| Thumbnail | Manual capture | None | Manual save | Auto-captured |
| Compression | Optional (custom) | None | None | msgpack compact |
| Fallback | Strict type checking | Lossy | Strict | Graceful (defaults) |
| Lua access | No | No | Yes (GDScript) | Yes |
| Autosave | Manual | Manual | Manual | Built-in timer |

## Configuration

`.uzproj` project file:

```toml
[saves]
autosave_interval = 300  # seconds
autosave_keep_count = 3  # rotating buffer size
max_slots = 10           # 0-9 + autosave
```

## Implementation Phase

- **Phase 3**: Basic serialization (all SaveGame-flagged properties), msgpack format, slots 0–9, thumbnail capture
- **Phase 4**: Autosave with rotating buffer, versioning/migration, Lua API
- **Phase 5**: Cloud saves, multi-profile support, save encryption
