# Data-Driven Configuration Framework Spec

> **Status**: Planned (Phase 2 — design in Phase 1)
> **Priority**: P0 — Architectural Blocker
> **Rationale**: A unified text format for `.uzevent`, `.uzphysmat`, `.uzentity`, `.uzbt`, `.uzfx`, `.uznavmesh`, `.uzterrain` files is essential for AI-assisted content creation. Must be designed and implemented before any Phase 2 subsystem (audio, physics, animation) can be completed. The format is deliberately simple to be parseable by a 200-line C parser with zero external dependencies.

---

## Overview

The **data-driven configuration framework** provides a unified, plain-text format for declaring game data that would otherwise be hard-coded: audio events, physics materials, entity templates, behavior trees, particle effects, navigation meshes, and terrain definitions.

Design philosophy:
- **Plain text** — human-readable, AI-generatable, git-diff-friendly
- **Simple grammar** — parseable by a minimal C parser, no external dependencies
- **Hot-reloadable** — FileWatcher → reimport → subscriber notification (< 100ms latency)
- **Type-safe** — runtime type checking, validation, and error reporting
- **Composable** — via `@include` directives for shared definitions
- **Versioned** — `[meta]` block enables schema evolution and auto-migration

All data-driven files share a common text format grammar. The `[meta]` block at the top identifies the file type, version, and UUID. Type-specific sections follow.

---

## Part 1: Shared Text Format Grammar

### 1.1 Overall Structure

```
<file> = [meta_block] [section]*

<meta_block>    = "[meta]" <property>*
<section>       = "[" <section_name> "]" <property>*
<property>      = <key> "=" <value> [<comment>] NEWLINE
                | <key> "[]" "=" <value> [<comment>] NEWLINE
<comment>       = "#" <text>

<section_name>  = [a-zA-Z_][a-zA-Z0-9_]*
<key>           = [a-zA-Z_][a-zA-Z0-9_]*
<value>         = <primitive> | <array> | <string>
```

### 1.2 Whitespace and Comments

- **Leading/trailing whitespace** around keys, `=`, and values is automatically trimmed
- **Comments** begin with `#` and extend to the end of the line
  ```
  volume = 0.8  # Master bus volume for cutscenes
  ```
- **Blank lines** are ignored
- **Indentation** is optional and ignored (for readability only)

### 1.3 Primitive Types and Coercion

The parser performs automatic type coercion based on the expected type (declared in the importer spec for each field):

| Type | Format | Example | Coercion Rules |
|------|--------|---------|-----------------|
| `bool` | `true` \| `false` | `loop = true` | Case-insensitive; `0`→false, `1`→true, `yes`→true, `no`→false |
| `int` | Decimal integer | `priority = 128` | Whitespace trimmed; leading zeros ignored; overflow error if exceeds i32 range |
| `float` | Decimal or scientific | `volume = 0.8` or `gain = 1.5e-2` | IEEE 754 double; NaN/Inf allowed; underflow to 0.0 |
| `string` | Unquoted or quoted | `name = Sword` or `label = "Cool Sword"` | Unquoted: trimmed, stops at newline; quoted: `"..."` with `\"` escape |
| `vec2` | `x,y` | `offset = 1.5,2.0` | Two floats comma-separated; no spaces allowed around comma |
| `vec3` | `x,y,z` | `position = 0.0,5.0,-3.0` | Three floats comma-separated |
| `vec4` | `x,y,z,w` | `rotation = 0.0,0.0,0.7,0.7` | Four floats comma-separated |
| `color` | `r,g,b[,a]` | `tint = 1.0,0.5,0.2` or `tint = 0.2,0.2,0.2,0.8` | Three or four floats (0.0–1.0 range); alpha defaults to 1.0 |
| `enum` | String literal | `blend_mode = Add` | Case-insensitive; must match predefined enum values in spec; case-sensitive output |

**Type Safety Rules:**
- Importer MUST declare expected type for each field (e.g., "volume" is `float`, "loop" is `bool`)
- Parser coerces string value to expected type; error if coercion fails
- Out-of-range values (e.g., int overflow) cause `ImportError` with line/column info
- Missing fields use type-specific defaults (0, false, empty string, etc.)

### 1.4 String Values

**Unquoted strings:**
```
name = MyEvent          # Identifier-like; whitespace trimmed
author = John Doe       # Contains spaces; trimmed
```

**Quoted strings** (for values with special characters):
```
description = "Explosion 'boom' sound"
path = "res://assets/weapons/sword_01.fbx"
dialog = "Say \"hello\" to NPC"
```

**Escape sequences** (inside quoted strings only):
- `\"` → double quote
- `\\` → backslash
- `\n` → newline (literal newline in string)
- `\t` → tab
- `\r` → carriage return
- Any other `\X` → character X (e.g., `\;` → `;`)

