# Core — Configuration System

> **Status**: Done
> **Source**: `runtime/engine/config/config.h`, `runtime/engine/config/config.cpp`
> **Depends on**: nlohmann/json (third-party)

## Purpose

Every engine parameter has a default value, overridable through a cascading chain of configuration sources. Zero hardcoded values in engine code — everything is data-driven and configurable per-project.

## Cascading Precedence

Configuration is resolved from highest to lowest priority:

```
CLI flags (--render.vsync=false)        ← highest priority
  ↓ overrides
Scene-specific overrides (.uzscene)     ← future
  ↓ overrides
Project config (.uzproject)
  ↓ overrides
Engine defaults (compiled into runtime) ← lowest priority
```

This mirrors how every major engine handles configuration:

| Engine | Project File | CLI Override | Cascading |
|--------|-------------|-------------|-----------|
| **UE5** | `.uproject` + `DefaultEngine.ini` | `-ExecCmds=...` | INI hierarchy (Base → Default → Project → User) |
| **Godot** | `project.godot` | `--render-driver` | ProjectSettings class with cascading |
| **UzEngine** | `.uzproject` (JSON) | `--key=value` | JSON merge (defaults → project → CLI) |

Key difference: we use JSON everywhere (not INI, not custom formats). This makes config files directly editable by LLMs and parseable in every language.

## API

### Loading

```cpp
uz::Config config;
uz::ConfigSetEngineDefaults(config);            // populate engine defaults
config.LoadFromFile("MyGame.uzproject");         // merge project config
config.ApplyCLIOverrides(argc, argv);            // merge CLI overrides
```

### Reading Values

All getters use dot-separated paths for nested keys and return a default if the key is missing:

```cpp
std::string backend = config.GetString("rendering.backend", "dx12");
int width = config.GetInt("rendering.resolution.0", 1920);
float gravity = config.GetFloat("physics.gravity.1", -9.81f);
bool vsync = config.GetBool("rendering.vsync", true);

// Check existence
if (config.Has("audio.sample_rate")) { ... }

// Raw JSON access (advanced)
const auto* root = config.Resolve("rendering");
```

### CLI Override Format

```bash
./engine --render.vsync=false --debug=true
./engine --render.backend dx12
./engine --no-vsync              # shorthand: sets "vsync" to false
./engine --headless              # boolean flag: sets "headless" to true
```

### Testing

Config can be loaded from a string (no file I/O), useful for unit tests:

```cpp
config.LoadFromString(R"({"rendering": {"backend": "vulkan"}})");
```

## .uzproject Format

The project file is the root configuration for a game project:

```json
{
  "name": "MyGame",
  "version": "1.0.0",
  "engine_version": "0.2",

  "rendering": {
    "backend": "dx12",
    "shader_model": "6.6",
    "resolution": [1920, 1080],
    "vsync": true,
    "max_fps": 0,
    "hdr": false,
    "render_scale": 1.0,
    "shadow_quality": "high",
    "anti_aliasing": "taa",
    "tonemap": "aces"
  },

  "physics": {
    "gravity": [0, -9.81, 0],
    "fixed_timestep": 0.01666,
    "max_substeps": 4
  },

  "audio": {
    "sample_rate": 48000,
    "max_channels": 64
  },

  "scenes": {
    "startup": "content/scenes/main_menu.uzscene"
  },

  "paths": {
    "content": "content",
    "source": "source",
    "plugins": "plugins",
    "cache": "cache"
  }
}
```

## Game Project Directory

```
MyGame/
├── MyGame.uzproject             # Root config
├── content/                     # All game assets
│   ├── scenes/                  # .uzscene files
│   ├── textures/                # Source textures
│   ├── meshes/                  # 3D models
│   ├── materials/               # Material definitions
│   ├── audio/                   # Sound files
│   └── ui/                      # UI layouts
├── source/                      # Game C++ or Lua scripts
├── plugins/                     # Project-specific plugins
├── binaries/                    # Build output (generated)
└── cache/                       # Intermediate files (git-ignored)
    ├── shaders/                 # Compiled shader cache
    └── cooked/                  # Cooked asset cache
```

## Design Decisions

### Why JSON, Not INI

UE5 uses INI files (a 1980s format) because of historical inertia. INI has no nesting, no arrays, no types, and requires custom parsers for each engine's variant. JSON:
- Has native nesting (perfect for `rendering.shadow.resolution`)
- Has types (string, number, boolean, array, object)
- Is universally parseable (C++, TypeScript, Python, Lua, LLMs)
- Is diffable in git
- Has schema validation via JSON Schema

### Why Not TOML or YAML

TOML is simpler but less universal (fewer LLM training examples, less tooling). YAML has ambiguous parsing rules (the "Norway problem": `NO` parses as `false`). JSON is the safest, most universal choice.

## Rules

1. **NEVER** hardcode engine parameters — always read from Config with a sensible default
2. **ALWAYS** provide a default value in getters — never crash on missing config
3. **ALWAYS** use dot-separated paths for nested keys: `"rendering.backend"`, not `"backend"`
4. **ALWAYS** validate on load — unknown keys warn, missing keys use defaults, invalid values error with clear message
