# Proposal: phase3-compound-colliders

## Why
Complex objects such as car chassis, furniture, and character hitboxes require multiple primitive shapes combined into a single physics body. The current system allows only one collider component per entity, forcing developers to either use an imprecise single box or attach invisible child entities as a workaround. A car body needs wheel wells, doors, and undercarriage as separate collision shapes that all move together as one rigid body. Without compound colliders, collision fidelity for any non-trivial object requires unsupported hacks.

## What Changes
- Add `CCompoundCollider` ECS component containing a `std::vector<ColliderShape>` where each `ColliderShape` holds type (box/sphere/capsule/convex hull), size parameters, and a local offset `Transform` (position + rotation relative to body origin)
- Implement Jolt backend using `JPH::StaticCompoundShapeSettings` assembled from each sub-shape and baked into a single `JPH::Body`
- Support runtime sub-shape add/remove via `CompoundColliderSystem::rebuild_body(entity)` — re-creates the Jolt body preserving velocity
- Editor gizmo draws each sub-shape outline separately with its local offset applied; selected sub-shape highlighted in yellow
- glTF import reads `_colliders` array in node `extras` to auto-populate `CCompoundCollider` from mesh-authored collision nodes
- Inspector panel lists sub-shapes with per-shape add/remove buttons and offset fields
- Serialization to `.uzscene` as `[[compound_collider]]` INI array block per sub-shape

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/asset-pipeline.md`, `docs/specs/editor-gizmos.md`
- Affected code: `src/physics/collider_system.cpp`, `src/physics/collider_system.h`, `src/editor/inspector_physics.cpp`, `src/assets/gltf_importer.cpp`, `src/scene/components.h`
- Breaking change: NO (existing `CCollider` single-shape component is unchanged; `CCompoundCollider` is additive)
- User benefit: Vehicles, furniture, and character hitbox hierarchies get accurate collision without child-entity workarounds. glTF artists can author collision directly in Blender and have it imported automatically.