**Multi-line strings** (triple-quote blocks):
```ini
description = """
This is a long description that spans
multiple lines without requiring escape sequences.
Newlines are preserved literally.
"""
```
- Begin with `"""` at end of line
- Content spans multiple lines until closing `"""`
- Internal quotes do not need escaping
- Indentation is preserved relative to the opening `"""`

### 1.5 Arrays and Repetition

Arrays are declared using the `[]` suffix on the key. Multiple lines with the same key append to the array:

```ini
[audio_event]
sounds[] = res://audio/explosion_01.wav
sounds[] = res://audio/explosion_02.wav
sounds[] = res://audio/explosion_03.wav
```

Parser behavior:
- All lines with `sounds[]` are collected into a single array in order
- Arrays are type-checked: all elements must coerce to the declared array element type
- Duplicate keys without `[]` cause an error (last-write-wins is not allowed for safety)

### 1.6 Include Directive

Files can include other files using the `@include` directive:

```ini
[meta]
type = uzphysmat
version = 1

@include "res://physics/common_materials.uzphysmat"

[material wood]
; overrides and extensions here
```

**Semantics:**
- `@include` is processed at parse time (not runtime)
- The included file MUST have the same `[meta] type` (e.g., both `uzphysmat`)
- Included file's `[meta]` block is ignored; only its sections are merged
- Sections from included file appear before the current file's sections
- Duplicate section names → last definition wins (allows overrides)
- Include paths are resolved relative to the includer's directory
- Circular includes are detected and cause `ImportError`
- `@include` can only appear at top level (not inside a section)

Example with override:
```ini
; in base.uzphysmat
[meta]
type = uzphysmat
version = 1

[material wood]
friction = 0.5
restitution = 0.3

; in project.uzphysmat
@include "base.uzphysmat"

[material wood]  ; overrides base.uzphysmat's [material wood]
restitution = 0.1  ; more bouncy
```

---

## Part 2: [meta] Block Convention

Every data-driven file MUST begin with a `[meta]` block. This block declares file identity and version information for migration and validation.

### 2.1 Required [meta] Fields

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| `type` | string | File type tag (lowercase, alphanumeric) | `uzevent`, `uzphysmat`, `uzentity`, `uzbt`, `uzfx`, `uznavmesh`, `uzterrain` |
| `version` | int | Schema version (1-based); incremented on breaking changes | `1`, `2`, `3` |
| `uuid` | string | RFC 4122 v4 UUID; generated once at creation and never changed | `550e8400-e29b-41d4-a716-446655440000` |

### 2.2 Optional [meta] Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `author` | string | (none) | Name of creator or team |
| `created` | string | (none) | ISO 8601 timestamp (e.g., `2026-03-01T15:30:00Z`) |
| `modified` | string | (none) | ISO 8601 timestamp of last edit |
| `tags[]` | array of strings | (empty) | Searchable metadata tags |
| `comment` | string | (none) | Long-form description or design notes |

### 2.3 Example [meta] Block

```ini
[meta]
type = uzevent
version = 2
uuid = a8d3c2f1-9e4a-4c8b-b5a3-6f1e2d7c9a4b
author = Audio Team
created = 2026-02-15T10:00:00Z
modified = 2026-03-01T18:45:00Z
tags[] = sfx
tags[] = explosion
comment = """
Main explosion sound event for grenades and mines.
Includes randomized pitch and volume variations.
Used in Phase 2 audio system prototyping.
"""
```

### 2.4 UUID Policy

- **Generation**: UUIDs are generated by the asset creation tool (VSCode panel, CLI, importer) and stored in `[meta] uuid`
- **Immutability**: UUIDs are never auto-assigned or modified at runtime
- **Purpose**: Enables stable asset references across renames and refactors (future work: asset ID database)
- **Format**: RFC 4122 v4 (random UUID); validated on parse; error if invalid format

### 2.5 Version Migration

When a file's schema version is lower than the importer's current version:

1. Importer calls `IAssetImporter::migrate(old_version, ParsedDoc&)` for each intermediate version
2. Migrate is responsible for inserting missing fields, transforming structures, etc.
3. If migrate returns `false`, import fails with error
4. On successful migration, `[meta] version` is updated to current version
5. The migrated file is **not automatically saved** — migration happens in-memory during import

Example migration (uzevent v1 → v2):
- v1: `[audio_event] volume = 0.8` (single volume)
- v2: `[audio_event] volume_main = 0.8, volume_variation = 0.1` (volume + variation)
- Migrate: if v1 and only `volume` present, copy to `volume_main`, set `volume_variation = 0`

---

## Part 3: File Type Registry

### 3.1 Registered File Types

All data-driven files use the shared text format. Type-specific schema is defined in the importer class and referenced spec.

