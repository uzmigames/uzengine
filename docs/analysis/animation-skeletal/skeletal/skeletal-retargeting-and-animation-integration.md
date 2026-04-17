<!-- doc-meta
title: Skeletal Retargeting & Animation System Integration
description: How to share animations across different skeletal meshes, retargeting patterns, and ECS integration
date: 2026-03-28
status: reference
-->

# Skeletal Retargeting & Animation System Integration

## Overview

This document answers: **"Can one animation work with multiple skeletal meshes that share the same skeleton?"**

**Answer: YES — and this is the primary architectural benefit of the 3-way split (skeleton / mesh / physics).**

---

## 1. Animation Retargeting: The Key Use Case

### The Problem (Without Shared Skeleton)

```
If skeleton is embedded in mesh:

    Mesh "Hero"
    ├── Skeleton "Hero_Rig" (50 joints)
    │   └── embedded, unique to this mesh
    └── vertex_weights tied to Hero_Rig

    Mesh "Villain"
    ├── Skeleton "Villain_Rig" (50 joints)
    │   └── embedded, unique to this mesh
    └── vertex_weights tied to Villain_Rig

    Animation "Walk.uzanim"
    └── targets skeleton ??? (which one?)
        → Can't work with both

Result: Need separate animation for each character
    Walk_Hero.uzanim (210 KB)
    Walk_Villain.uzanim (210 KB)
    ... × 20 animations × 10 characters = 42 MB

Total: duplicate animation data
```

### The Solution (With Shared Skeleton)

```
    Skeleton "HumanoidBase" (50 joints)
    ├── Shared by all humanoid characters
    └── Animation target (joint names: Spine, Arm_L, Leg_L, ...)

    Mesh "Hero" → references HumanoidBase
    Mesh "Villain" → references HumanoidBase
    Mesh "NPC" → references HumanoidBase

    Animation "Walk.uzanim"
    └── targets HumanoidBase (joint names)
        → works on ALL meshes using HumanoidBase

Result: ONE animation used by many characters
    Walk.uzanim (210 KB)
    ... × 20 animations × 1 skeleton = ~4 MB

Total: 10× less animation data
```

---

## 2. Animation Retargeting Workflow

### Scenario: Hero and Villain with Different Proportions

**Skeleton Definition (Shared):**

```json
{
  "name": "humanoid_base",
  "joints": [
    { "id": 0, "name": "Hips", "parent_id": -1 },
    { "id": 1, "name": "Spine", "parent_id": 0 },
    { "id": 2, "name": "Chest", "parent_id": 1 },
    { "id": 3, "name": "Arm.L", "parent_id": 2 },
    { "id": 4, "name": "Forearm.L", "parent_id": 3 }
  ]
}
```

**Mesh 1: Hero (tall, thin)**

```json
{
  "name": "hero_body",
  "skeleton_ref": "humanoid_base",
  "vertices": [
    { "position": [0, 0, 0], "bone_ids": [0, 0, 0, 0], "bone_weights": [255, 0, 0, 0] },
    { "position": [0, 50, 0], "bone_ids": [1, 2, 0, 0], "bone_weights": [200, 55, 0, 0] }
  ]
}
```

**Mesh 2: Villain (short, muscular)**

```json
{
  "name": "villain_body",
  "skeleton_ref": "humanoid_base",
  "vertices": [
    { "position": [0, 0, 0], "bone_ids": [0, 0, 0, 0], "bone_weights": [255, 0, 0, 0] },
    { "position": [0, 35, 0], "bone_ids": [1, 2, 0, 0], "bone_weights": [180, 75, 0, 0] }
  ]
}
```

