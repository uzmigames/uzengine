## 1. Prerequisites

- [ ] 1.1 Confirm `phase3-physics-system` is complete — Jolt Physics, `CCollider`, collision events exist
- [ ] 1.2 Confirm `phase0-data-driven-framework` spec is complete — `.uzphysmat` format defined
- [ ] 1.3 Read `docs/specs/physics-system.md` — understand `CCollider` fields, `JPH::PhysicsMaterial`, `CollisionEvent`
- [ ] 1.4 Read Jolt Physics `PhysicsMaterial` API — `JPH::PhysicsMaterial` base class, friction/restitution accessors

## 2. .uzphysmat Format

- [ ] 2.1 Define `[meta]` block: `type = uzphysmat`, `version = 1`, `uuid`
- [ ] 2.2 Define `[material]` section: `name` (display), `surface_type` (string tag: `wood`/`metal`/`stone`/`grass`/`water`/custom)
- [ ] 2.3 Define `[physics]` section: `friction` (0–1), `restitution` (0–1), `density` (kg/m³)
- [ ] 2.4 Define `[friction_combine]` and `[restitution_combine]` entries: `mode` (`average`/`min`/`max`/`multiply`) — maps to Jolt `EConstraintSpace`
- [ ] 2.5 Write default material files: `materials/physics/default.uzphysmat`, `wood.uzphysmat`, `metal.uzphysmat`, `stone.uzphysmat`

## 3. PhysicsMaterial Data Struct

- [ ] 3.1 Create `src/engine/physics/physics_material.h`
- [ ] 3.2 Define `PhysicsMaterial` struct: `name`, `surface_type`, `friction`, `restitution`, `density`, combine modes
- [ ] 3.3 Create `UzPhysicsMaterial : JPH::PhysicsMaterial` — wraps `PhysicsMaterial`, implements Jolt interface
- [ ] 3.4 Create `PhysicsMaterialRegistry` singleton: `register(path, PhysicsMaterial)`, `get(path) -> const PhysicsMaterial*`, `get_jolt(path) -> JPH::PhysicsMaterial*`
- [ ] 3.5 Pre-register built-in default material at path `"res://materials/physics/default.uzphysmat"`

## 4. PhysicsMaterialImporter

- [ ] 4.1 Create `src/engine/assets/physics_material_importer.h/cpp`
- [ ] 4.2 Implement `PhysicsMaterialImporter` implementing `IAssetImporter`
- [ ] 4.3 Implement `parse(path, ParsedDoc&)` — fill `PhysicsMaterial` from sections
- [ ] 4.4 Implement `reimport(path)` — update registry entry; existing Jolt shapes automatically use new values (Jolt material pointers are shared)
- [ ] 4.5 Register for `.uzphysmat` extension

## 5. CCollider Integration

- [ ] 5.1 Update `ecs/components.h` — replace `friction: float` and `restitution: float` fields in `CCollider` with `material_path: string` (default: `"res://materials/physics/default.uzphysmat"`)
- [ ] 5.2 Update `.uzscene` parser — handle legacy `friction`/`restitution` fields as fallback (migration path)
- [ ] 5.3 Update `PhysicsSystem::create_shape(CCollider)` — look up `PhysicsMaterialRegistry::get_jolt(material_path)`, pass to Jolt shape creation
- [ ] 5.4 Verify Jolt `JPH::BodyCreationSettings.mMaterial` is set correctly

## 6. Collision Event Extension

- [ ] 6.1 Update `CollisionEvent` struct — add `surface_type_a: string`, `surface_type_b: string` fields
- [ ] 6.2 Update `PhysicsSystem::on_contact_added` — populate surface types from `PhysicsMaterialRegistry` lookup via `body_id → entity → CCollider → material_path`
- [ ] 6.3 Update `CollisionEvent` EventBus dispatch — new fields available to Lua `on_collision` callbacks
- [ ] 6.4 Update Lua `collision_event` userdata — expose `.surface_type_a` and `.surface_type_b` fields

## 7. Editor IPC Integration

- [ ] 7.1 Expose `material_path` field in Inspector panel for `CCollider` — file picker filtering `.uzphysmat`
- [ ] 7.2 Add `asset.validate_physmat` IPC handler — returns parsed material summary for Inspector preview

## 8. Testing

- [ ] 8.1 Unit test: parse `wood.uzphysmat` → `PhysicsMaterial { friction=0.6, restitution=0.1, surface_type="wood" }`
- [ ] 8.2 Unit test: `PhysicsMaterialRegistry::get` returns same pointer on second call (no duplicate alloc)
- [ ] 8.3 Unit test: `CCollider` with default `material_path` → Jolt shape created with default material
- [ ] 8.4 Unit test: collision between `metal` and `stone` surfaces → `CollisionEvent.surface_type_a == "metal"`, `surface_type_b == "stone"`
- [ ] 8.5 Unit test: hot-reload `.uzphysmat` — subsequent collision uses updated friction value
- [ ] 8.6 Integration test: Lua `on_collision` callback receives `.surface_type_a` field

## 9. Documentation

- [ ] 9.1 Update `docs/specs/physics-system.md` — "Physics Materials" section with `.uzphysmat` format and `CCollider.material_path`
- [ ] 9.2 Create `docs/specs/physics-materials.md` — full format spec with annotated examples
- [ ] 9.3 Update `docs/guides/physics-guide.md` — "Surface Types and Materials" section