| Extension | Type Tag | Importer Class | Spec | Phase | Description |
|-----------|----------|----------------|------|-------|-------------|
| `.uzevent` | `uzevent` | `AudioEventImporter` | `audio/audio-event-system.md` | 2 | Audio event definitions with randomization and parameters |
| `.uzphysmat` | `uzphysmat` | `PhysMaterialImporter` | `physics/physics-materials.md` | 2 | Physics material presets (friction, restitution, etc.) |
| `.uzentity` | `uzentity` | `EntityTemplateImporter` | `gameplay/entity-templates.md` | 1 | Entity template instances with property overrides |
| `.uzbt` | `uzbt` | `BehaviorTreeImporter` | `gameplay/behavior-trees.md` | 2 | Behavior tree definitions (Sequence, Selector, Parallel nodes) |
| `.uzfx` | `uzfx` | `ParticleImporter` | `rendering/particle-system.md` | 3 | GPU particle effects (emitters, behaviors, LOD) |
| `.uznavmesh` | `uznavmesh` | `NavmeshImporter` | `gameplay/navmesh.md` | 4 | Baked navigation meshes (tiles, agents, avoidance) |
| `.uzterrain` | `uzterrain` | `TerrainImporter` | `physics/terrain-system.md` | 4 | Terrain heightmaps, splatmaps, foliage (`.uzterrain` index; chunk files referenced) |

### 3.2 Importer Registration API

```cpp
// src/engine/assets/asset_importer_registry.h

class AssetImporterRegistry {
public:
    /// Register an importer for a file extension
    /// @param extension  File extension (with dot, e.g., ".uzevent")
    /// @param importer   Ownership transferred; registry will delete
    /// @return false if extension already registered
    static bool register_importer(
        const std::string& extension,
        IAssetImporter* importer
    );

    /// Retrieve importer for extension (returns nullptr if not registered)
    static IAssetImporter* get_importer(const std::string& extension);

    /// List all registered extensions
    static std::vector<std::string> registered_extensions();

    /// Check if extension is registered
    static bool is_registered(const std::string& extension);

    /// Global registry instance (singleton)
    static AssetImporterRegistry& instance();

private:
    std::unordered_map<std::string, std::unique_ptr<IAssetImporter>> importers_;
};
```

**Registration example** (in main.cpp or subsystem startup):
```cpp
#ifdef UZ_EDITOR
AssetImporterRegistry::register_importer(".uzevent", new AudioEventImporter());
AssetImporterRegistry::register_importer(".uzphysmat", new PhysMaterialImporter());
AssetImporterRegistry::register_importer(".uzentity", new EntityTemplateImporter());
AssetImporterRegistry::register_importer(".uzbt", new BehaviorTreeImporter());
#endif
```

---

## Part 4: IAssetImporter Interface

All data-driven importers inherit from `IAssetImporter`, which provides a common protocol for parsing, validating, migrating, and hot-reloading.

### 4.1 IAssetImporter Interface Definition

```cpp
// src/engine/assets/asset_importer.h

namespace uz {

/// Result of an import operation
struct ImportResult {
    bool        ok              = false;
    std::string error_message   = "";
    int         error_line      = -1;      // 1-based; -1 if not applicable
    int         error_column    = -1;      // 1-based; -1 if not applicable
    std::string error_context   = "";      // Line of text where error occurred
};

/// Parsed document: key-value pairs grouped by section
struct ParsedDoc {
    struct Section {
        std::string name;
        std::unordered_map<std::string, std::vector<std::string>> properties;
    };

    std::vector<Section> sections;
    std::unordered_map<std::string, std::string> meta;  // [meta] block fields

    /// Lookup helper: get first value of key in section
    std::string get(const std::string& section, const std::string& key,
                    const std::string& default_val = "") const;

    /// Lookup helper: get all values of key in section (for arrays)
    std::vector<std::string> get_array(const std::string& section,
                                       const std::string& key) const;

    /// Lookup helper: check if section exists
    bool has_section(const std::string& section) const;
};

/// Pure-virtual base class for asset importers
class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    /// Parse file from disk into structured ParsedDoc
    /// @param path         Full file path
    /// @param[out] doc     Filled with parsed sections and [meta] block
    /// @return ImportResult with ok=true on success, error info on failure
    virtual ImportResult parse(const std::string& path, ParsedDoc& doc) = 0;

    /// Migrate ParsedDoc from old_version to current version in-place
    /// @param old_version  Version from [meta] block
    /// @param[in,out] doc  Document to migrate
    /// @return false if migration fails (import will fail)
    /// @note Called repeatedly for each intermediate version step
    virtual bool migrate(int old_version, ParsedDoc& doc) = 0;

    /// Re-import (hot-reload) a previously imported asset
    /// @param path  Full file path of asset being reloaded
    /// @return ImportResult; failed reimport keeps previous asset version
    /// @note Called by FileWatcher → AssetImporterRegistry on file change
    virtual ImportResult reimport(const std::string& path) = 0;

    /// Extract dependency paths from a ParsedDoc
    /// @param doc           Parsed document
    /// @param[out] deps     Vector filled with dependency file paths
    /// @note Used by source asset database for dependency tracking
    /// @example For a `.uzentity` template, this might return mesh/material refs
    virtual void get_dependencies(const ParsedDoc& doc,
                                  std::vector<std::string>& deps) const = 0;

    /// Get the expected [meta] type tag for this importer
    /// @return String like "uzevent", "uzphysmat", etc.
    virtual std::string expected_type() const = 0;

    /// Get the current schema version this importer supports
    virtual int current_version() const = 0;
};

}  // namespace uz
```

