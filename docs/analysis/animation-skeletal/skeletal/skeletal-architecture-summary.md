<!-- doc-meta
title: UE5 Skeletal Asset Architecture — Complete Answers
description: Direct answers to all 6 original questions about skeleton/mesh/physics separation
date: 2026-03-28
status: reference
-->

# UE5 Skeletal Asset Architecture: Direct Answers

This document directly answers your 6 core questions.

---

## Question 1: USkeleton — What Does It Store? Why Separate?

### What It Stores

```cpp
struct USkeleton {
    // ─── The ONLY Essential Data ────────────────────────────────
    std::vector<Joint> bones;         // parent_id, name, bind pose transform
    std::map<std::string, uint32_t> bone_name_to_id;  // fast lookup

    // ─── Optional Metadata ──────────────────────────────────────
    std::vector<FSocket> sockets;     // attachment points (swords, helmets)
    std::map<std::string, FIKChain> ik_chains;  // procedural animation chains
    std::map<std::string, FBoneReduction> retarget_map;  // anatomy differences
};
```

### Why Separate From Mesh?

**Three Architectural Reasons:**

1. **Animation Retargeting** (killer feature)
   ```
   One skeleton "Humanoid" (50 joints) + 10 meshes (Hero, Villain, NPC, ...)
   All 10 meshes play the same 20 animations
   Same animation looks different on each mesh (proportions differ)

   Without separation:
       Would need 10 × 20 = 200 animation files
   With separation:
       Need only 20 animation files + 10 meshes

   Memory savings: 10× for animation data
   ```

2. **Independent Iteration**
   ```
   Animator tweaks skeleton hierarchy → all meshes/anims affected together
   Mesh artist updates Hero body → only Hero mesh updated
   Physics designer adjusts ragdoll → only physics asset updated

   If skeleton embedded in mesh:
       Changing skeleton requires re-importing all meshes
       Breaking all animations
   ```

3. **Shared Skeleton Library**
   ```
   One "HumanoidBase" skeleton:
       - Used by Hero, Villain, NPC, Boss
       - Targets all humanoid animations
       - Version control: 1 file, not 10
       - Update skeleton once → all characters benefit
   ```

### Memory Benefit (Secondary)

If 10 characters use one skeleton:
```
Embedded per-mesh:  10 × 8 KB skeleton = 80 KB
Shared skeleton:    1 × 8 KB           = 8 KB
Savings:            70% per character instance
```

For 100 character instances in-memory:
```
Embedded:  100 × 8 KB = 800 KB
Shared:    1 × 8 KB   = 8 KB
Savings:   99.2% ← significant for open worlds
```

**Verdict:** Separation is NOT primarily for memory. It's for **animation sharing + retargeting**. Memory savings are a bonus.

---

## Question 2: USkeletalMesh — What Data Beyond Vertices?

### Complete Data Structure

```cpp
struct USkeletalMesh {
    // ─── Geometry ───────────────────────────────────────────────
    Mesh mesh;                     // vertices, indices, UVs, normals, tangents

    // ─── Skeleton Reference ─────────────────────────────────────
    USkeleton* RefSkeleton;        // the skeleton this mesh was rigged to
    USkeleton* Skeleton;           // current skeleton (for retargeting)

    // ─── Skinning Data ───────────────────────────────────────────
    std::vector<SkinnedVertex> skinned_vertices;
    struct SkinnedVertex {
        uint8_t bone_ids[4];       // which 4 joints influence this vertex
        uint8_t bone_weights[4];   // normalized weights (0-255 on disk, 0-1 on GPU)
    };

    // ─── Bind Pose Inverse Matrices (Precomputed) ────────────────
    std::vector<Mat4> RefBasesInvMatrix;
    // Transform from world space back to bind pose local space
    // Computed once per mesh, per-skeleton pair
    // Used in skinning: V_final = Σ(weight[i] × InvBind[i] × JointWorld[i] × V_local)

    // ─── LOD Levels (Multiple) ──────────────────────────────────
    struct LOD {
        uint32_t vert_count;       // LOD0: 45K, LOD1: 15K, LOD2: 5K
        uint32_t index_count;
        std::vector<VertexSkinData> skin_data;
        uint32_t active_bones;     // LOD0: 128, LOD1: 64, LOD2: 32
    };
    std::vector<LOD> lod_levels;

    // ─── Morph Targets (Blend Shapes) ───────────────────────────
    struct MorphTarget {
        std::string name;          // "smile", "blink", "frown"
        std::vector<Vec3> delta_vertices;  // offset per vertex
    };
    std::vector<MorphTarget> morph_targets;

    // ─── Bone Sockets (Attachment Points) ────────────────────────
    std::vector<FSocket> sockets;  // for attaching items to bones
};
```

### RefSkeleton vs. Skeleton: Critical Distinction

