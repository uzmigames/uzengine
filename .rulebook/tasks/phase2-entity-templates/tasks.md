## 1. Prerequisites

- [ ] 1.1 Confirm `phase0-data-driven-framework` spec is complete — read `.uzentity` format section
- [ ] 1.2 Confirm `phase1-scene-system` is complete — EnTT registry and component types exist
- [ ] 1.3 Read `docs/specs/scene-system.md` — understand component type registry and node creation API
- [ ] 1.4 Read `docs/specs/data-driven-framework.md` — understand `[meta]`, `IAssetImporter` interface, hot-reload protocol

## 2. .uzentity Format Implementation

- [ ] 2.1 Create `src/engine/assets/entity_template_importer.h/cpp`
- [ ] 2.2 Implement `EntityTemplateImporter` implementing `IAssetImporter`
- [ ] 2.3 Implement `parse(path, ParsedDoc&)` — read `[meta]` block: validate `type = uzentity`, parse `version`
- [ ] 2.4 Implement `[components]` section parsing: each key-value is `ComponentType.field = value`
  - Example: `CMeshRenderer.mesh = "res://meshes/enemy.glb"`
  - Example: `CTransform.scale = 1.5,1.5,1.5`
- [ ] 2.5 Implement `[defaults]` section parsing: top-level entity metadata (name, tags, layer)
- [ ] 2.6 Implement `[overridable]` section: list of fields that callers may override at spawn time
- [ ] 2.7 Register `EntityTemplateImporter` for `.uzentity` extension in `AssetImporterRegistry`

## 3. EntityTemplate Data Struct

- [ ] 3.1 Create `src/engine/ecs/entity_template.h`
- [ ] 3.2 Define `ComponentFieldValue` variant: bool/int32/float/string/vec3
- [ ] 3.3 Define `ComponentSpec` struct: `component_type_id`, `field_overrides: map<string, ComponentFieldValue>`
- [ ] 3.4 Define `EntityTemplate` struct: `name`, `tags[]`, `layer`, `components: vector<ComponentSpec>`, `overridable_fields: set<string>`
- [ ] 3.5 Create `src/engine/ecs/entity_template_registry.h/cpp` — `EntityTemplateRegistry` singleton
- [ ] 3.6 Implement `EntityTemplateRegistry::register(path, EntityTemplate)` — stores by path key
- [ ] 3.7 Implement `EntityTemplateRegistry::get(path) -> const EntityTemplate*` — returns nullptr if not found
- [ ] 3.8 Implement `EntityTemplateRegistry::reload(path)` — re-imports from disk, updates entry

## 4. Instantiation API

- [ ] 4.1 Create `src/engine/ecs/entity_spawner.h/cpp` — `EntitySpawner` utility
- [ ] 4.2 Implement `EntitySpawner::spawn(registry, template, overrides, parent_node) -> entt::entity`
  - Create `entt::entity` in registry
  - Add default components from template `ComponentSpec` list
  - Apply field overrides (both template defaults and caller overrides)
  - Set parent in scene graph
- [ ] 4.3 Implement field override application: `ComponentReflection::set_field(entity, component_type_id, field_name, value)`
- [ ] 4.4 Implement override validation: reject fields not in `overridable_fields` set (warn, don't error)
- [ ] 4.5 Implement `EntitySpawner::despawn(registry, entity)` — removes entity and all children

## 5. Lua API

- [ ] 5.1 Extend `ctx.world` table in `lua_binding.cpp`
- [ ] 5.2 Implement `ctx.world:spawn(path) -> NodeRef` — load template, instantiate at world origin
- [ ] 5.3 Implement `ctx.world:spawn(path, overrides_table) -> NodeRef` — with field overrides from Lua table
- [ ] 5.4 Implement `ctx.world:spawn(path, overrides_table, parent_node_ref) -> NodeRef` — with explicit parent
- [ ] 5.5 Implement `ctx.world:despawn(node_ref)` — removes entity and children from registry
- [ ] 5.6 Lua override table format: `{ ["CTransform.scale"] = {1.5, 1.5, 1.5}, ["CMeshRenderer.visible"] = false }`

## 6. Hot-Reload Integration

- [ ] 6.1 Register `EntityTemplateImporter` as `IAssetReloadSubscriber`
- [ ] 6.2 On `.uzentity` file change: `EntityTemplateRegistry::reload(path)` re-parses and updates registry
- [ ] 6.3 After reload: find all live entities instantiated from this template, update mutable field values
- [ ] 6.4 Log reload event: `[EntityTemplate] reloaded: res://entities/enemy.uzentity`

## 7. IPC Handler

- [ ] 7.1 Add `scene.spawn_template` handler to `src/runtime/ipc/handlers/scene_handlers.cpp`
- [ ] 7.2 Handler body: `{ path, position, overrides }` → `EntitySpawner::spawn()` on main thread → return `node_id`
- [ ] 7.3 Register handler under `#ifdef UZ_EDITOR`

## 8. Testing

- [ ] 8.1 Unit test: parse valid `.uzentity` file → `EntityTemplate` struct with correct component specs
- [ ] 8.2 Unit test: parse `.uzentity` with unknown component type → warning logged, other components still parsed
- [ ] 8.3 Unit test: `spawn(template, {})` creates entity with all default component values
- [ ] 8.4 Unit test: `spawn(template, {"CMeshRenderer.visible": false})` applies override correctly
- [ ] 8.5 Unit test: override of non-overridable field logs warning and is ignored
- [ ] 8.6 Unit test: `despawn` removes entity and all children from registry
- [ ] 8.7 Integration test: `ctx.world:spawn("res://entities/enemy.uzentity")` returns valid NodeRef
- [ ] 8.8 Integration test: hot-reload `.uzentity` file — live entity field updated within 100ms

## 9. Documentation

- [ ] 9.1 Update `docs/specs/scene-system.md` — add `.uzentity` format and `ctx.world:spawn` API
- [ ] 9.2 Create `docs/specs/entity-templates.md` — full format spec with annotated example
- [ ] 9.3 Update `docs/guides/scene-authoring.md` — "Entity Templates" section
