# Project Modules Spec

## Overview

UzEngine projects are composed of **independent Git repositories** called modules. Each module contains a subset of the project's assets, code, or content. Access is controlled at the Git host level (GitHub, GitLab, Gitea) — if a developer does not have permission to a repository, that module is simply not cloned.

This architecture solves three problems common in large game projects:
- **Storage**: devs only download what their role requires
- **Security**: asset access is enforced by the Git host, not the engine
- **Onboarding**: new devs are productive in minutes, not days

---

## 1. `.uzproj` Module Declaration

Modules are declared in the `[modules]` section of `project.uzproj`:

```toml
[project]
name    = "MyMMORPG"
version = "1.0.0"
engine  = "0.1.0"

[modules]
# format: alias = { git = "url", path = "local/mount/path", ref = "branch|tag|commit" }

gameplay    = { git = "https://github.com/myorg/mmorpg-gameplay",    path = "src/game",        ref = "main" }
world-zone1 = { git = "https://github.com/myorg/mmorpg-world-zone1", path = "content/zone1",   ref = "main" }
world-zone2 = { git = "https://github.com/myorg/mmorpg-world-zone2", path = "content/zone2",   ref = "main" }
characters  = { git = "https://github.com/myorg/mmorpg-characters",  path = "content/chars",   ref = "main" }
audio       = { git = "https://github.com/myorg/mmorpg-audio",       path = "content/audio",   ref = "main" }
ui          = { git = "https://github.com/myorg/mmorpg-ui",          path = "content/ui",      ref = "main" }
```

### Field Reference

| Field | Required | Description |
|-------|----------|-------------|
| `git` | yes | HTTPS or SSH URL of the Git repository |
| `path` | yes | Local path relative to the project root where the module is mounted |
| `ref` | no | Branch, tag, or full commit SHA. Defaults to `main` |

### Pinning to a Commit

For reproducible builds, pin modules to a specific commit:

```toml
characters = { git = "https://github.com/myorg/mmorpg-characters", path = "content/chars", ref = "a3f92c1" }
```

---

## 2. Module Resolution at Engine Startup

When UzEngine opens a project, it resolves modules in order:

```
Open project.uzproj
        ↓
For each [module]:
    Does local path exist?
    ├── YES → git fetch + fast-forward to ref (if online)
    └── NO  → git clone --depth=1 --branch=ref <url> <path>
              ├── Success  → module mounted
              └── Auth fail / not found → module skipped silently
        ↓
Mount available modules into virtual filesystem
        ↓
Open editor with available content
```

### Silent Skip Behavior

If a developer does not have access to a module repository, the engine:
- Logs a warning in the editor output panel: `[modules] 'characters' not available — skipped`
- Continues opening the project with all other modules
- Shows missing modules as **grayed out** in the Asset Browser with a lock icon

This ensures that a level designer without access to `gameplay` code can still open the project and work on their content.

### Offline Mode

If the machine has no internet access, the engine uses whatever is already cloned locally. No automatic fetch is attempted. The editor shows `[offline]` next to each module in the status bar.

---

## 3. Module Lifecycle Commands

### VSCode Editor — Module Panel

```
Modules Panel
┌─────────────────────────────────────────────────────────┐
│  Module          Status        Ref         Local Path    │
├─────────────────────────────────────────────────────────┤
│  gameplay      ✓ up-to-date   main        src/game/     │
│  world-zone1   ✓ up-to-date   main        content/zone1/│
│  world-zone2   ↑ 3 behind     main        content/zone2/│
│  characters    🔒 no access   main        —             │
│  audio         ✓ up-to-date   main        content/audio/│
│  ui            ✓ up-to-date   main        content/ui/   │
├─────────────────────────────────────────────────────────┤
│  [Sync All]  [Sync Selected]  [Open in Terminal]         │
└─────────────────────────────────────────────────────────┘
```

### CLI

```bash
# Sync all accessible modules to their declared ref
uzengine modules sync

# Sync a specific module
uzengine modules sync --module=world-zone1

# Show status of all modules
uzengine modules status

# Add a new module to the project
uzengine modules add --alias=vfx --git=https://github.com/myorg/mmorpg-vfx --path=content/vfx

# Remove a module (deletes local clone and removes from .uzproj)
uzengine modules remove --alias=vfx
```

---

## 4. Full Build — Module Aggregation

The full build (typically run by the build server or CTO) requires access to all modules. `UzBuild.exe` (see `build-system.md`) enforces this:

```bash
uzengine build --platform=windows --config=release
```

### Build Behavior with Missing Modules

Unlike the editor (which skips inaccessible modules silently), **the build fails explicitly** if any declared module is inaccessible:

```
[build] Resolving modules...
[build] ✓ gameplay       cloned @ a3f92c1
[build] ✓ world-zone1    cloned @ 7b21de4
[build] ✓ world-zone2    cloned @ 9c04af2
[build] ✗ characters     ERROR: repository not found or access denied
[build] FATAL: build aborted — 1 module(s) unavailable
[build] Grant access to all modules or run with --skip-modules=characters (unsafe)
```

The `--skip-modules` flag exists for partial builds (e.g., a patch that doesn't touch characters) but is explicitly marked unsafe.

---

## 5. `.uzpak` Partitioning by Module

For large projects, the pack stage splits output by module instead of producing a single monolithic file. This enables platform features like **Steam depot partitioning** and **DLC delivery**.

```toml
# project.uzproj
[build.pack]
strategy = "per-module"   # options: "single" (default) | "per-module"
output_dir = "build/"
```

Output with `per-module`:

```
build/
├── mmorpg-base.uzpak         ← engine runtime + gameplay code
├── mmorpg-world-zone1.uzpak
├── mmorpg-world-zone2.uzpak
├── mmorpg-characters.uzpak
├── mmorpg-audio.uzpak
└── mmorpg-ui.uzpak
```

### Steam Depot Mapping Example

```ini
; steam_depot_map.ini (generated by uzengine build --steam)
[depot_1001]  ; base game — always downloaded
files = mmorpg-base.uzpak, mmorpg-ui.uzpak, mmorpg-audio.uzpak

[depot_1002]  ; world zone 1 — downloaded on first login
files = mmorpg-world-zone1.uzpak, mmorpg-characters.uzpak

[depot_1003]  ; world zone 2 — downloaded when player reaches zone 2
files = mmorpg-world-zone2.uzpak
```

---

## 6. Module Repository Structure Convention

Each module repository should follow this layout (enforced by `uzengine modules validate`):

```
mmorpg-characters/
├── .module.toml          ← module manifest (name, type, engine_min_version)
├── meshes/               ← .fbx / .glb source files
├── textures/             ← .png / .hdr source files
├── materials/            ← .uzmat files
├── animations/           ← .fbx animation clips
└── .import/              ← cooked asset cache (gitignored)
```

### `.module.toml`

```toml
[module]
name              = "mmorpg-characters"
type              = "content"            # content | code | mixed
engine_min_version = "0.1.0"
description       = "Character meshes, textures, and animations for MyMMORPG"
```

`type` values:
- `content` — assets only, no C++ or Lua source
- `code` — C++ game.dll sources and/or Lua scripts only
- `mixed` — both assets and code (e.g., a self-contained gameplay feature)

---

## 7. IPC Handlers (modules.*)

```
modules.status    {}                            → { modules: [{ alias, status, ref, path, behind }] }
modules.sync      { alias? }                    → stream progress events
modules.add       { alias, git, path, ref? }    → { ok }
modules.remove    { alias }                     → { ok }
modules.validate  {}                            → { ok, errors: string[] }
```
