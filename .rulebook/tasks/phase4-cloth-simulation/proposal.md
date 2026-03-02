# Proposal: phase4-cloth-simulation

## Why

Capes, flags, curtains, and cloth-covered furniture require cloth simulation for visual authenticity. CPU spring-mass simulation is too expensive at high cloth resolution; GPU compute is impractical to author. Jolt Physics `SoftBody` provides a production-quality PBD (Position-Based Dynamics) cloth solver that integrates naturally with the existing physics world, handles collision with rigid bodies and characters, and blends with skeletal mesh skinning for attached cloth.

## What Changes

- **`ecs/components.h`** — `CCloth` component: mesh path, attachment bones, wind zone reference, LOD distance
- **`src/engine/physics/cloth_system.h/cpp`** — `ClothSystem`: creates Jolt `SoftBody`, manages skinned blend
- **`src/engine/renderer/passes/cloth_skin_pass.h/cpp`** — compute pass: blend Jolt vertex positions with skeleton pose
- **Wind zones** — `CWindZone` component with directional + turbulence wind affecting all cloth in range
- **Tests** — cloth creation, gravity drape, wind response, collision, skinned blend, LOD disable

## Impact

- Affected specs: `docs/specs/physics-system.md` (cloth section)
- Dependencies: `phase3-physics-system` (Jolt), `phase3-skeletal-mesh` (bone transforms for cloth attachment)
- Breaking change: NO — additive; non-cloth meshes unaffected
- User benefit: realistic cape/flag/curtain simulation without custom physics code; collision with player and world