### 4.2 ParsedDoc Helper Functions

The `ParsedDoc` structure provides convenient lookup methods for accessing parsed data:

```cpp
// Implementation in ParsedDoc class

/// Get single value from section[key], or default if missing
std::string get(const std::string& section, const std::string& key,
                const std::string& default_val = "") const {
    for (const auto& sec : sections) {
        if (sec.name == section) {
            auto it = sec.properties.find(key);
            if (it != sec.properties.end() && !it->second.empty()) {
                return it->second[0];
            }
        }
    }
    return default_val;
}

/// Get all values for section[key[]] (array)
std::vector<std::string> get_array(const std::string& section,
                                   const std::string& key) const {
    for (const auto& sec : sections) {
        if (sec.name == section) {
            auto it = sec.properties.find(key);
            if (it != sec.properties.end()) {
                return it->second;
            }
        }
    }
    return {};
}

/// Check if section exists
bool has_section(const std::string& section) const {
    return std::any_of(sections.begin(), sections.end(),
        [&](const Section& s) { return s.name == section; });
}
```

### 4.3 Parser Pseudocode

A reference implementation of the shared text format parser (simplified):

```cpp
ImportResult parse_text_format(const std::string& path, ParsedDoc& doc) {
    std::ifstream file(path);
    if (!file) {
        return {false, "File not found", -1, -1};
    }

    std::string line;
    int line_num = 0;
    std::string current_section = "";
    ParsedDoc::Section* current_sec = nullptr;

    auto error = [&](const std::string& msg) {
        return ImportResult{false, msg, line_num, 0, line};
    };

    while (std::getline(file, line)) {
        ++line_num;

        // Trim and skip blank/comment lines
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        // [section] header
        if (trimmed[0] == '[') {
            auto close = trimmed.find(']');
            if (close == std::string::npos) {
                return error("Malformed section header");
            }
            current_section = trim(trimmed.substr(1, close - 1));

            // Create new section
            doc.sections.emplace_back();
            doc.sections.back().name = current_section;
            current_sec = &doc.sections.back();
            continue;
        }

        // key = value property
        size_t eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) {
            return error("Expected 'key = value'");
        }

        std::string key = trim(trimmed.substr(0, eq_pos));
        std::string value_part = trimmed.substr(eq_pos + 1);

        // Strip trailing comment
        size_t comment = value_part.find('#');
        if (comment != std::string::npos) {
            value_part = value_part.substr(0, comment);
        }

        std::string value = trim(value_part);

        if (!current_sec) {
            return error("Property outside of section");
        }

        // Check for array syntax (key[])
        bool is_array = false;
        if (key.size() > 2 && key.substr(key.size() - 2) == "[]") {
            is_array = true;
            key = key.substr(0, key.size() - 2);
        }

        // Parse string literals
        if (value[0] == '"') {
            // quoted string handling
            value = parse_quoted_string(value);
        } else if (value.substr(0, 3) == R"(""")" ) {
            // multi-line string
            value = parse_multiline_string(file, line_num);
        }

        // Store in parsed doc
        current_sec->properties[key].push_back(value);

        if (!is_array && current_sec->properties[key].size() > 1) {
            return error("Duplicate key (use key[] for arrays)");
        }
    }

    return {true, "", -1, -1};
}
```

### 4.4 Import Error Reporting

When `ImportResult.ok == false`, the following fields are populated:

| Field | Type | Purpose |
|-------|------|---------|
| `ok` | bool | Always `false` for errors |
| `error_message` | string | Human-readable error (e.g., "Invalid float value: abc") |
| `error_line` | int | 1-based line number (or -1 if not applicable) |
| `error_column` | int | 1-based column number (or -1 if not applicable) |
| `error_context` | string | The actual line of text that caused the error |

**Error reporting best practices:**
- Include line number and column for syntax errors
- For semantic errors (missing required field), point to the section header
- For type coercion errors, include the problematic value and expected type
- Never include file paths in error messages (those are logged separately)