```cpp
RefSkeleton:
    ✓ The skeleton THIS MESH WAS CREATED WITH (at import time)
    ✓ Has inverse bind matrices specific to this mesh + skeleton
    ✓ READ-ONLY (used for validation)
    ✓ Example: "Hero_Armature" as it appeared in Maya export

Skeleton:
    ✓ The skeleton CURRENTLY ASSIGNED (can be reassigned at runtime)
    ✓ Enables retargeting (assign different skeleton)
    ✓ Must have same joint names as RefSkeleton (for retargeting to work)
    ✓ Example: change from "Hero_Armature" to "Monster_Humanoid"

Relationship:
    At load:  Skeleton = RefSkeleton (always true)
    At runtime: Skeleton might be different (for retargeting)

Inverse Bind Matrix Issue:
    If you change skeleton, inverse bind matrices don't change
    → Skinning still uses RefSkeleton's inverse bind matrices
    → Works IF joint names match
    → If proportions differ too much → visual artifacts (needs bone mapping)
```

### Why Not Bake Skeleton Into Mesh?

The **inverse bind matrix is mesh-specific**:

```
For vertex V at position [x, y, z]:

    V_skinned = Σ(weight[i] × JointMatrix[i] × InverseBindMatrix[i] × V_local)

InverseBindMatrix depends on:
    - Bind pose at rigging time
    - Mesh vertex positions
    - Skeleton proportions

Example:
    Mesh A: vertex at [0, 100, 0]
    Mesh B: vertex at [0, 75, 0]   (shorter torso)

    If they use the SAME skeleton, same animation:
    Mesh A: vertex moves by +5cm
    Mesh B: vertex moves by +5cm
    → different visual effect (shorter torso makes it look like more rotation)

If you change skeleton's bind pose:
    → must recompute InverseBindMatrix for each vertex
    → must re-import mesh

Therefore:
    Skeleton (immutable joint hierarchy) ← shared
    Mesh (immutable vertex geometry) ← unique
    InverseBindMatrix (per mesh × skeleton pair) ← precomputed and cached
```

### LOD Levels

Each LOD has independent vertex/index data:

```
LOD0 (close):
    45,000 vertices
    128 active bones
    Full detail (finger positions, facial bones)

LOD1 (medium):
    15,000 vertices
    64 active bones
    (fewer finger bones)

LOD2 (far):
    5,000 vertices
    32 active bones
    (only main skeleton joints)
```

**Bone reduction is manual** (done at import), not automatic.

---

## Question 3: UPhysicsAsset — Why Separate From Skeleton?

### What It Stores

```cpp
struct UPhysicsAsset {
    // ─── Rigid Bodies ───────────────────────────────────────────
    struct FBodySetupData {
        FName BoneName;            // which joint this body is attached to
        std::vector<Shape> shapes; // capsule, box, sphere, convex
        float mass;
        EPhysicsType type;         // Dynamic, Kinematic, Static
    };
    std::vector<FBodySetupData> bodies;

    // ─── Constraints ────────────────────────────────────────────
    struct FConstraintData {
        FName parent_bone;
        FName child_bone;
        EConstraintType type;      // Hinge, Ball-and-Socket, etc.
        Vec3 limits;               // rotation/linear limits
    };
    std::vector<FConstraintData> constraints;
};
```

### Three Reasons for Separation

1. **Different Use Cases**
   ```
   Skeleton:
       Used by: Animation system, VFX, Lua scripting
       Changes: Rarely (new character skeleton)
       Iteration: Days (character design phase)

   PhysicsAsset:
       Used by: Physics engine, gameplay (ragdoll, hit detection)
       Changes: Frequently (balance tweaking)
       Iteration: Hours (playtesting, adjusting weight/constraints)

   Result: Separate assets allow independent iteration
   ```

2. **Many-to-Many Mapping**
   ```
   One Skeleton ↔ Many PhysicsAssets

   Example: Character variants
       Skeleton "Humanoid" ← shared by all

       Mesh "Hero_Light" → Physics "Light" (60 kg, flexible joints)
       Mesh "Hero_Heavy" → Physics "Heavy" (120 kg, stiff joints)
       Mesh "Hero_Armored" → Physics "Armored" (100 kg, reinforced)

   Without separation:
       Would need 3 different skeletons
       3 × animation library = 3× larger
   ```

3. **Physics Iteration Without Re-Import**
   ```
   Designer workflow:
       1. Import character mesh (auto-gen physics capsule)
       2. Playtest
       3. "Ragdoll rotation is too fast" → adjust constraint limits
       4. Save PhysicsAsset, re-playtest

       NO mesh re-import needed
       NO skeleton re-import needed

   If physics were in skeleton:
       1-4: would require full re-import chain
           (mesh → skeleton → animation → all downstream)
   ```

---

