# Animation System

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/animation/`
> **References**: UE5 Animation Blueprint + Control Rig (clean-room), Godot AnimationPlayer

## Purpose

Full skeletal animation pipeline: skeletal mesh (joint hierarchy, bind pose), GPU skinning, animation state machines, blend trees, morph targets, IK solvers, root motion, montages, and additive blending.

## Architecture

```
Animation Assets (.uzanim, .uzskeleton)
    │
    ├── Skeleton (.uzskeleton)
    │   └── Joint hierarchy, bind pose, inverse bind matrices
    │
    ├── Animation Sequences (.uzanim)
    │   └── Per-joint keyframes (position, rotation, scale) + events
    │
    └── Animation Controller (.uzanimcontroller)
        └── State machine: states, transitions, blend trees, layers
            │
            ▼
Animation System (per-entity, per-frame)
    │
    ├── 1. Evaluate state machine → select active animations
    ├── 2. Sample keyframes at current time
    ├── 3. Blend (layered, additive, blend trees)
    ├── 4. Apply IK solvers (FABRIK, CCD)
    ├── 5. Extract root motion (optional)
    ├── 6. Compute final joint matrices (local → world)
    ├── 7. Upload joint palette to GPU
    │
    └── GPU Skinning (compute shader)
        └── Transform vertices by joint weights → output to vertex buffer
```

## Key Components

### SkeletalMeshComponent

```cpp
/// Skinned mesh renderer. Inherits PrimitiveComponent.
/// Drives GPU skinning from an AnimationController's joint output.
class SkeletalMeshComponent : public PrimitiveComponent {
public:
    ResourceHandle<Mesh>      mesh;           // .uzmesh with bone weights
    ResourceHandle<Skeleton>  skeleton;       // .uzskeleton
    ResourceHandle<Material>  material;

    /// The animation controller driving this mesh (null = bind pose).
    AnimationController* anim_controller = nullptr;

    /// Morph target weights (indexed by morph target name hash).
    void SetMorphTarget(StringView name, float weight);
};
```

### AnimationController (State Machine)

```cpp
/// Per-entity animation state machine.
/// Loaded from .uzanimcontroller (JSON) or created in code.
class AnimationController {
public:
    /// Set a parameter used by transitions and blend trees.
    void SetFloat(StringView name, float value);
    void SetBool(StringView name, bool value);
    void SetInt(StringView name, int value);
    void SetTrigger(StringView name);

    /// Get current state name (for debugging/UI).
    StringView GetCurrentStateName(int layer = 0) const;

    /// Play a montage — one-shot animation that overrides the state machine temporarily.
    void PlayMontage(ResourceHandle<AnimSequence> anim, float blend_in = 0.2f);
    void StopMontage(float blend_out = 0.2f);

    /// AnimNotify events — fire callbacks at specific animation frames.
    MulticastDelegate<void(StringView)> OnNotify;
};
```

### Blend Trees

| Node Type | Description |
|-----------|-------------|
| `BlendTree1D` | 1-parameter blend (walk ↔ run by speed) |
| `BlendTree2D` | 2-parameter blend (directional locomotion by angle + speed) |
| `Additive` | Layer an animation on top (breathing + walk) |
| `Override` | Full override on specific bones (upper body aim) |

### IK Solvers

| Solver | Use Case |
|--------|----------|
| **FABRIK** | General-purpose chain IK (arms reaching, tentacles) |
| **CCD** | Fast iterative (tail, chains with many joints) |
| **Two-Bone** | Optimized for arms and legs (elbow/knee constraint) |
| **Look-At** | Head/eye tracking toward a target |

### Root Motion

When enabled, the root bone's translation/rotation is extracted from the animation and applied to the entity's movement component instead of the skeleton. This gives animation-driven movement (realistic footstep distances, turns).

```cpp
/// Root motion extraction mode.
enum class RootMotionMode : uint8_t {
    NoExtraction,     // root bone animates normally (root stays at origin)
    ExtractTranslation, // root XY motion → movement component, Z stays in anim
    ExtractAll,       // root translation + rotation → movement component
};
```

## File Formats

| Format | Extension | Content |
|--------|-----------|---------|
| Skeleton | `.uzskeleton` | JSON: joint hierarchy, bind pose, inverse bind matrices |
| Animation | `.uzanim` | JSON metadata + binary keyframes (position, rotation, scale per joint) |
| Controller | `.uzanimcontroller` | JSON: state machine, transitions, blend trees, parameters |

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| State machine | AnimGraph (visual) | AnimationTree | Animator Controller | **AnimationController (JSON)** |
| Blend trees | BlendSpace (1D/2D) | BlendTree | Blend Tree | **BlendTree1D/2D** |
| IK | Control Rig (FABRIK, CCDIK) | SkeletonIK3D | Animation Rigging | **FABRIK, CCD, Two-Bone** |
| Root motion | Root Motion Montage | Root Motion | Root Motion | **RootMotionMode** |
| Montages | AnimMontage (sections, notifies) | None built-in | Animation Override | **PlayMontage() + AnimNotify** |
| Morph targets | MorphTarget (Blueprints) | BlendShape | BlendShape | **SetMorphTarget()** |
| Skinning | GPU compute | CPU (GPU optional) | GPU | **GPU compute shader** |
| Additive | Full support | Limited | Layers | **Additive + Override layers** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Skeleton, AnimSequence playback, GPU skinning compute shader, basic state machine |
| **Phase 4** | Blend trees, IK solvers, root motion, montages, morph targets, AnimNotify events |
