# Spec: Data-Driven Config Framework

> **Status**: Planned (Phase 3)
> **Subsystem**: Asset System, Data Management, Hot-Reload
> **Dependencies**: Asset Pipeline, Save System, Lua Scripting
> **Related Docs**: [asset-pipeline.md](asset-pipeline.md), [audio-system.md](audio-system.md), [physics-system.md](physics-system.md), [ui-system.md](ui-system.md)

---

## Overview

The Data-Driven Config Framework provides a **unified format and pipeline** for authoring game data:
- **Eliminates format diversity** — single spec for audio events, physics materials, entity templates, UI flows
- **Human-readable & AI-friendly** — plain-text INI-like format, no binary bloat
- **Hot-reloadable** — edit `.uz*` files, see changes instantly in editor or game
- **Validation-first** — centralized validation prevents broken references and type mismatches
- **Inspector integration** — editor shows typed fields, dropdowns for assets, live preview
- **Inheritance** — templates override parent configs, reducing duplication

Instead of each subsystem (audio, physics, UI, ECS) inventing its own format, this framework provides the **schema, validation, import, hot-reload, and editor tooling** for all.

---

## Architecture

```
AssetRegistry (central)
│
├── AssetType Registry
│   ├── Register "Event" type (.uzevent)
│   ├── Register "PhysMaterial" type (.uzphysmat)
│   ├── Register "EntityTemplate" type (.uzentity)
│   └── Register "UIFlow" type (.uzflow)
│
├── Importer Pipeline
│   ├── For each .uz* file:
│   │   ├── Parse INI → dictionary
│   │   ├── Validate schema (required fields, types)
│   │   ├── Resolve references (e.g., material → res://materials/ice.uzmat)
│   │   ├── Compile to runtime format (msgpack blob)
│   │   └── Cache .import metadata
│   │
│   └── On file change:
│       ├── Reload file
│       ├── Validate
│       ├── Update in-memory representation
│       └── Fire AssetReloadedEvent
│
├── Config Loader (Lua)
│   ├── Load at runtime: config = ctx.assets:load_config("res://events/footstep.uzevent")
│   └── Returns Lua table (transparently from msgpack)
│
└── Editor Inspector
    ├── Show .uz* file fields as typed input boxes
    ├── Dropdowns for asset references (filtered by type)
    ├── Live preview (for templates, play animation; for flows, show state machine)
    └── Real-time validation feedback
```

---

## Base Format Specification

All `.uz*` files follow INI-like syntax with extensions for arrays and hierarchies.

### Grammar

```
ConfigFile = Section*
Section = "[" identifier "]" Property* Subsection*
Property = key "=" value
Subsection = "[" parent_id "." child_id "]" Property*
Array = key "=" "[" value ("," value)* "]"
Object = key "=" "{" (key ":" value ("," key ":" value)*)* "}"

value = string | number | boolean | array | object | reference
string = "\"" (not "\"")* "\""
number = "-"? digit+ ("." digit+)?
boolean = "true" | "false" | "yes" | "no"
reference = "res://" path  // Asset path
```

### Example: Simple Audio Event

```ini
[footstep]
name = "Player Footstep"
priority = 50
loop = false
volume = 0.8
pitch_variance = 0.1

[footstep.variants]
# Multiple audio files, randomly chosen
0 = "res://audio/footstep_1.wav"
1 = "res://audio/footstep_2.wav"
2 = "res://audio/footstep_3.wav"
```

Parsed to Lua:
```lua
{
    footstep = {
        name = "Player Footstep",
        priority = 50,
        loop = false,
        volume = 0.8,
        pitch_variance = 0.1,
        variants = {
            "res://audio/footstep_1.wav",
            "res://audio/footstep_2.wav",
            "res://audio/footstep_3.wav",
        }
    }
}
```

---

## Asset Type Specifications

### 1. Audio Events (.uzevent)