(Note: Villain's spine vertex is at Y=35 instead of Y=50 due to proportions)

**Animation: Walk (targets HumanoidBase)**

```json
{
  "name": "walk",
  "skeleton_ref": "humanoid_base",
  "duration": 1.0,
  "tracks": [
    {
      "bone_name": "Hips",
      "translation": [
        [0.0, 0.0, 0.0],
        [5.0, 0.0, 0.0],
        [10.0, 0.0, 0.0]
      ],
      "times": [0.0, 0.5, 1.0]
    },
    {
      "bone_name": "Spine",
      "rotation": [
        [0, 0, 0, 1],
        [0.1, 0, 0, 0.995],
        [0, 0, 0, 1]
      ],
      "times": [0.0, 0.5, 1.0]
    }
  ]
}
```

### Playback: Same Animation, Different Meshes

```cpp
// Load shared assets
auto skeleton = AssetManager::Load<Skeleton>("humanoid_base");
auto walk_anim = AssetManager::Load<AnimSequence>("walk");

// Create Hero
Entity hero = world.SpawnActor("hero");
auto hero_mesh = hero.GetComponent<SkeletalMeshComponent>();
hero_mesh->mesh = AssetManager::Load<Mesh>("hero_body");
hero_mesh->skeleton = skeleton;
hero_mesh->animation_controller->Play(walk_anim);  // ← same animation

// Create Villain
Entity villain = world.SpawnActor("villain");
auto villain_mesh = villain.GetComponent<SkeletalMeshComponent>();
villain_mesh->mesh = AssetManager::Load<Mesh>("villain_body");
villain_mesh->skeleton = skeleton;
villain_mesh->animation_controller->Play(walk_anim);  // ← same animation

// Result: both walk, but look different
// Hero walks taller steps (mesh proportions)
// Villain walks shorter steps (mesh proportions)
// Animation is identical
```

**Why it works:**

```
Walk animation keyframe at t=0.5:
    Spine: rotate [0.1, 0, 0, 0.995]
    Hips: translate [5.0, 0, 0]

Applied to Hero skeleton (tall):
    Spine at Y=50 + offset → visible lean

Applied to Villain skeleton (short):
    Spine at Y=35 + offset → same rotation, different position
        → appears steeper lean due to shorter torso
```

The **same animation** produces **different-looking results** due to mesh proportions.

---

## 3. Animation System: ECS Integration

### AnimationController Component

The animation state machine is per-entity:

```cpp
// animation_controller.h

class AnimationController {
public:
    /// Load state machine from .uzanimcontroller JSON
    bool Load(const std::string& path);

    // ─── Parameters (for transitions) ────────────────────────────
    void SetFloat(std::string_view name, float value);
    void SetBool(std::string_view name, bool value);
    void SetTrigger(std::string_view name);

    // ─── Playback ────────────────────────────────────────────────
    /// Play animation immediately (overrides state machine)
    void PlayMontage(ResourceHandle<AnimSequence> anim, float blend_in_time = 0.2f);
    void StopMontage(float blend_out_time = 0.2f);

    // ─── Current Pose ────────────────────────────────────────────
    /// Get current animation pose as local joint transforms
    std::vector<Transform> GetLocalPoses(const Skeleton& skeleton) const;

    // ─── Events ─────────────────────────────────────────────────
    /// Called when animation notifies fire (e.g., footstep at frame 15)
    MulticastDelegate<void(std::string_view)> OnNotify;

    // ─── Update (called by SkeletalMeshComponent) ─────────────────
    void Update(float delta_time);

private:
    // ─── State Machine ──────────────────────────────────────────
    struct State {
        std::string name;
        ResourceHandle<AnimSequence> animation;
        std::vector<Transition> transitions;
        float playback_time = 0.0f;
    };

    struct Transition {
        std::string target_state;
        std::function<bool()> condition;  // predicate on parameters
        float blend_time = 0.2f;
    };

    State* current_state_ = nullptr;
    std::map<std::string, State> states_;

    // ─── Parameters ──────────────────────────────────────────────
    std::map<std::string, float> float_params_;
    std::map<std::string, bool> bool_params_;
    std::set<std::string> triggers_;  // consumed each frame

    // ─── Blending ───────────────────────────────────────────────
    float blend_weight_ = 1.0f;
    float blend_velocity_ = 0.0f;
    ResourceHandle<AnimSequence> blend_animation_;
};
```

### SkeletalMeshComponent Integration

```cpp
// From skeletal_mesh_component.cpp

void SkeletalMeshComponent::OnUpdate(float delta_time) {
    if (!enabled || !skeleton || !mesh) return;

    // ───── Step 1: Update Animation Controller ─────────────────
    if (animation_controller) {
        animation_controller->Update(delta_time);
    }

    // ───── Step 2: Get Current Pose ───────────────────────────
    std::vector<Transform> local_poses;
    if (animation_controller) {
        // Get pose from state machine
        local_poses = animation_controller->GetLocalPoses(*skeleton);
    } else {
        // Use bind pose (no animation)
        local_poses = skeleton->GetBindPose();
    }

    // ───── Step 3: Compute World Matrices ──────────────────────
    auto world_matrices = skeleton->ComputeWorldMatrices(local_poses);

    // ───── Step 4: Upload to GPU ───────────────────────────────
    joint_palette_buffer_.SetData(world_matrices.data(),
                                  world_matrices.size() * sizeof(Mat4));

    // ───── Step 5: Update Attachments ──────────────────────────
    UpdateBoneAttachments();
}
```

### ECS Architecture

```cpp
// In game.cpp (ECS setup)

// Animation system runs per-frame
void UpdateAnimationSystem(World& world, float delta_time) {
    for (auto entity : world.View<SkeletalMeshComponent>()) {
        auto skeletal_mesh = entity.GetComponent<SkeletalMeshComponent>();
        skeletal_mesh->OnUpdate(delta_time);  // updates poses, GPU buffers
    }
}

// Rendering system consumes the poses
void RenderSkeletalMeshes(World& world, RenderContext& ctx) {
    for (auto entity : world.View<SkeletalMeshComponent, TransformComponent>()) {
        auto skeletal_mesh = entity.GetComponent<SkeletalMeshComponent>();
        auto transform = entity.GetComponent<TransformComponent>();

        // Mesh is already skinned (UpdateSkinning called in OnUpdate)
        // Just render it
        ctx.DrawSkeletalMesh(
            skeletal_mesh->mesh,
            skeletal_mesh->material,
            transform->world_transform,
            skeletal_mesh->GetJointMatrices());
    }
}

int main() {
    World world;

    // Game loop
    while (running) {
        float delta_time = 0.016f;  // 60 FPS

        // Update animations
        UpdateAnimationSystem(world, delta_time);

        // Update other systems
        UpdatePhysicsSystem(world, delta_time);
        UpdateGameplaySystem(world, delta_time);

        // Render
        RenderSkeletalMeshes(world, render_ctx);
    }
}
```

---

## 4. Practical Example: Character Library

### Asset Structure

```
res/characters/
├── shared_skeleton/
│   └── humanoid.uzskeleton  ← SHARED (1 file, 100 KB)
│
├── hero/
│   ├── hero_body.uzmesh     (geometry + skinning for humanoid)
│   ├── hero_armor.uzmesh    (variant: clothed)
│   └── controller.uzanimcontroller
│
├── villain/
│   ├── villain_body.uzmesh
│   ├── villain_armor.uzmesh
│   └── controller.uzanimcontroller
│
├── npc/
│   ├── npc_body.uzmesh
│   └── controller.uzanimcontroller
│
└── animations/  ← SHARED (targets humanoid skeleton)
    ├── idle.uzanim         (once, used by hero + villain + npc)
    ├── walk.uzanim
    ├── run.uzanim
    ├── jump.uzanim
    ├── attack.uzanim
    └── ... (20 more animations)
```

### Memory Breakdown

**With 3-way split (current UzEngine):**

```
Skeleton:              100 KB (shared by 3 meshes)
Animations:            4 MB (20 anims × 200 KB each, shared)
Mesh "hero":           5 MB (geometry + skinning)
Mesh "villain":        4 MB (geometry + skinning)
Mesh "npc":            3 MB (geometry + skinning)
                       ─────────────────
Total:                 16.1 MB

Per character instance (in-memory at runtime):
    AnimationController state:  ~10 KB
    Joint matrices (128 × 64B): ~8 KB
                                ──────
    ~18 KB per instance
    × 100 characters = 1.8 MB
```

**If skeleton were embedded in each mesh:**

```
Mesh "hero":           5 MB + 100 KB (skeleton) = 5.1 MB
Mesh "villain":        4 MB + 100 KB (skeleton) = 4.1 MB
Mesh "npc":            3 MB + 100 KB (skeleton) = 3.1 MB

Animations:
    Walk_hero.uzanim:  200 KB
    Walk_villain.uzanim: 200 KB
    Walk_npc.uzanim:   200 KB
    (× 20 animations)
                       ─────────────
    20 × 3 × 200 KB = 12 MB

Total:                 25.2 MB (56% more)
```

---

## 5. Retargeting Without Shared Skeleton (Workaround)

If you need to use different skeletons but want to share animations, use **bone mapping**:

```cpp
// BoneMapping: maps one skeleton's joints to another's
struct BoneMapping {
    std::map<std::string, std::string> mapping;
    // "Hero_Spine" → "Villain_Spine"
    // "Hero_Arm.L" → "Monster_UpperLimb.L"
};

std::vector<Transform> RetargetAnimation(
    const AnimSequence& anim,
    const Skeleton& source_skeleton,
    const Skeleton& target_skeleton,
    const BoneMapping& mapping) {

    std::vector<Transform> result = target_skeleton.GetBindPose();

    for (const auto& [src_name, track] : anim.tracks_) {
        // Find target bone name
        auto it = mapping.mapping.find(src_name);
        if (it == mapping.mapping.end()) continue;

        const std::string& target_name = it->second;
        auto* target_joint = target_skeleton.GetJointByName(target_name);
        if (!target_joint) continue;

        // Apply animation to target bone
        result[target_joint->id] = track.Sample(anim_time);
    }

    return result;
}
```

**However:** This requires manual bone mapping for each skeleton pair. The 3-way split avoids this entirely.

---

## 6. Advanced: Blend Trees & Procedural Animation

### Blend Tree 1D (Walk ↔ Run by Speed)

```json
{
  "name": "locomotion",
  "type": "blend_tree_1d",
  "parameter": "movement_speed",
  "poses": [
    { "speed": 0.0, "animation": "idle.uzanim", "position": 0.0 },
    { "speed": 3.0, "animation": "walk.uzanim", "position": 0.3 },
    { "speed": 7.0, "animation": "run.uzanim", "position": 1.0 }
  ]
}
```

### Additive Animation (Breathing Over Walk)

```cpp
// C++ example

AnimationController controller;
controller.Play("walk");  // Base animation

// Add breathing as additive layer
auto breathing = AssetManager::Load<AnimSequence>("breathing_idle");
controller.PlayAdditive("breathing", breathing, 0.5f);

// Result: character walks + breathes simultaneously
```

---

## 7. Anti-Patterns: What NOT to Do

### ❌ DON'T: Store Bone Index in Animation

```cpp
// WRONG: breaks retargeting
struct AnimationTrack {
    uint32_t bone_id;  // 0, 1, 2, ...
};

// If skeleton changes → animation breaks silently
```

### ❌ DON'T: Duplicate Animations Per Character

```cpp
// WRONG: massive asset bloat
animations/
├── hero_walk.uzanim
├── villain_walk.uzanim
├── npc_walk.uzanim
├── hero_run.uzanim
├── villain_run.uzanim
├── npc_run.uzanim
```

### ❌ DON'T: Lock Mesh to Skeleton

```cpp
// WRONG: can't share skeleton
struct SkeletalMesh {
    Skeleton skeleton;  // embedded
};

// Can't load a different skeleton on this mesh
```

### ❌ DON'T: Assume Bone Naming Convention

```cpp
// WRONG: not universal
if (bone_name.StartsWith("Bone")) {
    // Blender uses "Bone.001", Maya uses "joint1", etc.
}

// DO: Make bone names semantic
// "Spine", "Arm.L", "Leg.R" (same across all rigs)
```

---

## 8. Content Authoring Guidelines

### For Game Designers

**Before exporting a character:**

1. **Check skeleton naming** — is it "Armature|Spine" or "Spine" or "spine_0"?
   - Keep names consistent across all characters
   - Use semantic names, not "Bone.001"

2. **Verify bind pose** — character in T-pose or A-pose?
   - Blend trees assume consistent bind pose
   - All meshes using same skeleton should have same bind pose

3. **Test with shared skeleton** — does character load?
   - If mesh says it wants skeleton "hero_rig" but you assign "humanoid_rig", does it still work?
   - (It should, via retargeting, but may need bone mapping)

### For Riggers/Animators

**When creating skeleton:**

1. Use **consistent bone naming** across all characters
   ```
   GOOD:   Armature → Hips → Spine → Chest → Arm.L → Forearm.L → Hand.L
   BAD:    armature_001 → bone_1 → bone_2 → bone_3 → ...
   ```

2. **Document bind pose** — what skeleton looks like at rest
   ```
   Bind pose: T-pose
   Spine at Y=100cm
   Arm length: 30cm
   ```

3. **Test animation on multiple meshes** — does it work on tall + short characters?

---

## 9. Performance Considerations

### Per-Frame Cost (Single Character)

```
Animation sampling:        0.1 ms (linear interp, 100 joints)
Joint matrix computation:  0.5 ms (parent walk, 100 joints)
GPU skinning (vertex):     1.5 ms (50K verts, 128 bones)
                           ─────────
Total per character:       2.1 ms

× 50 characters: 105 ms (would be 16.7 ms CPU-bound at 60 FPS)
→ Need to parallelize or use compute shader skinning
```

### Optimization Strategies

1. **Compute shader skinning** (instead of vertex shader)
   - Dispatch one compute job per mesh
   - Update output mesh in-place
   - Better VRAM bandwidth

2. **Animation LOD**
   - Far characters: sample every 2nd frame
   - Very far: use blend tree (idle/stand only)

3. **Bone LOD**
   - Some characters: reduce to 64 bones (skip fingers, face)
   - Doesn't affect animation, just skinning resolution

---

## 10. Summary: Retargeting Architecture

| Component | Role | Shared? |
|-----------|------|---------|
| Skeleton | Joint hierarchy, animation target | YES (N meshes) |
| SkeletalMesh | Geometry + skinning data | NO (unique per mesh) |
| AnimSequence | Keyframes for joint names | YES (N skeletons) |
| AnimationController | State machine, per-entity | NO (unique per character) |

**Result:**
- 1 skeleton → N meshes (Hero, Villain, NPC)
- N meshes + 1 skeleton → 1 animation library
- Different meshes play same animation, look different
- Can add new mesh without new animations