Example error reporting:
```cpp
// Syntax error: malformed section
return ImportResult {
    false,
    "Malformed section header: expected ]",
    42,
    15,
    "[audio_event volume = 0.8  # missing ]"
};

// Semantic error: missing required field
return ImportResult {
    false,
    "Required field missing: [audio_event] sounds",
    -1,
    -1,
    ""
};

// Type error: invalid float
return ImportResult {
    false,
    "Invalid float value: 'abc' in field 'volume'",
    17,
    10,
    "volume = abc  # expected number"
};
```

---

## Part 5: Hot-Reload Protocol

### 5.1 Overview

When a data-driven file is saved in the editor, the engine automatically detects the change, re-imports it, and notifies all subscribers of the reload. This enables designers to iterate quickly without restarting.

Flow:
```
File saved in editor
    ↓
FileWatcher detects change
    ↓
AssetImporterRegistry::on_file_changed(path)
    ↓
Get importer for extension → call reimport(path)
    ↓
On success: call all subscribers' on_asset_reloaded(path)
    ↓
On failure: log error, keep old version in registry
```

### 5.2 FileWatcher Integration

The `FileWatcher` (in `src/engine/editor/file_watcher.cpp`) monitors all registered data-driven extensions:

```cpp
// In editor startup code
#ifdef UZ_EDITOR
FileWatcher& watcher = FileWatcher::instance();

// Subscribe to all registered extensions
for (const auto& ext : AssetImporterRegistry::registered_extensions()) {
    watcher.watch(project_root, ext, [](const std::string& path) {
        AssetImporterRegistry::on_file_changed(path);
    });
}
#endif
```

**Filter behavior:**
- Only files matching registered extensions are watched
- Temporary files (`.tmp`, `.swp`, `.bak`) are ignored
- Directories are recursively watched (matches `**/*.uzevent`, etc.)
- Latency: debounce file events for 50ms to batch rapid saves

### 5.3 Reimport and Subscriber Notification

```cpp
// src/engine/assets/asset_importer_registry.h

class AssetImporterRegistry {
    // ... (earlier declaration)

    /// Called by FileWatcher when a data-driven file changes
    /// @param path  Full file path of changed file
    /// @return true if reimport succeeded, false if it failed
    static bool on_file_changed(const std::string& path) {
        auto importer = get_importer(extension_from_path(path));
        if (!importer) return false;

        ImportResult result = importer->reimport(path);
        if (result.ok) {
            // Notify all subscribers
            notify_subscribers(path, /* success */ true);
            return true;
        } else {
            // Log error but keep old version in cache
            log_error("Failed to reimport {}: {}:{}",
                      path, result.error_line, result.error_message);
            notify_subscribers(path, /* success */ false);
            return false;
        }
    }

    /// Register a subscriber for hot-reload notifications
    /// @param callback  Called with (path, success) when an asset reloads
    static void subscribe_reload(
        std::function<void(const std::string&, bool)> callback
    );

private:
    static inline std::vector<
        std::function<void(const std::string&, bool)>
    > reload_subscribers_;

    static void notify_subscribers(const std::string& path, bool success) {
        for (auto& cb : reload_subscribers_) {
            cb(path, success);
        }
    }
};
```

### 5.4 Error Isolation

**Important guarantee:** If a hot-reload fails, the previous version of the asset remains in the registry and in memory.

```cpp
// Example: Lua script listens for reload events
ctx.assets:on_reloaded("res://config/gameplay.uzentity", function(path, success)
    if success then
        print("Reloaded " .. path)
        -- Apply new configuration
        reload_game_settings()
    else
        print("Failed to reload " .. path .. "; keeping old version")
    end
end)
```

### 5.5 Reload Latency Target

- **Debounce**: FileWatcher buffers changes for 50ms before dispatching
- **Parse time**: Reimport should complete in < 20ms for typical files (< 1000 lines)
- **Subscriber notification**: Synchronous callbacks, should complete in < 20ms
- **Total**: < 100ms from file write to all subscribers notified (measured)

Latency measurement (debug info):
```cpp
auto t0 = high_resolution_clock::now();
bool ok = on_file_changed(path);
auto t1 = high_resolution_clock::now();
auto ms = duration_cast<milliseconds>(t1 - t0).count();
if (ms > 100) {
    log_warning("Hot-reload took {}ms (target: <100ms)", ms);
}
```

---

## Part 6: Lua Access Pattern

Scripts and engine systems access data-driven assets through the `ctx.assets` namespace.

### 6.1 Synchronous Load

```lua
-- Load a data-driven asset
local event = ctx.assets:load("res://audio/explosion.uzevent")
if event then
    print("Loaded event: " .. event.name)
    event:play(pos, volume)
else
    print("Failed to load event")
end
```

**Behavior:**
- Returns a Lua userdata object (opaque C++ handle)
- If asset is already in memory, returns cached version
- If asset is not found, returns `nil` (no exception)
- Type-safe: returned userdata has methods specific to asset type

### 6.2 Manual Force-Reimport