```ini
# res://audio/events/footstep.uzevent

[footstep]
# Metadata
type = "sound_effect"
category = "sfx/movement"
tags = "movement,contact,player"

# Audio properties
priority = 50
volume = 0.8
pitch = 1.0
pitch_variance = 0.1
max_instances = 8

# Variants (randomly chosen each time)
[footstep.variants]
0 = "res://audio/sfx/footstep_1.wav"
1 = "res://audio/sfx/footstep_2.wav"
2 = "res://audio/sfx/footstep_3.wav"

# 3D spatial audio
[footstep.spatial]
min_distance = 0.5
max_distance = 50.0
doppler = 0.5
spread = 0.0
```

**Lua Usage:**
```lua
local event = ctx.assets:load_config("res://audio/events/footstep.uzevent")
ctx.audio:play_event(event)  -- Picks random variant
```

### 2. Physics Materials (.uzphysmat)

```ini
# res://physics/materials/ice.uzphysmat

[ice]
density = 917.0  # kg/m3 (actual ice density)
friction = 0.05  # Very slippery
restitution = 0.3
linear_damping = 0.1
angular_damping = 0.1

# Friction overrides for specific surfaces
[ice.friction_overrides]
ice = 0.05
wood = 0.3
metal = 0.15

# Sound feedback (plays on collision)
[ice.sounds]
impact_soft = "res://audio/events/impact_ice_soft.uzevent"
impact_hard = "res://audio/events/impact_ice_hard.uzevent"
slide = "res://audio/events/slide_ice.uzevent"

[ice.visual]
# Material reference (for rendering)
material = "res://materials/ice.uzmat"
decal = "res://materials/ice_decal.uzmat"
```

**C++ Usage:**
```cpp
auto config = asset_registry.load_config<PhysMaterialConfig>("res://physics/materials/ice.uzphysmat");
JoltPhysicsEngine::set_material_properties(collider, config);
```

### 3. Entity Templates (.uzentity)

```ini
# res://entities/enemy_grunt.uzentity

[metadata]
name = "Enemy Grunt"
category = "enemies"
tags = "hostile,melee,ai"
prefab = "res://entities/enemy_grunt.uzprefab"

# Properties (applied to root node)
[properties]
health = 50
damage = 10
armor = 5
speed = 5.0
detection_range = 20.0

# Subsystems attached
[subsystems.ai]
type = "melee_patrol"
waypoints = "[res://waypoints/patrol_a, res://waypoints/patrol_b]"
behavior_tree = "res://ai/behaviors/grunt_patrol.uzbt"

[subsystems.animation]
idle = "res://animations/enemy_idle.uzanim"
walk = "res://animations/enemy_walk.uzanim"
attack = "res://animations/enemy_attack.uzanim"
death = "res://animations/enemy_death.uzanim"

[subsystems.loot]
drops = "[coin, health_potion]"
drop_chance = 0.5

# Node spawning (procedural hierarchy)
[nodes.weapon]
type = "node_3d"
mesh = "res://meshes/sword.gltf"
position = "[0, 1.5, 0]"
parent = "root"

[nodes.shield]
type = "node_3d"
mesh = "res://meshes/shield.gltf"
position = "[0, 1.2, 0]"
parent = "root"
```

**Lua Usage:**
```lua
local template = ctx.assets:load_config("res://entities/enemy_grunt.uzentity")
local enemy = ctx.scene:instantiate_from_template(template)
enemy.health = template.properties.health
```

### 4. UI Flows (.uzflow)

State machine for UI navigation (menus, dialogs, etc.).

```ini
# res://ui/flows/main_menu.uzflow

[metadata]
name = "Main Menu Flow"
entry_state = "home"

# State definitions
[state.home]
template = "res://ui/templates/home_menu.uzhtml"
on_enter = "res://ui/scripts/home_menu.lua"
transitions = {
    "play" : "game_select",
    "settings" : "settings",
    "quit" : "exit"
}

[state.game_select]
template = "res://ui/templates/game_select.uzhtml"
on_enter = "res://ui/scripts/game_select.lua"
transitions = {
    "back" : "home",
    "load" : "loading"
}

[state.loading]
template = "res://ui/templates/loading.uzhtml"
transitions = {
    "done" : "game_ready"
}
overlay = true  # Display over previous state

[state.settings]
template = "res://ui/templates/settings.uzhtml"
transitions = {
    "back" : "home",
    "apply" : "home"
}

[state.exit]
action = "quit_game"

# Shared animations
[animations]
fade_in = "res://ui/animations/fade_in.uzanim"
fade_out = "res://ui/animations/fade_out.uzanim"
slide_left = "res://ui/animations/slide_left.uzanim"
```

