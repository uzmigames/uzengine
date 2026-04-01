<!-- doc-meta
title: Skeletal Asset Architecture — Quick Reference Card
description: One-page cheat sheet for skeleton/mesh/physics/animation relationships
date: 2026-03-28
status: reference
-->

# Skeletal Architecture: Quick Reference Card

## The 3-Way Split (UE5 Pattern)

```
┌─────────────────────────────────────────────────────────────────┐
│ SKELETON (1 shared asset)                                       │
│ ├── Joint hierarchy (parent-child relationships)                │
│ ├── Bind pose (rest position)                                   │
│ ├── Inverse bind matrices (for GPU skinning)                    │
│ └── Used by: Animations + SkeletalMeshes                        │
└─────────────────────────────────────────────────────────────────┘
    ↓
    ├─→ ANIMATION (many assets, all target same skeleton)
    │   ├── Keyframes per joint NAME ("Spine", "Leg_L", ...)
    │   ├── Can work on ANY mesh using this skeleton
    │   └── One animation = many visual results (mesh proportions differ)
    │
    ├─→ SKELETAL MESH (many assets, each unique)
    │   ├── Geometry (vertices, UVs, normals)
    │   ├── Skinning data (bone weights per vertex)
    │   ├── LOD levels (different vertex counts)
    │   └── References skeleton by name
    │
    └─→ PHYSICS ASSET (optional, many variants possible)
        ├── Collision bodies (per-joint capsules/boxes)
        ├── Constraints (joint limits, stiffness)
        └── Independent from skeleton changes
```

---

## Why This Design?

| Benefit | Explanation |
|---------|-------------|
| **Animation Sharing** | One "Walk.anim" plays on Hero + Villain + NPC (same skeleton) |
| **Retargeting** | Same animation, different meshes → different-looking results |
| **Memory Efficiency** | 10× animation data savings (1 library vs. 10 copies) |
| **Independent Iteration** | Artist changes mesh, animator changes animation, no conflicts |
| **Physics Flexibility** | Different physics per character (light/heavy armor) without new skeleton |

---

## The Two Critical Relationships

### Relationship 1: Animation ↔ Skeleton

```
Animation says:    "I target joints named: Spine, Arm_L, Leg_L, ..."
Skeleton says:     "I have joints named: Spine, Arm_L, Leg_L, ..."
                   ↓
                   ✓ Match → animation plays

Mesh uses skeleton:
    All meshes using HumanoidBase skeleton
    ↓
    Can all play animations targeting HumanoidBase
```

**Critical Rule:** Store joint references by NAME, not ID.
```cpp
// CORRECT
struct AnimationTrack {
    std::string bone_name;  // "Spine"
};

// WRONG — breaks retargeting
struct AnimationTrack {
    uint32_t bone_id;  // 1, 2, 3, ...
};
```

### Relationship 2: SkeletalMesh ↔ Skeleton

```
Mesh says:     "I was rigged to skeleton: HumanoidBase"
              "Here are my vertex skinning weights"

SkeletalMesh stores:
    - reference to skeleton
    - per-vertex: [bone_ids] and [bone_weights]
    - inverse bind matrices (computed from skeleton + mesh)
```

**Critical Rule:** Mesh stores inverse bind matrices based on reference skeleton.
```
If you assign a different skeleton → retargeting required
But if skeletons have same joint names → works automatically
```

---

## Asset Checklist

### Skeleton (.uzskeleton)

- [ ] All joints have unique IDs
- [ ] All joints have semantic names (not "Bone.001")
- [ ] All parent_ids valid (no cycles)
- [ ] One root joint (parent_id == -1)
- [ ] Bind pose in T-pose or consistent across all meshes
- [ ] Validated on load

### Animation (.uzanim)

- [ ] References correct skeleton
- [ ] All tracks have joint names that exist in skeleton
- [ ] Keyframe times are monotonically increasing
- [ ] Weights sum to 1.0 (or normalized on GPU)
- [ ] Validated on load

### SkeletalMesh (.uzmesh)

- [ ] Geometry vertices + indices valid
- [ ] Bone IDs per vertex within skeleton joint count
- [ ] Bone weights normalized to 1.0
- [ ] Inverse bind matrices computed
- [ ] References correct skeleton
- [ ] Validated on load

### PhysicsAsset (.uzphysics, Phase 4)

- [ ] All bone names exist in skeleton
- [ ] Parent/child constraint pairs valid
- [ ] Constraint limits sensible (not inverted)
- [ ] Mass distribution realistic

---

## Common Operations

### Load and Play Animation

```cpp
// Load shared assets
auto skeleton = AssetManager::Load<Skeleton>("humanoid");
auto walk = AssetManager::Load<AnimSequence>("walk");

// Create character
Entity hero = world.SpawnActor("hero");
auto mesh = hero.GetComponent<SkeletalMeshComponent>();

// Set assets (typically done in scene/prefab)
mesh->mesh = AssetManager::Load<Mesh>("hero_body");
mesh->skeleton = skeleton;
mesh->material = AssetManager::Load<Material>("hero_mat");

// Play animation
mesh->animation_controller->Play(walk);
// Hero walks using HumanoidBase skeleton
```

### Retarget Animation to Different Mesh

```cpp
Entity villain = world.SpawnActor("villain");
auto villain_mesh = villain.GetComponent<SkeletalMeshComponent>();

// Same skeleton, different mesh
villain_mesh->mesh = AssetManager::Load<Mesh>("villain_body");
villain_mesh->skeleton = skeleton;  // ← same skeleton as hero
villain_mesh->animation_controller->Play(walk);  // ← same animation

// Result: Both walk, but look different (mesh proportions differ)
```