## Question 4: The Practical Benefit — 3-Way Split

### The Core Win: One Skeleton, Many Assets

```
┌──────────────────────────────────────────────────────┐
│ Skeleton: "HumanoidBase" (1 file, 100 KB)           │
│   - Animation target (joint names)                   │
│   - Immutable (changed rarely)                       │
│   - Shared across all humanoid characters            │
└──────────────────────────────────────────────────────┘
         ↓           ↓           ↓
    ┌────────┐  ┌────────┐  ┌────────┐
    │  Hero  │  │Villain │  │  NPC   │
    └────────┘  └────────┘  └────────┘
         ↓           ↓           ↓
    ┌─────────────────────────────────┐
    │ Animation Library (20 anims)    │
    │ - Walk, Run, Jump, Attack, ...  │
    │ (1× shared by all characters)   │
    └─────────────────────────────────┘
```

### Memory Efficiency

```
Without 3-way split (skeleton embedded in each mesh):

    Hero mesh + skeleton:         5.1 MB
    Villain mesh + skeleton:      4.1 MB
    NPC mesh + skeleton:          3.1 MB
    Hero animations (20):         4.0 MB
    Villain animations (20):      4.0 MB
    NPC animations (20):          4.0 MB
                                 ────────
    Total:                        24.3 MB

With 3-way split:

    Skeleton:                     0.1 MB (shared)
    Hero mesh:                    5.0 MB
    Villain mesh:                 4.0 MB
    NPC mesh:                     3.0 MB
    Shared animations (20):       4.0 MB (used by all 3)
                                 ────────
    Total:                        16.1 MB

Savings:                         34% (but more importantly...)
```

### The Real Benefit: Development Efficiency

```
Scenario: Animator creates new "Crouch_Walk" animation

Without 3-way split:
    - Create Walk_Hero_Crouch.anim
    - Create Walk_Villain_Crouch.anim
    - Create Walk_NPC_Crouch.anim
    (3 files, same animation data, takes 3× long to export/validate)

With 3-way split:
    - Create Crouch_Walk.anim (targets HumanoidBase skeleton)
    - All 3 characters use it immediately
    (1 file, 1 export, instant update to all characters)

Time to add animation:
    Without: 30 minutes
    With: 10 minutes
    (3× faster iteration)
```

---

## Question 5: Animation Assets — Skeleton Reference

### How UAnimSequence References Skeleton

```cpp
class UAnimSequence {
public:
    USkeleton* Skeleton;  // ← explicit reference

    // Keyframe data: indexed by JOINT NAME, not ID
    struct FBoneAnimationTrack {
        FName BoneName;           // "Spine", "Arm_L", "Leg_R"
                                  // NOT "Bone.001" or "joint_0"

        TArray<FVector> TranslationKeys;  // position
        TArray<FQuat> RotationKeys;       // rotation
        TArray<FVector> ScaleKeys;        // scale
        TArray<float> Times;              // when each key occurs
    };
    TArray<FBoneAnimationTrack> Tracks;
};
```

### One Animation, Many Meshes

```cpp
// Load
USkeleton* skeleton = LoadAsset<USkeleton>("HumanoidBase");
UAnimSequence* walk = LoadAsset<UAnimSequence>("Walk");
// walk->Skeleton == skeleton ✓ (same reference)

// Create 3 characters with same skeleton + animation
for (FCharacterType type : {Hero, Villain, NPC}) {
    ACharacter* character = SpawnCharacter(type);

    // All 3 use same skeleton
    character->GetSkeletalMesh()->SetSkeleton(skeleton);

    // All 3 play same animation
    character->PlayAnimation(walk);
}

// Result: Walk plays on all 3, looks different due to mesh proportions
```

### Why This Works

```
Animation "Walk":
    Tracks:
        Spine: rotate [0.1, 0, 0, 0.995]
        Hips: translate [5, 0, 0]
        Leg.L: rotate [0.2, 0, 0, 0.98]

Skeleton "HumanoidBase":
    Joints:
        Spine (ID=1)
        Hips (ID=0)
        Leg.L (ID=5)

At load time:
    Animation.GetTrack("Spine") → returns track
    Skeleton.GetJointByName("Spine") → returns ID=1
    Apply track to joint 1

At playback:
    1. Sample track at time T
    2. Apply to joint by name lookup
    3. Different mesh proportions produce different visuals
```

**This is why joint names must be consistent across all meshes sharing a skeleton.**

---

## Question 6: For UzEngine — 2-Way vs. 3-Way Split?

### Current UzEngine: 2-Way Split (Appropriate for Phase 3)

```cpp
// Phase 3: Animation-only

struct Skeleton {
    std::vector<Joint> joints;
    // joint hierarchy, bind pose, inverse bind matrices
};

struct SkeletalMesh {
    ResourceHandle<Mesh> mesh;           // geometry
    ResourceHandle<Skeleton> skeleton;   // reference
    std::vector<VertexSkinData> skin;    // weights
};

// No physics asset yet (Phase 4)
```