**Lua Usage:**
```lua
local flow = ctx.assets:load_config("res://ui/flows/main_menu.uzflow")
local ui_controller = UIFlowController(flow)
ui_controller:transition_to("game_select")
```

---

## AssetRegistry & Validation

The registry manages all `.uz*` types, validates references, and enforces schemas.

```cpp
class AssetTypeDefinition {
public:
    struct FieldDef {
        std::string name;
        enum Type { String, Number, Bool, Array, AssetRef, Object } type;
        bool required = false;
        std::string description;
        std::vector<std::string> allowed_values;  // For enum
    };

    std::string type_name;            // "Audio", "PhysMaterial", etc.
    std::string file_extension;       // ".uzevent", ".uzphysmat", etc.
    std::vector<FieldDef> schema;
    std::function<void(const Dictionary&)> validator;
};

class AssetRegistry {
public:
    // Register a new .uz* type
    void register_type(const AssetTypeDefinition& def);

    // Load and parse .uz* file
    template <typename T>
    [[nodiscard]] T load_config(std::string_view path) {
        auto dict = parse_ini_file(path);
        validate_schema(dict);
        resolve_references(dict);
        return deserialize<T>(dict);
    }

    // Hot-reload: called when file changes on disk
    void reload_asset(std::string_view path);

    // Get validation errors (if any)
    [[nodiscard]] std::vector<std::string> validate(
        std::string_view path,
        const std::string& content
    );

    // List all assets of a type
    [[nodiscard]] std::vector<std::string> list_assets(std::string_view type_name);

    // Get schema for editor (for inspector)
    [[nodiscard]] AssetTypeDefinition get_type_def(std::string_view type_name);
};
```

### Validation Rules

```cpp
class ConfigValidator {
private:
    const AssetTypeDefinition& type_def_;

public:
    struct ValidationResult {
        bool success;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] ValidationResult validate(const Dictionary& config) {
        ValidationResult result;

        // 1. Check required fields
        for (const auto& field : type_def_.schema) {
            if (field.required && !config.has(field.name)) {
                result.errors.push_back(std::format(
                    "Missing required field: {}",
                    field.name
                ));
            }
        }

        // 2. Check field types
        for (const auto& [key, value] : config) {
            auto field = find_field(key);
            if (field && !type_matches(value, field->type)) {
                result.errors.push_back(std::format(
                    "Field '{}' has wrong type: expected {}, got {}",
                    key, field->type, value.type()
                ));
            }
        }

        // 3. Resolve and check asset references
        for (const auto& [key, value] : config) {
            if (is_asset_reference(value)) {
                auto ref_path = value.as_string();
                if (!asset_exists(ref_path)) {
                    result.errors.push_back(std::format(
                        "Asset reference broken: {} (from field '{}')",
                        ref_path, key
                    ));
                }
            }
        }

        // 4. Custom validator (per type)
        if (type_def_.validator) {
            type_def_.validator(config);
        }

        result.success = result.errors.empty();
        return result;
    }
};
```

---

## Hot-Reload Integration

When a `.uz*` file is edited and saved, the engine reloads it instantly (in editor mode).

```cpp
class ConfigHotReloader {
public:
    // Called by FileWatcher when .uz* file changes
    void on_file_changed(std::string_view path) {
        // 1. Parse new file
        auto new_config = parse_ini_file(path);

        // 2. Validate
        auto validation = validate_config(path, new_config);
        if (!validation.success) {
            report_validation_errors(path, validation.errors);
            return;
        }

        // 3. Update in-memory cache
        asset_registry.update_config(path, new_config);

        // 4. Fire reload event
        event_bus.fire<ConfigReloadedEvent>({
            .path = std::string(path),
            .type = detect_type(path),
        });

        // 5. Notify interested systems (UI refreshes, physics updates, etc.)
        notify_subscribers(path, new_config);
    }

private:
    void notify_subscribers(std::string_view path, const Dictionary& config) {
        // Lua scripts listening on this path
        for (auto& listener : lua_subscribers_[path]) {
            listener(config);
        }

        // C++ systems (audio, physics, UI)
        if (path.ends_with(".uzevent")) {
            audio_system_.on_event_reloaded(path, config);
        } else if (path.ends_with(".uzphysmat")) {
            physics_system_.on_material_reloaded(path, config);
        } else if (path.ends_with(".uzflow")) {
            ui_system_.on_flow_reloaded(path, config);
        }
    }
};
```

