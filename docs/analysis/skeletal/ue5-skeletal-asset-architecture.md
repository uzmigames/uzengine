<!-- doc-meta
title: UE5 Skeletal Asset Architecture Analysis
description: Why UE5 separates skeleton, skeletal mesh, and physics asset, and implications for UzEngine
date: 2026-03-28
status: reference
-->

# UE5 Skeletal Asset Architecture: Why the 3-Way Split?

## Executive Summary

UE5 separates skeletal data into 3 assets—**USkeleton**, **USkeletalMesh**, **UPhysicsAsset**—because each solves a different architectural problem:

1. **USkeleton** → shared joint hierarchy + animation target (1 skeleton, N meshes + N animations)
2. **USkeletalMesh** → mesh-specific deformation (geometry binding to skeleton)
3. **UPhysicsAsset** → physics constraints independent of mesh topology

This is **NOT** just for memory efficiency (though that's a benefit). It's about **decoupling animation from representation**.

---

## 1. USkeleton: The Shared Joint Hierarchy

### What It Stores

```cpp
// UE5 Skeleton (conceptual)
class USkeleton {
public:
    // Joint hierarchy — the ONLY thing that matters for animation
    TArray<FBoneNode> BoneTree;  // parent_idx, name, transform

    // Socket attachments (bone-relative positions for swords, helmets, etc.)
    TArray<FSocketData> Sockets;

    // Retarget metadata (allows animating different character proportions)
    TMap<FName, FBoneReduction> BoneReductionMap;

    // IK chains (for procedural animation)
    TArray<FIKChain> IKChains;
};
```

### Why Separate?

**Animation retargeting** — the killer use case:

```
One skeleton: "HumanoidBase" (spine, arms, legs, 50 joints)
    ↓
N meshes can use it:
    - "Hero_Male_Tall"     (52 bones, scaled proportions)
    - "Hero_Female_Short"  (52 bones, different proportions)
    - "Hero_Male_Muscular" (52 bones, different mesh topology)

N animations can target it:
    - "Walk_Forward"
    - "Attack_Sword"
    - "Jump"
    - (same animation plays on all 3 meshes, looks different because mesh proportions differ)
```

**This is NOT possible with per-mesh skeletons.** If the skeleton is baked into the mesh asset, you can't retarget animations.

### The Coupling Problem (If Skeleton Were In Mesh)

```cpp
// BAD: skeleton embedded in mesh
struct BadSkeletalMesh {
    Mesh mesh;
    Skeleton skeleton;  // ← locked to THIS mesh forever

    // Result: "Walk" animation only works on THIS mesh
    //         Cannot reuse animation on other meshes
};

// GOOD: skeleton is shared
struct GoodSkeletalMesh {
    Mesh mesh;
    ResourceRef<Skeleton> skeleton;  // ← points to external skeleton

    // Result: "Walk" animation works on ANY mesh that refs this skeleton
    //         One animation library, many meshes
};
```

### Memory Benefit (Secondary)

If you have 10 character meshes all using "HumanoidBase" skeleton:

```
Per-mesh skeleton:
    10 × (128 bones × 64 bytes per bone) = 81.9 KB per instance
    × 10 meshes = 819 KB

Shared skeleton:
    1 × (128 bones × 64 bytes per bone) = 8.19 KB
    + references from each mesh = negligible

Savings: ~810 KB per character instance
```

More importantly for animation:

```
Per-mesh animation storage:
    10 meshes × 5 animations × (128 bones × 4 floats × 3 frames/sec × 10 sec)
    ≈ 2-3 MB

Shared skeleton + shared animations:
    5 animations × (128 bones × ...) = 256 KB
    (animations are independent of mesh)
```

---

## 2. USkeletalMesh: The Mesh-Specific Binding

### What It Stores

```cpp
// UE5 SkeletalMesh
class USkeletalMesh {
public:
    // ─── Mesh Geometry ───────────────────────────────────────
    TArray<FSkeletalMeshLODRenderData> LODRenderData;  // vertices, indices, UVs, normals, tangents

    // ─── Skeleton Reference ──────────────────────────────────
    USkeleton* RefSkeleton;  // ← link to external skeleton

    // ─── Skinning Data ───────────────────────────────────────
    TArray<SkinnedVertex> SkinnedVertices;
    struct SkinnedVertex {
        uint8 BoneIndices[4];    // which joints influence this vertex
        uint8 BoneWeights[4];    // how much (0-255, normalized on GPU to 0-1)
    };

    // ─── Vertex Bone Inverse Matrices ────────────────────────
    // These transform from world space back to BIND POSE space
    TArray<FMatrix44f> RefBasesInvMatrix;

    // ─── Morph Targets ──────────────────────────────────────
    TArray<FMorphTarget> MorphTargets;  // blend shapes (smile, blink, etc.)

    // ─── Socket Attachments ──────────────────────────────────
    TArray<FSocketData> Sockets;  // bone-relative attachment points
};
```

### RefSkeleton vs. Skeleton

This is a critical distinction:

```
RefSkeleton (Reference Skeleton):
    - The skeleton THIS mesh was CREATED WITH
    - Baked at import time (from the FBX)
    - Has inverse bind matrices specific to this mesh's bind pose
    - Example: "Hero_Armature" as it appeared in Maya

Skeleton (LinkedSkeleton):
    - Can be REASSIGNED at runtime (for retargeting)
    - Allows mapping different skeleton to same mesh
    - Requires bone name matching or IK chain setup
    - Example: change to "MonsterHumanoid" skeleton on the fly

In practice:
    RefSkeleton ≈ "the skeleton this mesh was rigged to"
    Skeleton    ≈ "the skeleton this mesh is currently using"
```

### Why Not Bake Skeleton Into Mesh?

The inverse bind matrices are mesh-specific:

```
For a given mesh vertex V:
    V_skinned = Σ(Weight[i] × JointMatrix[i] × InverseBindMatrix[i] × V_local)

The InverseBindMatrix DEPENDS ON:
    - The bind pose at the time of rigging
    - The mesh's geometry (where vertices are positioned)

If you change the skeleton's bind pose → MUST recompute InverseBindMatrix
    → MUST re-import the mesh

Therefore:
    Skeleton (constant joint hierarchy) ← shared across meshes + animations
    Mesh (constant vertex positions) ← specific to this mesh
    InverseBindMatrix ← specific to (mesh, skeleton) pair
```

### LOD Levels

Each LOD (Level of Detail) has its own vertex and skinning data:

```cpp
FSkeletalMeshLODRenderData LOD0 {
    vertices: 45,000
    indices: 120,000
    bones per vertex: 4
    active bones: 128
};

FSkeletalMeshLODRenderData LOD1 {
    vertices: 15,000
    indices: 35,000
    bones per vertex: 4
    active bones: 64
};

FSkeletalMeshLODRenderData LOD2 {
    vertices: 5,000
    indices: 10,000
    bones per vertex: 2
    active bones: 32
};
```

The bone weight reduction is manual (done at import), not automatic.

---

## 3. UPhysicsAsset: Why Separate From Skeleton?

### What It Stores

```cpp
// UE5 PhysicsAsset
class UPhysicsAsset {
public:
    // ─── Rigid Bodies ────────────────────────────────────────
    TArray<FBodySetupData> BodySetups;
    struct FBodySetupData {
        FName BoneName;            // which joint this body is attached to
        FKBoxElem Boxes;           // collision shapes
        FKSphereElem Spheres;
        FKCapsuleElem Capsules;
        FKConvexElem Convex;       // for complex shapes
        EPhysicsType PhysicsType;  // Dynamic, Kinematic, Static
    };

    // ─── Constraints ─────────────────────────────────────────
    TArray<FConstraintData> Constraints;
    struct FConstraintData {
        FName ParentBone;
        FName ChildBone;
        EConstraintType Type;      // Hinge, Ball-and-Socket, etc.
        FVector Limits;            // rotation/linear limits
    };
};
```

### Why NOT Part of Skeleton?

Three reasons:

**1. Different Use Cases**

```
Skeleton:
    - Animation playback
    - Retargeting (can change without physics changing)
    - Deformation (visual only)
    - Used by animator/VFX teams

PhysicsAsset:
    - Collision detection
    - Ragdoll simulation
    - Hit detection
    - Used by physics/gameplay teams

If they're one asset, changing physics requires re-exporting the skeleton
    → breaks all animations
```

**2. Many-to-Many Mapping**

```
Example: Character in game

    Skeleton: "HumanoidBase" (animation rig)
    ├── SkeletalMesh: "Hero" (visual appearance)
    ├── SkeletalMesh: "HeroArmor" (variant)
    └── PhysicsAsset: "Humanoid_Physics"

But physics might differ:
    - Heavy Armor variant: heavier bodies, tighter constraints
    - Light Archer variant: lighter bodies, more flexible shoulders

With shared skeleton, you can have N physics assets for 1 skeleton
```

**3. Physics Iteration Without Mesh Re-export**

```
Designer's workflow:
    1. Import character mesh (exports with default physics capsule)
    2. Play in-game
    3. "Jump collision isn't right" → adjust PhysicsAsset only
    4. No mesh re-export needed

If physics were in mesh:
    1-4: Would need to re-export entire mesh + skeleton
        (also re-validates all skeletal data, breaks animation re-import)
```

---

## 4. The Practical Benefit: One Skeleton, Many Assets

This is the core architectural win:

```
┌─────────────────────────────────────────────────────────┐
│ Shared Skeleton: "HumanoidBase" (50 joints)            │
│   - Animation-driven (no mesh data)                     │
│   - Retargeting metadata (bone proportions)             │
│   - IK chains (procedural animation)                    │
└─────────────────────────────────────────────────────────┘
         ↓                    ↓                    ↓
    ┌────────────┐    ┌────────────┐    ┌────────────┐
    │SkeletalMesh│    │SkeletalMesh│    │SkeletalMesh│
    │ "Hero"     │    │ "Villain"  │    │ "NPC"      │
    │ 52 joints  │    │ 52 joints  │    │ 52 joints  │
    │ (same skel)│    │ (same skel)│    │ (same skel)│
    └────────────┘    └────────────┘    └────────────┘
         ↓                    ↓                    ↓
    ┌────────────┐    ┌────────────┐    ┌────────────┐
    │PhysicsAsset│    │PhysicsAsset│    │PhysicsAsset│
    │ "Humanoid" │    │ "Heavy"    │    │ "Light"    │
    │ (80 kg)    │    │ (120 kg)   │    │ (60 kg)    │
    └────────────┘    └────────────┘    └────────────┘

Result:
    - 5 animations × 1 skeleton = works on all 3 characters
    - Mesh variations (visual) don't affect animation
    - Physics variations (gameplay) don't affect animation
    - All assets can be developed/iterated independently
```

---

## 5. Animation Assets: How Animations Reference Skeleton

### UAnimSequence

```cpp
// UE5 AnimSequence
class UAnimSequence {
public:
    USkeleton* Skeleton;  // ← which skeleton this animation targets

    // Keyframe data: per-bone, per-frame
    TArray<FBoneAnimationTrack> Tracks;
    struct FBoneAnimationTrack {
        FName BoneName;  // must match a bone in the skeleton
        TArray<FVector> TranslationKeys;  // position keyframes
        TArray<FQuat> RotationKeys;       // rotation keyframes
        TArray<FVector> ScaleKeys;        // scale keyframes
        TArray<float> Times;              // frame times
    };

    // Animation events (notify at frame 30: "play footstep sound")
    TArray<FAnimNotifyEvent> Notifies;

    // Length + frame rate
    float Duration;
    float SamplingFrameRate;
};
```

### One Animation, Many Meshes

```cpp
// C++ code
USkeleton* skeleton = LoadAsset<USkeleton>("HumanoidBase");
UAnimSequence* walk = LoadAsset<UAnimSequence>("Walk");  // targets HumanoidBase
UAnimSequence* run = LoadAsset<UAnimSequence>("Run");

// Apply to different meshes
for (auto mesh : GetAllSkeletalMeshesUsingSkeleton(skeleton)) {
    mesh->PlayAnimation(walk);  // ← same animation on all
    // walks look different because meshes have different proportions
}
```

**Why this works:**

```
Animation "Walk" → targets skeleton "HumanoidBase"
    Tracks: Spine, Arm_L, Arm_R, Leg_L, Leg_R, ...

Mesh "Hero" → uses skeleton "HumanoidBase"
    References same joint names
    → animation plays correctly

Mesh "Villain" → uses skeleton "HumanoidBase"
    References same joint names
    → animation plays correctly

Mesh "NPC" → uses skeleton "HumanoidBase"
    References same joint names
    → animation plays correctly
```

---

## 6. Should UzEngine Use 3-Way or 2-Way Split?

### Current UzEngine Design (2-Way Split)

From `docs/engine/19-animation/03-skeletal-mesh.md`:

```cpp
// UzEngine: 2-way split
struct SkeletalMeshComponent {
    ResourceHandle<Mesh> mesh;           // geometry + skinning data
    ResourceHandle<Skeleton> skeleton;   // joint hierarchy + bind pose
    ResourceHandle<Material> material;

    // Mesh.uzmesh internally references skeleton.uzskeleton
};
```

**Advantages:**
- Simpler pipeline (fewer asset types)
- No separate physics system needed yet (Phase 3)
- Appropriate for lean runtime

**Limitations:**
- Cannot easily swap skeletons on a mesh (retargeting requires code)
- Physics would have to live in mesh or as separate system
- If physics gets added later, will need refactor

### Recommendation: Upgrade to 3-Way for Physics (Phase 3→4 Transition)

**Proposed UzEngine Architecture (3-Way):**

```cpp
// Phase 3: Keep current 2-way
struct Skeleton {
    std::vector<Joint> joints;
    // joint hierarchy, bind pose, inverse bind matrices
};

struct SkeletalMesh {
    ResourceHandle<Mesh> mesh;           // geometry
    ResourceHandle<Skeleton> skeleton;   // reference to skeleton
    std::vector<VertexSkinData> skin;    // weights
};

// Phase 4: Add physics
struct PhysicsAsset {
    std::vector<RigidBody> bodies;       // per-joint collision shapes
    std::vector<Constraint> constraints; // joint constraints

    // Separate file: character.uzphysics
    // Optional, shared or per-character
};
```

**Why this upgrade path makes sense:**

1. **No breaking changes** — existing skeletons/meshes unchanged
2. **Ragdoll addition** — without it, Phase 4 ragdoll is messy (where does physics live?)
3. **Retargeting prep** — if multiplayer/modding ever needed, path exists

---

## 7. Animation System: Integration Points

### Per-Frame Animation Pipeline

```
┌─────────────────────────────────────────────────────────┐
│ AnimationController (state machine)                     │
│   - Evaluates current state                            │
│   - Selects active animations                          │
│   - Blends weighted animations                         │
│   - Returns: joint_local_poses[128]                    │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ SkeletalMeshComponent::UpdateSkinning()                │
│   - Gets joint_local_poses from controller             │
│   - Computes world matrices (local → parent → world)   │
│   - Uploads joint_matrices buffer to GPU               │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ GPU Vertex Shader (or Compute Skinning)                │
│   - Reads joint_matrices constant buffer               │
│   - Reads VertexSkinData (bone_ids, weights)           │
│   - Transforms vertex: V_final = Σ(weight[i] × M[i] × V)
│   - Outputs skinned position                           │
└─────────────────────────────────────────────────────────┘
```

### How Animations Know About Skeleton

```
AnimSequence:
    - Stores: tracks per joint name ("Spine", "Arm_L", "Leg_R")
    - Does NOT store: joint indices (those are per-skeleton)

At runtime:
    1. Load AnimSequence (has joint names)
    2. Load SkeletalMesh (has skeleton reference)
    3. Skeleton.FindJoint("Spine") → returns joint_id
    4. AnimSequence.GetValue("Spine", time) → returns Transform
    5. Apply Transform to joint_id in skeleton
```

This is why multiple meshes can use one animation + one skeleton:

```
Animation "Walk":
    Keyframes for joint names: ["Spine", "Leg_L", "Leg_R", ...]

Skeleton "HumanoidBase":
    Joint map: {"Spine": 1, "Leg_L": 5, "Leg_R": 6, ...}

Mesh "Hero":
    Uses "HumanoidBase"
    → animation plays (joint names match)

Mesh "Villain":
    Uses "HumanoidBase"
    → animation plays (joint names match)
```

---

## 8. UzEngine Architectural Recommendations

### For Phase 3 (Current Work)

**Keep the 2-way split:**

```cpp
// skeleton.h
struct Skeleton {
    std::vector<Joint> joints;

    /// Find joint by exact name match
    int32_t GetJointByName(std::string_view name) const;
};

// skeletal_mesh_component.h
struct SkeletalMeshComponent : PrimitiveComponent {
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Skeleton> skeleton;
    ResourceHandle<Material> material;

    /// Get joint world transform (for attachments, VFX)
    Transform GetBoneTransform(std::string_view bone_name) const;
};

// animation_sequence.h
struct AnimSequence {
    ResourceHandle<Skeleton> skeleton;  // ← which skeleton to target

    std::vector<AnimationTrack> tracks;  // per-joint-name keyframes
    struct AnimationTrack {
        std::string bone_name;
        std::vector<Vec3> translation_keys;
        std::vector<Quat> rotation_keys;
        std::vector<float> times;
    };
};
```

**Rules for Phase 3:**
1. Store joint hierarchy by name, not ID (enables retargeting later)
2. AnimSequence references skeleton by name, not ID
3. No hardcoded joint indices in animation data
4. Load-time validation: ensure animation joint names exist in skeleton

### For Phase 4 (Physics/Ragdoll Addition)

**Add the physics asset:**

```cpp
// physics_asset.h
struct PhysicsAsset {
    /// Per-joint collision bodies
    struct RigidBody {
        std::string bone_name;
        std::vector<Shape> shapes;  // capsule, box, sphere, convex
        float mass = 1.0f;
        EPhysicsType type = Dynamic;  // or Kinematic, Static
    };

    struct Constraint {
        std::string parent_bone;
        std::string child_bone;
        EConstraintType type;  // Hinge, Ball, etc.
        Vec3 limits;           // rotation/translation limits
    };

    std::vector<RigidBody> bodies;
    std::vector<Constraint> constraints;
};

// skeletal_mesh_component.h (updated for Phase 4)
struct SkeletalMeshComponent : PrimitiveComponent {
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Skeleton> skeleton;
    ResourceHandle<PhysicsAsset> physics;  // ← Phase 4 addition

    enum ERagdollMode {
        Disabled,      // skeleton drives physics
        Simulating,    // physics drives bones
        BlendedIK,     // IK chains drive end-effectors
    };

    ERagdollMode ragdoll_mode = Disabled;
};
```

### Asset File Structure (Recommended)

```
res/characters/hero/
├── hero.uzactor              (main character)
├── meshes/
│   ├── hero_body.uzmesh      (geometry + skinning)
│   └── hero_armor.uzmesh     (variant)
├── skeleton/
│   └── humanoid.uzskeleton   (shared skeleton)
├── animations/
│   ├── walk.uzanim
│   ├── run.uzanim
│   └── attack.uzanim
├── physics/
│   ├── humanoid.uzphysics    (standard)
│   └── heavy_armor.uzphysics (variant)
└── controller/
    └── hero_controller.uzanimcontroller  (state machine)
```

This structure makes it clear that:
- One skeleton is shared
- Multiple meshes possible
- Multiple physics assets possible
- Animations are separate

---

## 9. Anti-Patterns to Avoid

### ❌ DON'T: Store Joint Indices in Animation Data

```cpp
// WRONG: hardcoded joint indices
struct AnimationTrack {
    uint32_t joint_id;  // brittle: if skeleton changes, animation breaks
    std::vector<Vec3> translation_keys;
};
```

**Why:** If you reorder joints or retarget skeleton, all animations break silently.

**Do instead:**
```cpp
struct AnimationTrack {
    std::string bone_name;  // "Spine" — maps to whatever joint index in current skeleton
    std::vector<Vec3> translation_keys;
};

// At load time:
// int32_t joint_id = skeleton->GetJointByName("Spine");
```

### ❌ DON'T: Bake Skeleton Into SkeletalMesh Format

```cpp
// WRONG: can't separate
struct SkeletalMesh {
    Skeleton skeleton;  // ← locked to this mesh forever
    Mesh mesh;
};
```

**Why:** Can't retarget to different skeleton, can't share skeleton with other meshes.

### ❌ DON'T: Store Physics in Skeleton Asset

```cpp
// WRONG: conflates animation and physics
struct Skeleton {
    std::vector<Joint> joints;
    std::vector<RigidBody> physics_bodies;  // ← doesn't belong here
};
```

**Why:**
- Skeleton is animation-focused, changes infrequently
- Physics is gameplay-focused, changes frequently
- Designer wants to adjust physics without touching animation
- Can't have multiple physics variants (heavy armor, light armor)

### ❌ DON'T: Assume One Animation Per Mesh

```cpp
// WRONG: assumes 1:1 animation-to-mesh
auto anim = mesh->GetAnimation("walk");
```

**Do instead:**
```cpp
// Correct: animations are independent assets
auto skeleton = mesh->GetSkeleton();
auto anim = AssetManager::Load<AnimSequence>("walk");
assert(anim->skeleton->name == skeleton->name);
```

---

## 10. Implementation Checklist for Phase 3

- [ ] **Skeleton** — JSON format with explicit joint hierarchy (parent_id, not parent index)
- [ ] **SkeletalMesh** — reference external skeleton (not embedded)
- [ ] **AnimSequence** — track by joint name, not joint ID
- [ ] **Load-time validation** — warn if animation bone_name not in skeleton
- [ ] **GetBoneTransform()** — return world transform given bone name
- [ ] **AttachToBone()** — attach component to moving bone
- [ ] **Retargeting prep** — ensure joint names are semantic (not "Bone.001")

For Phase 4 (ragdoll):
- [ ] **PhysicsAsset** — separate file, per-bone collision shapes + constraints
- [ ] **Ragdoll mode** — toggle between skeleton-driven and physics-driven
- [ ] **IK chains** — track end-effectors while physics simulates

---

## 11. Comparison: UzEngine vs. UE5

| Aspect | UE5 (3-Way) | UzEngine Phase 3 (2-Way) | UzEngine Phase 4 (3-Way) |
|--------|------------|-------------------------|-------------------------|
| **Skeleton** | USkeleton (shared) | Skeleton (shared) | Skeleton (shared) |
| **Mesh** | USkeletalMesh (geometry + skinning) | SkeletalMesh (geometry + skinning) | SkeletalMesh (geometry + skinning) |
| **Physics** | UPhysicsAsset (separate) | N/A | PhysicsAsset (separate) |
| **Animation** | UAnimSequence (targets skeleton) | AnimSequence (targets skeleton) | AnimSequence (targets skeleton) |
| **Retargeting** | Yes (bone name mapping) | Possible but manual | Yes (auto via shared skeleton) |
| **Asset Sharing** | 1 skeleton, 10 meshes | 1 skeleton, 10 meshes | 1 skeleton, 10 meshes + varied physics |
| **Ragdoll** | PhysicsAsset + SkeletalMesh | Not supported yet | Full support (Phase 4) |

---

## Key Takeaways

1. **Skeleton != Mesh** — skeleton is animation-focused, mesh is geometry-focused
2. **Skeleton != Physics** — skeleton is animation-focused, physics is gameplay-focused
3. **One skeleton, many animations** — animations target skeleton by joint name, not ID
4. **One skeleton, many meshes** — meshes ref skeleton, all play same animations with different visual results
5. **One skeleton, many physics assets** — gameplay variations (weight, armor) don't affect animation
6. **For UzEngine:** 2-way split is appropriate for Phase 3 (animation-only). Upgrade to 3-way for Phase 4 (ragdoll physics).