### Attach Object to Bone

```cpp
auto skeletal_mesh = hero.GetComponent<SkeletalMeshComponent>();

// Create sword entity
Entity sword = world.SpawnActor<Sword>();

// Attach to "Hand.R" bone
skeletal_mesh->AttachToBone("Hand.R", sword->GetRootComponent());

// Now sword follows right hand as skeleton animates
```

### Get Bone World Transform

```cpp
auto transform = skeletal_mesh->GetBoneTransform("Head");
if (transform) {
    // Attach effect to head
    auto effect = world.SpawnActor<ParticleEffect>();
    effect->SetWorldTransform(*transform);
}
```

---

## Performance Targets (Per Character)

| Operation | Budget | Notes |
|-----------|--------|-------|
| Skeleton load | < 10 ms | 200 joints |
| Animation sample | < 0.1 ms | One track, linear interp |
| Joint matrix compute | < 0.5 ms | 200 joints, parent walk |
| GPU skinning | < 2 ms | 50K vertices, 128 bones |
| **Total per frame** | **~3 ms** | Should be <5% frame budget at 60 FPS |

At 100 characters: 300 ms CPU bound → need compute shader or LOD system.

---

## File Organization (Recommended)

```
res/characters/
├── shared/
│   └── humanoid.uzskeleton          ← SHARED (1 file)
│
├── hero/
│   ├── hero_body.uzmesh
│   ├── hero_armor.uzmesh            (variant)
│   └── controller.uzanimcontroller
│
├── villain/
│   ├── villain_body.uzmesh
│   ├── villain_scarred.uzmesh       (variant)
│   └── controller.uzanimcontroller
│
├── npc/
│   ├── npc_body.uzmesh
│   └── controller.uzanimcontroller
│
├── animations/                       ← SHARED (targets humanoid)
│   ├── idle.uzanim
│   ├── walk.uzanim
│   ├── run.uzanim
│   ├── jump.uzanim
│   ├── attack_sword.uzanim
│   └── ... (20 more)
│
└── physics/                          ← Phase 4
    ├── humanoid_light.uzphysics     (ranger, archer)
    ├── humanoid_standard.uzphysics
    └── humanoid_heavy.uzphysics     (knight, armored)
```

---

## Anti-Patterns (DON'T DO THIS)

```cpp
// ❌ DON'T: Store bone ID in animation
struct AnimationTrack {
    uint32_t bone_id;  // WRONG: breaks retargeting
};

// ✓ DO: Store bone name
struct AnimationTrack {
    std::string bone_name;  // "Spine"
};

// ❌ DON'T: Embed skeleton in mesh
struct SkeletalMesh {
    Skeleton skeleton;  // WRONG: can't retarget or share
};

// ✓ DO: Reference external skeleton
struct SkeletalMesh {
    ResourceHandle<Skeleton> skeleton;
};

// ❌ DON'T: Store physics in skeleton
struct Skeleton {
    std::vector<RigidBody> physics_bodies;  // WRONG
};

// ✓ DO: Separate physics asset
struct PhysicsAsset {
    std::vector<RigidBody> bodies;
};

// ❌ DON'T: Duplicate animations per character
animations/
├── hero_walk.uzanim       // WRONG
├── villain_walk.uzanim
├── npc_walk.uzanim

// ✓ DO: One animation per skeleton
animations/
└── walk.uzanim  // targets humanoid_base
```

---

## UzEngine Implementation Status

### Phase 3 (Current): 2-Way Split

```cpp
struct Skeleton {
    std::vector<Joint> joints;
    // ✓ Implemented
};

struct SkeletalMesh {
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Skeleton> skeleton;
    std::vector<VertexSkinData> skin;
    // ✓ Spec complete (docs/engine/19-animation/03-skeletal-mesh.md)
};

struct AnimationSequence {
    std::vector<AnimationTrack> tracks;  // by joint name
    ResourceHandle<Skeleton> skeleton;
    // ✓ Spec complete (docs/engine/19-animation/01-overview.md)
};
```

### Phase 4 (Planned): 3-Way Split

```cpp
// Phase 3 unchanged, just add:

struct PhysicsAsset {
    std::vector<RigidBody> bodies;
    std::vector<Constraint> constraints;
    // Planned for Phase 4 (ragdoll, complex physics)
};

// No breaking changes to skeleton/mesh/animation APIs
```

---

## Decision Tree: Should You Separate?

```
Is this a game with skeletal animation?
    ├─ NO → Don't use skeletal meshes, use static mesh + morph targets
    │
    └─ YES
        ├─ Do you want to reuse animations across multiple characters?
        │   ├─ NO → Can embed skeleton in mesh (simpler)
        │   └─ YES → MUST separate skeleton (animation retargeting)
        │
        ├─ Do you want physics (ragdoll, hit detection, etc.)?
        │   ├─ NO (Phase 3) → Skeleton + SkeletalMesh only
        │   └─ YES (Phase 4) → Add PhysicsAsset (separate)
        │
        └─ Result: UzEngine 2-way split (Phase 3) → 3-way split (Phase 4)
```

**For UzEngine: Start with 2-way split (Phase 3), upgrade to 3-way when adding physics (Phase 4).**

---

## Reading Order

1. **This file** (you are here) — 5 min overview
2. **SKELETAL-ARCHITECTURE-SUMMARY.md** — 15 min, answers all 6 questions
3. **ue5-skeletal-asset-architecture.md** — 30 min, deep UE5 analysis + UzEngine mapping
4. **uzengine-skeletal-implementation-guide.md** — 45 min, C++23 code patterns + JSON format
5. **skeletal-retargeting-and-animation-integration.md** — 30 min, retargeting + ECS integration