---

## Editor Inspector Integration

The editor shows `.uz*` files as forms with typed fields, dropdowns, and live preview.

**Inspector Rendering (TypeScript/Svelte):**

```svelte
<script lang="ts">
  // Inspector for res://audio/events/footstep.uzevent

  import { assetRegistry, onFileChange } from './registry';

  let config = assetRegistry.loadConfig('res://audio/events/footstep.uzevent');
  let typeDef = assetRegistry.getTypeDef('Audio');
  let validationErrors: string[] = [];

  async function saveConfig() {
    validationErrors = await assetRegistry.validate(config);
    if (validationErrors.length === 0) {
      await assetRegistry.saveConfig(config);
      // Hot-reload happens automatically via FileWatcher
    }
  }

  function onFieldChange(fieldName: string, value: any) {
    config[fieldName] = value;
    saveConfig();
  }
</script>

<div class="config-inspector">
  <h2>{config.name || 'Untitled Audio Event'}</h2>

  {#each typeDef.schema as field}
    <div class="field">
      <label>{field.name}</label>

      {#if field.type === 'String'}
        <input type="text"
               value={config[field.name]}
               on:change={(e) => onFieldChange(field.name, e.target.value)} />

      {:else if field.type === 'Number'}
        <input type="number"
               value={config[field.name]}
               on:change={(e) => onFieldChange(field.name, parseFloat(e.target.value))} />

      {:else if field.type === 'Bool'}
        <input type="checkbox"
               checked={config[field.name]}
               on:change={(e) => onFieldChange(field.name, e.target.checked)} />

      {:else if field.type === 'AssetRef'}
        <AssetPicker
          onSelect={(path) => onFieldChange(field.name, path)} />

      {:else if field.type === 'Array'}
        <ArrayField
          items={config[field.name]}
          onChange={(items) => onFieldChange(field.name, items)} />

      {/if}

      {#if field.required}
        <span class="required">*</span>
      {/if}
      <small>{field.description}</small>
    </div>
  {/each}

  {#if validationErrors.length > 0}
    <div class="errors">
      {#each validationErrors as error}
        <div class="error">{error}</div>
      {/each}
    </div>
  {/if}

  <button on:click={saveConfig}>Save</button>
</div>

<style>
  .config-inspector {
    padding: 16px;
  }
  .field {
    margin: 12px 0;
  }
  .required {
    color: red;
  }
  .errors {
    background: #ffe0e0;
    padding: 8px;
    margin-top: 12px;
  }
  .error {
    color: #cc0000;
  }
</style>
```

---

## Lua API

```lua
-- Load a .uz* config (returns parsed Lua table)
local event = ctx.assets:load_config("res://audio/events/footstep.uzevent")
local material = ctx.assets:load_config("res://physics/materials/ice.uzphysmat")
local template = ctx.assets:load_config("res://entities/enemy_grunt.uzentity")
local flow = ctx.assets:load_config("res://ui/flows/main_menu.uzflow")

-- Watch for config changes (hot-reload)
ctx.assets:on_config_changed("res://audio/events/footstep.uzevent", function(new_config)
    print("Footstep event reloaded!")
    -- Update any cached references
end)

-- List all configs of a type
local events = ctx.assets:list_configs("Audio")
-- Returns: {"res://audio/events/footstep.uzevent", "res://audio/events/jump.uzevent", ...}

-- List configs with prefix
local movement_events = ctx.assets:list_configs_with_prefix("res://audio/events/movement")

-- Get config schema (for editor tools)
local schema = ctx.assets:get_config_schema("Audio")
```

---

## Test Coverage

### Unit Tests