```lua
-- Force a reimport (useful for debugging or manual reload)
ctx.assets:reload("res://audio/explosion.uzevent")
print("Asset reloaded (or reload failed)")
```

**Behavior:**
- Synchronously calls `IAssetImporter::reimport(path)`
- If reload fails, logs error but does not raise exception
- Previous version remains in cache on failure

### 6.3 Hot-Reload Callback Registration

```lua
-- Register a callback for when an asset reloads
ctx.assets:on_reloaded("res://config/gameplay.uzentity", function(path, success)
    if success then
        print("Successfully reloaded: " .. path)
        -- Re-initialize game settings
        init_game_config()
    else
        print("Reload failed for: " .. path)
    end
end)
```

**Behavior:**
- Callback is invoked when the asset reloads (during editor play or hot-reload)
- Callback receives `(path: string, success: bool)`
- Callbacks are cleared when the script stops (on `uz_game_stop`)
- Multiple scripts can subscribe to the same asset

### 6.4 Asset Lifetime

**Editor mode:**
- Assets are loaded on demand and cached in `AssetRegistry`
- Hot-reload callbacks are active
- Assets are released when scene is unloaded or editor is closed

**Play mode:**
- Assets are loaded into the game DLL's VM
- Hot-reload is disabled (file changes are ignored)
- Assets are released when `uz_game_stop()` is called
- Lua references become invalid after stop (userdata is cleared)

**Errors:**
- Accessing a released asset raises a Lua error
- Callbacks for released assets are discarded

---

## Part 7: Error Reporting and Diagnostics

### 7.1 IPC Message Format

When an import error occurs, the engine broadcasts the error via IPC (for VSCode diagnostics integration):

```cpp
// Message: "asset.parse_error"
struct AssetParseError {
    std::string path;           // Full file path
    int         line;           // 1-based line number
    int         column;         // 1-based column number
    std::string message;        // Error description
    std::string code;           // Error code (e.g., "syntax_error", "type_error")
    std::string context;        // The line of text with the error
};
```

**IPC JSON format:**
```json
{
    "message": "asset.parse_error",
    "data": {
        "path": "res://audio/explosion.uzevent",
        "line": 42,
        "column": 15,
        "message": "Invalid float value: 'abc'",
        "code": "type_error",
        "context": "volume = abc  # expected number"
    }
}
```

**VSCode integration:**
The VSCode extension listens for `asset.parse_error` messages and maps them to diagnostics in the editor:

```typescript
// vscode-uzengine/src/diagnostics.ts (example pattern)
function onAssetParseError(error: AssetParseError) {
    const range = new vscode.Range(
        new vscode.Position(error.line - 1, error.column - 1),
        new vscode.Position(error.line - 1, error.column + 10)
    );

    const diagnostic = new vscode.Diagnostic(
        range,
        error.message,
        vscode.DiagnosticSeverity.Error
    );

    diagnostics.set(
        vscode.Uri.file(error.path),
        [diagnostic]
    );
}
```

### 7.2 VSCode Diagnostics Integration

Reuse the `shader-diagnostics.ts` pattern for data-driven asset files:

1. **On file open**: Query AssetImporterRegistry for existing errors
2. **On file change**: Listen for `asset.parse_error` IPC messages
3. **On successful reload**: Clear diagnostics for that file
4. **On error**: Display inline red squiggles with error tooltips

**Diagnostic codes:**
- `syntax_error` — Malformed section, missing `=`, etc.
- `type_error` — Value cannot coerce to expected type
- `validation_error` — Semantic error (missing required field, invalid enum, etc.)
- `migration_error` — Schema migration failed
- `include_error` — Circular or missing include directive

### 7.3 Console Error Output (Non-Editor)

In non-editor builds (standalone game), parse errors are logged to the console:

```
[ERROR] Failed to import 'res://audio/explosion.uzevent':
        Line 42, column 15: Invalid float value: 'abc'
        > volume = abc  # expected number
[ERROR] Asset reimport failed; keeping previous version
```

---

## Part 8: BNF/EBNF Grammar Appendix

Formal grammar in EBNF notation:

