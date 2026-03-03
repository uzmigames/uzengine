## Context

Deferred from `phase1-scene-system`. Adds `.uzproj` TOML project configuration
loaded at engine startup. Required before asset browser, play mode, and multi-scene workflows.

## 1. TOML Dependency

- [ ] 1.1 Add `toml++` (`tomlplusplus`) to `vcpkg.json`
- [ ] 1.2 Add `find_package(tomlplusplus CONFIG REQUIRED)` and `target_link_libraries` in CMakeLists.txt
- [ ] 1.3 Verify vcpkg installs cleanly on Linux + Windows

## 2. ProjectConfig (`src/engine/core/project_config.h/.cpp`)

- [ ] 2.1 `ProjectConfig::load(path)` — parse `.uzproj` TOML file, return `Result<ProjectConfig>`
- [ ] 2.2 Fields: `name` (string), `version` (string), `main_scene` (path), `renderer.vsync` (bool), `renderer.target_fps` (int), `renderer.resolution` (int2)
- [ ] 2.3 `ProjectConfig::save(path)` — serialize back to TOML
- [ ] 2.4 `ProjectConfig::defaults()` — returns config with sensible defaults
- [ ] 2.5 Error handling: missing file, malformed TOML, missing required fields

## 3. Engine Integration

- [ ] 3.1 Engine loads `project.uzproj` on startup from CWD or `--project <path>` CLI arg
- [ ] 3.2 If no `.uzproj` found: use `ProjectConfig::defaults()` and log warning
- [ ] 3.3 `scene.load` auto-loads `main_scene` on startup if set
- [ ] 3.4 `engine.getProjectConfig` IPC handler — returns current config as JSON
- [ ] 3.5 `engine.setProjectConfig` IPC handler — updates config and saves to disk

## 4. `project.uzproj` Template

- [ ] 4.1 Create `project.uzproj` at project root with default values
- [ ] 4.2 Document all fields with inline TOML comments

## 5. Tests (`tests/core/test_project_config.cpp`)

- [ ] 5.1 Load valid `.uzproj` — all fields parsed correctly
- [ ] 5.2 Load minimal `.uzproj` (only required fields) — defaults applied for optional
- [ ] 5.3 Load missing file — returns error, not crash
- [ ] 5.4 Load malformed TOML — returns error with message
- [ ] 5.5 Round-trip: load → save → load → same values
- [ ] 5.6 IPC handler tests: `engine.getProjectConfig`, `engine.setProjectConfig`
- [ ] 5.7 ≥95% line + branch coverage