```cpp
// tests/config_framework_test.cpp

TEST(ConfigParserTest, ParseSimpleIni) {
    std::string content = R"(
[footstep]
name = "Player Footstep"
priority = 50
volume = 0.8
    )";

    auto config = parse_ini_string(content);
    EXPECT_EQ(config["footstep"]["name"].as_string(), "Player Footstep");
    EXPECT_EQ(config["footstep"]["priority"].as_number(), 50);
    EXPECT_EQ(config["footstep"]["volume"].as_number(), 0.8);
}

TEST(ConfigParserTest, ParseArrays) {
    std::string content = R"(
[event.variants]
0 = "res://audio/1.wav"
1 = "res://audio/2.wav"
2 = "res://audio/3.wav"
    )";

    auto config = parse_ini_string(content);
    auto variants = config["event"]["variants"].as_array();
    EXPECT_EQ(variants.size(), 3);
}

TEST(ConfigValidatorTest, RequiredFieldMissing) {
    AssetTypeDefinition audio_type;
    audio_type.schema.push_back({"name", FieldDef::Type::String, true});

    ConfigValidator validator(audio_type);
    Dictionary config;  // Missing "name"

    auto result = validator.validate(config);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(has_error(result.errors, "Missing required field: name"));
}

TEST(ConfigValidatorTest, BrokenAssetReference) {
    Dictionary config;
    config["material"] = AssetReference("res://materials/nonexistent.uzmat");

    ConfigValidator validator(audio_type);
    auto result = validator.validate(config);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(has_error(result.errors, "Asset reference broken"));
}

TEST(ConfigHotReloadTest, FileChangeTriggersReload) {
    ConfigHotReloader reloader;
    std::vector<std::string> reload_events;

    event_bus.subscribe<ConfigReloadedEvent>([&](const auto& e) {
        reload_events.push_back(e.path);
    });

    // Simulate file change
    reloader.on_file_changed("res://audio/events/footstep.uzevent");

    EXPECT_EQ(reload_events.size(), 1);
    EXPECT_EQ(reload_events[0], "res://audio/events/footstep.uzevent");
}

TEST(LuaBindingTest, LoadConfig) {
    LuaScriptingEngine lua;

    std::string script = R"(
local config = ctx.assets:load_config("res://audio/events/footstep.uzevent")
assert(config.name == "Player Footstep")
assert(config.priority == 50)
return true
    )";

    auto result = lua.eval(script);
    EXPECT_TRUE(result.as_bool());
}

TEST(LuaBindingTest, OnConfigChanged) {
    LuaScriptingEngine lua;

    std::string script = R"(
local changed = false
ctx.assets:on_config_changed("res://audio/events/footstep.uzevent", function(cfg)
    changed = true
    assert(cfg.priority == 100)
end)

-- Simulate reload with changed priority
ctx.assets:reload_config("res://audio/events/footstep.uzevent", {priority = 100})

return changed
    )";

    auto result = lua.eval(script);
    EXPECT_TRUE(result.as_bool());
}
```

### Integration Tests

- Parse complex entity template with nested nodes
- Validate config with broken references, show helpful errors
- Hot-reload config, verify listeners are notified
- Editor inspector renders correctly for all schema types
- Lua scripts can load and watch configs

---

## Phase Roadmap

**Phase 3 (Initial):**
- Base INI-like parser (support strings, numbers, booleans, arrays)
- Asset type registration framework
- Validation pipeline
- Audio events (.uzevent), physics materials (.uzphysmat)
- Hot-reload via file watcher
- Lua API (load_config, on_config_changed)

**Phase 4 (Enhancement):**
- Entity templates (.uzentity) with node instantiation
- UI flows (.uzflow) state machines
- Editor inspector with typed fields, dropdowns, live preview
- Config inheritance (templates extending base configs)
- Migration system (schema versioning, auto-upgrade old configs)
- Binary compiled format for shipping (optional)

---

## Related Docs

- [asset-pipeline.md](asset-pipeline.md) — General asset import, compression, LOD
- [audio-system.md](audio-system.md) — Audio events, buses, snapshots
- [physics-system.md](physics-system.md) — Physics materials, collider configurations
- [ui-system.md](ui-system.md) — HTML UI, state machines, data binding
- [lua-scripting.md](lua-scripting.md) — Lua scripting engine, ctx.assets API