```ebnf
(* Lexical elements *)
WS          = { ' ' | '\t' }
NL          = '\n' | '\r\n'
DIGIT       = '0'..'9'
HEX_DIGIT   = DIGIT | 'A'..'F' | 'a'..'f'
LETTER      = 'a'..'z' | 'A'..'Z'
IDENT_CHAR  = LETTER | DIGIT | '_'
IDENT       = LETTER IDENT_CHAR*

(* Literals *)
INTEGER     = [ '-' ] DIGIT+
FLOAT       = [ '-' ] DIGIT+ ( '.' DIGIT+ [ ('e'|'E') INTEGER ] )?
STRING      = '"' { CHAR | '\"' | '\\' } '"'
MULTILINE   = '"""' { CHAR } '"""'
COLOR       = FLOAT ',' FLOAT ',' FLOAT [ ',' FLOAT ]
VEC2        = FLOAT ',' FLOAT
VEC3        = FLOAT ',' FLOAT ',' FLOAT
VEC4        = FLOAT ',' FLOAT ',' FLOAT ',' FLOAT
ENUM        = LETTER IDENT_CHAR*

(* Grammar *)
file        = [ meta_block ] { ( section | include ) }
meta_block  = '[meta]' { property }
section     = '[' IDENT { WS IDENT } ']' { property }
include     = '@include' WS STRING NL
property    = key [ array_suffix ] WS '=' WS value [ comment ] NL
key         = IDENT
array_suffix= '[]'
value       = primitive | array_value | multiline
primitive   = ( bool | INTEGER | FLOAT | ENUM |
                COLOR | VEC2 | VEC3 | VEC4 |
                STRING | unquoted_string )
array_value = { primitive NL }
comment     = '#' { CHAR }
bool        = 'true' | 'false' | 'yes' | 'no' | '0' | '1'
unquoted_string = IDENT { IDENT | WS }
```

---

## Part 9: Annotated File Type Examples

### Example 1: `.uzevent` (Audio Event)

```ini
[meta]
type = uzevent
version = 1
uuid = 550e8400-e29b-41d4-a716-446655440000
author = Audio Team
created = 2026-02-15T10:00:00Z

[audio_event]
name = Explosion Main
description = """
Primary explosion sound event.
Used for grenades, mines, and barrel explosions.
Includes randomized pitch and volume.
"""
bus = SFX
priority = 100

; Array of sound variations (random selection on play)
sounds[] = res://audio/sfx/explosion_01.wav
sounds[] = res://audio/sfx/explosion_02.wav
sounds[] = res://audio/sfx/explosion_03.wav

; Volume and pitch randomization
volume_base = 0.8
volume_random = 0.15
pitch_base = 1.0
pitch_random = 0.2

; 3D spatialness
spatial = true
min_distance = 1.0
max_distance = 100.0
doppler_level = 1.0

; Filtering (Phase 3)
low_pass_filter = false
lpf_frequency = 5000.0
```

**Schema notes:**
- `sounds[]` is required (at least one audio clip)
- `bus` must be a valid bus name from audio config
- `volume_random` and `pitch_random` are randomization ranges (±)
- `spatial = false` means 2D audio (non-positional)

### Example 2: `.uzphysmat` (Physics Material)

```ini
[meta]
type = uzphysmat
version = 1
uuid = a1b2c3d4-e5f6-4a7b-8c9d-e0f1a2b3c4d5
author = Physics Team
created = 2026-02-20T14:00:00Z

@include "res://physics/common_materials.uzphysmat"

[material wood]
density = 600.0           # kg/m³
static_friction = 0.5
dynamic_friction = 0.4
restitution = 0.3         # bounciness (0.0 = absorb, 1.0 = perfect bounce)
velocity_threshold = 0.1  # minimum impact velocity to trigger events

[material metal]
density = 7800.0
static_friction = 0.3
dynamic_friction = 0.25
restitution = 0.7

[material rubber]
density = 1200.0
static_friction = 0.8
dynamic_friction = 0.75
restitution = 0.9
```

**Schema notes:**
- Friction and restitution values are 0.0–1.0
- Density is in kg/m³ (used for mass calculation)
- Sections are named `[material <name>]`; used as keys in physics bodies

### Example 3: `.uzentity` (Entity Template)

```ini
[meta]
type = uzentity
version = 1
uuid = f9e8d7c6-b5a4-9321-8765-fedcba987654
author = Gameplay Team
created = 2026-03-01T09:00:00Z

[template]
name = Enemy Goblin
prefab = res://prefabs/enemies/goblin.uzscene
category = Enemy

[properties]
health = 20
speed = 5.5
ai_behavior = "aggressive"
```

**Schema notes:**
- `[template]` section defines identity
- `prefab` points to a `.uzscene` file (the base hierarchy)
- `[properties]` section lists property overrides (name = value)
- Property names map to node properties in the prefab

### Example 4: `.uzbt` (Behavior Tree)

```ini
[meta]
type = uzbt
version = 1
uuid = 1a2b3c4d-5e6f-7a8b-9c0d-e1f2a3b4c5d6
author = AI Team
created = 2026-03-01T11:00:00Z

[behavior_tree]
name = Guard Patrol
description = Simple guard patrol with alarm on threat

[root]
type = Selector  # Try children until one succeeds

[node1]
type = Sequence  # All children must succeed
parent = root
condition = "can_see_player"  # BT condition predicate

[node2]
type = Action
parent = node1
action = "attack_player"

[node3]
type = Action
parent = root
action = "patrol"

[blackboard]
; Shared state for tree nodes
target[] = none
alert_level[] = 0
```

