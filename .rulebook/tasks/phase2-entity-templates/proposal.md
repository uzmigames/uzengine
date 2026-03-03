# Proposal: phase1-entity-templates

## Why

Spawning runtime entities (enemies, projectiles, pickups) currently requires writing C++ factory code or duplicating node definitions in `.uzscene`. Neither approach is accessible to AI agents or designers. A data-driven `.uzentity` file — listing component types and default field values — lets designers and AI tools define entity archetypes without C++ changes. The `EntityTemplateRegistry` instantiates them into the ECS with optional per-spawn field overrides.

## What Changes

- **`src/engine/ecs/entity_template.h/cpp`** — `EntityTemplate` data struct and `EntityTemplateRegistry` singleton
- **`src/engine/assets/entity_template_importer.h/cpp`** — `.uzentity` file parser (uses data-driven framework)
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.world:spawn(path, overrides) -> NodeRef` Lua API
- **`src/runtime/ipc/handlers/scene_handlers.cpp`** — `scene.spawn_template` IPC handler for editor "drag to scene" workflow
- **`.uzentity` format** — INI-like with `[meta]`, `[components]` section, field overrides
- **Tests** — parse, instantiate, field override, hot-reload

## Impact

- Affected specs: `docs/specs/scene-system.md` (spawn API), `docs/specs/data-driven-framework.md` (`.uzentity` format)
- Dependencies: `phase0-data-driven-framework` (format spec), `phase1-scene-system` (ECS/EnTT exists)
- Breaking change: NO — additive; existing `.uzscene` node definitions unchanged
- User benefit: AI-friendly entity authoring; designer-editable archetypes; live spawn from editor Asset Browser