**Advantages:**
- Simpler pipeline (fewer asset types)
- Sufficient for animation-only features
- Can add physics later without breaking changes

**Limitations:**
- Physics would live in a messy place (where?)
- Can't have variants (light armor vs. heavy armor with different physics)
- Retargeting is "possible but awkward" (manual code)

### Recommended: Upgrade to 3-Way for Phase 4 (Ragdoll)

**Proposed upgrade path (NO breaking changes):**

```cpp
// Phase 3→4 transition: Add physics asset

struct Skeleton {
    std::vector<Joint> joints;
    // no changes
};

struct SkeletalMesh {
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Skeleton> skeleton;
    std::vector<VertexSkinData> skin;
    // all Phase 3 code unchanged
};

// NEW (Phase 4):
struct PhysicsAsset {
    std::vector<RigidBody> bodies;      // per-joint collision shapes
    std::vector<Constraint> constraints; // joint constraints

    // Separate file: character.uzphysics
    // Optional, can have multiple per skeleton
};

// Skeleton + Mesh interaction unchanged
// Just add optional physics reference in scene
```

### Asset File Structure (Recommended)

```
res/characters/hero/
├── skeleton/
│   └── humanoid.uzskeleton  ← shared (Phase 3)
├── meshes/
│   ├── hero_body.uzmesh     ← unique
│   └── hero_armor.uzmesh    ← variant
├── animations/              ← shared (Phase 3)
│   ├── walk.uzanim
│   ├── run.uzanim
│   └── ...
├── controller/              ← per-character
│   └── hero_controller.uzanimcontroller
└── physics/                 ← separate (Phase 4)
    ├── humanoid_standard.uzphysics
    └── humanoid_armored.uzphysics
```

### Why This Matters for UzEngine

1. **Animation retargeting** — necessary for multiplayer/modding
   ```cpp
   // Player 1 loads "Hero"
   // Player 2 loads "Villain"
   // Both play "Walk.uzanim" (targets HumanoidBase)
   // Same animation library, different characters
   ```

2. **Physics variants** — different "weights" without new skeleton
   ```cpp
   Character "Knight":
       Mesh: knight_body.uzmesh
       Skeleton: humanoid.uzskeleton
       Physics: humanoid_heavy.uzphysics (armor)

   Character "Ranger":
       Mesh: ranger_body.uzmesh
       Skeleton: humanoid.uzskeleton
       Physics: humanoid_light.uzphysics (light)

   Same skeleton, different physics, same animations
   ```

3. **Development velocity** — independent iteration
   ```
   Animator: adds new animation (targets skeleton)
   Mesh artist: updates character body (mesh only)
   Physics: tweaks ragdoll (physics asset only)

   All work in parallel, no conflicts
   ```

**Recommendation:** Design Phase 3 with Phase 4 in mind. Separate skeleton from mesh now. Add physics asset in Phase 4 (no breaking changes).

---

## Summary Table

| Question | Answer |
|----------|--------|
| **What does USkeleton store?** | Joint hierarchy, names, bind pose. Immutable, reusable. |
| **Why separate from mesh?** | Animation retargeting. One skeleton → N meshes → N animations. |
| **What does USkeletalMesh store?** | Geometry + skinning weights. References external skeleton. |
| **RefSkeleton vs. Skeleton?** | Ref = at-import, Skeleton = current (for retargeting). |
| **Why separate physics asset?** | Different iteration cycles. Mesh changes ≠ physics changes. |
| **Practical benefit of 3-way?** | 10× animation memory savings + independent iteration. |
| **How do animations target skeleton?** | By joint name, not ID. Enables retargeting. |
| **Can one animation work on multiple meshes?** | YES. If they share skeleton and have matching bone names. |
| **For UzEngine: 2-way or 3-way?** | Start 2-way (Phase 3). Upgrade to 3-way for Phase 4 (ragdoll). |

---

## Key Takeaways

1. **Skeleton is the animation contract** — not a mesh component
2. **Names, not IDs** — enables retargeting and decoupling
3. **One skeleton, many animations** — animations are shared assets
4. **One skeleton, many meshes** — retargeting allows 10× animation savings
5. **Separate physics for gameplay flexibility** — ragdoll, armor variants, etc.
6. **For UzEngine:** The 2-way split is correct for Phase 3. Upgrade cleanly to 3-way in Phase 4.

---

## Files Generated

See companion documents for implementation details:

- `ue5-skeletal-asset-architecture.md` — Complete architectural analysis
- `uzengine-skeletal-implementation-guide.md` — C++23 implementation patterns
- `skeletal-retargeting-and-animation-integration.md` — Retargeting workflows + ECS integration