**Schema notes:**
- Tree is defined via `parent` relationships (parent = root means this is a child of root)
- Node types: `Sequence` (all must succeed), `Selector` (first success wins), `Parallel` (all run), `Action` (leaf)
- `condition` is a predicate name to check before executing
- `[blackboard]` section lists shared variables

---

## Part 10: AI Agent Guidelines

This section provides guidance for AI agents generating or modifying data-driven files.

### 10.1 Generation Checklist

When an AI agent creates a new data-driven file, it MUST:

- [ ] Generate a unique RFC 4122 v4 UUID and include it in `[meta]`
- [ ] Set `[meta] type` to the correct tag (e.g., `uzevent`, `uzphysmat`)
- [ ] Set `[meta] version` to `1` (current version, per spec)
- [ ] Validate all required sections and fields are present (check spec)
- [ ] Ensure all property values are syntactically valid (no unclosed quotes, etc.)
- [ ] Use type-appropriate values (e.g., floats for volume, strings for names)
- [ ] For arrays, use the `key[]` syntax consistently
- [ ] Include reasonable comments explaining complex or non-obvious fields
- [ ] Do not use tabs (spaces only, for consistency)
- [ ] Do not embed file paths outside of `res://` prefix format

### 10.2 Validation Checklist

When validating an AI-generated file:

- [ ] Parse the file successfully (syntax check)
- [ ] All required fields are present in all required sections
- [ ] Type coercion succeeds for all values (float values parse as floats, etc.)
- [ ] Enum values match the spec (case-insensitive, but verify spell)
- [ ] Array values are homogeneous (all same type)
- [ ] Include directives resolve and don't form cycles
- [ ] References to other assets (res:// paths) exist (if validation includes filesystem check)

### 10.3 Common Patterns to Avoid

**Don't:**
```ini
; Wrong: unquoted string with spaces should be quoted
name = My Awesome Event

; Wrong: array syntax misuse
sounds = res://audio/boom.wav  ; should be sounds[]
sounds = res://audio/pop.wav

; Wrong: raw backslashes in unquoted strings (use quotes)
path = C:\assets\sound.wav

; Wrong: duplicate key without [] suffix
volume = 0.8
volume = 0.9
```

**Do:**
```ini
; Correct: quoted string for values with spaces
name = "My Awesome Event"

; Correct: array syntax
sounds[] = res://audio/boom.wav
sounds[] = res://audio/pop.wav

; Correct: quoted path
path = "res://assets/sound.wav"

; Correct: array syntax for multiple values
volume[] = 0.8
volume[] = 0.9
```

---

## Related Documentation

- [Scene System Spec](../gameplay/scene-system.md) — `.uzscene` format uses a variant of this grammar
- [Audio Event System](../audio/audio-event-system.md) — `.uzevent` file type specification
- [Physics Materials](../physics/physics-materials.md) — `.uzphysmat` file type specification
- [Entity Templates](../gameplay/entity-templates.md) — `.uzentity` file type specification
- [Behavior Trees](../gameplay/behavior-trees.md) — `.uzbt` file type specification
- [Particle System](../rendering/particle-system.md) — `.uzfx` file type specification
- [NavMesh](../gameplay/navmesh.md) — `.uznavmesh` file type specification
- [Terrain System](../physics/terrain-system.md) — `.uzterrain` file type specification
- [Asset Pipeline](../assets/asset-pipeline.md) — Overview of asset import process
- [Hot-Reload](../engine/hot-reload.md) — General hot-reload protocol and file watching

---

## Phase Roadmap

### Phase 1 (Editor Functional)
- [x] Design shared text format grammar
- [x] Define [meta] block convention
- [x] Design IAssetImporter interface
- [x] Write this spec document
- [ ] Implement text format parser (C++)
- [ ] Implement AssetImporterRegistry
- [ ] Implement .uzentity importer + hot-reload

### Phase 2 (Visual + Assets + Audio)
- [ ] Implement .uzevent importer (audio-event-system.md)
- [ ] Implement .uzphysmat importer (physics-materials.md)
- [ ] Implement .uzbt importer (behavior-trees.md)
- [ ] VSCode diagnostics integration for parse errors
- [ ] Asset hot-reload latency benchmarks (target: < 100ms)

### Phase 3 (Gameplay Complete)
- [ ] Implement .uzfx importer (particle-system.md)
- [ ] Implement .uznavmesh importer (navmesh.md)

### Phase 4 (Production)
- [ ] Implement .uzterrain importer (terrain-system.md)
- [ ] Asset dependency tracking (for incremental cooking)
- [ ] Schema migration examples and best practices

---

> **Version**: 1.0
> **Last Updated**: 2026-03-07
> **Author**: Documentation Operator
> **Status**: Planned (Design phase complete, implementation beginning Phase 1)
