# Control Rig Editor — IK & Constraint Visual Editor

> **Status**: Planned (Phase 3–4)
> **Source**: `extension/editor/controlrig-editor.ts`, `engine/anim/control-rig.h`
> **References**: Animation System (§32), Skeleton Editor (§31), UE5 Control Rig, Godot SkeletonModifier3D, Maya RIGGING

## Purpose

The Control Rig Editor allows riggers to visually build inverse kinematics (IK) chains and bone constraints. Rather than writing code or manual bone translation, animators use a node-graph interface to define constraints (FABRIK IK, two-bone IK, pole targets, look-at, aim) and control shapes that animators can grab to pose the skeleton interactively.

This is a more advanced system for professional rigging than the Animation Controller (§11), used during animation production rather than gameplay.

## Architecture

```
Control Rig Editor (TypeScript)
    ├─ Constraint Graph Canvas
    │  ├─ Constraint nodes (FABRIK, TwoBoneIK, LookAt, etc.)
    │  ├─ Control shapes (visible manipulators for animators)
    │  └─ Visual feedback (bone positions, target points, pole vectors)
    │
    ├─ Skeleton Viewport
    │  ├─ Skeletal mesh visualization
    │  ├─ Real-time constraint solving (IK evaluation)
    │  ├─ Interactive posing (drag controls to pose character)
    │  └─ Bone selection (click bone in viewport)
    │
    ├─ Constraint Inspector
    │  ├─ Target, pole vector, weight settings
    │  ├─ Iteration count (for iterative solvers)
    │  └─ Limits (angle clamps, chain length)
    │
    ├─ Control Shape Editor
    │  ├─ Shape type (circle, square, cross, cube, sphere)
    │  ├─ Color, size, offset from bone
    │  └─ Visibility toggle (show/hide controls)
    │
    └─ Retargeting Panel
       ├─ Source skeleton selector
       ├─ Bone mapping (automatic + manual override)
       └─ Test retargeting on target skeleton
```

## Constraint Types

### FABRIK IK (Forward And Backward Reaching IK)

Multi-bone IK chain solver:

```typescript
interface FABRIKConstraint {
    type: "FABRIK";
    rootBone: string;           // First bone in chain
    endBone: string;            // Last bone in chain (the "hand")
    targetBone: string;         // Where to position end bone
    pole: {
        enable: boolean;
        bone?: string;          // Optional pole target for elbow direction
        vector?: [number, number, number];  // Or fixed pole vector
    };
    iterations: number;         // 10–20 typical (more = more accurate)
    weight: number;             // [0, 1] blend with original pose
}

// Example: arm IK
const armIK: FABRIKConstraint = {
    type: "FABRIK",
    rootBone: "Shoulder",
    endBone: "Hand",
    targetBone: "HandTarget",
    pole: {
        enable: true,
        bone: "ElbowPole"  // Drag this control to bend elbow
    },
    iterations: 15,
    weight: 1.0
};
```

### Two-Bone IK

Specialized IK for two bones (arm, leg):

```typescript
interface TwoBoneIKConstraint {
    type: "TwoBoneIK";
    rootBone: string;       // Shoulder or hip
    midBone: string;        // Elbow or knee
    endBone: string;        // Hand or foot

    // Target position for end bone
    target: {
        bone?: string;      // Control bone to follow
        position?: [number, number, number];  // Or fixed position
    };

    // Pole vector (controls bend direction)
    pole: {
        enable: boolean;
        bone?: string;
        position?: [number, number, number];
    };

    // Limits
    limits?: {
        midAngleMin: number;
        midAngleMax: number;  // Prevent hyperextension
    };

    weight: number;
}
```

### Look-At Constraint

Point a bone (head, eyes) toward a target:

```typescript
interface LookAtConstraint {
    type: "LookAt";
    bone: string;           // Head, eye socket, etc.
    targetBone?: string;    // Control to look at
    targetPosition?: [number, number, number];

    // Which local axis points forward
    forwardAxis: "X+" | "X-" | "Y+" | "Y-" | "Z+" | "Z-";

    // Optional up vector
    upAxis?: "X" | "Y" | "Z";

    // Angle limits (prevent 180° flips)
    pitchLimit?: [number, number];  // Up/down
    yawLimit?: [number, number];    // Left/right

    weight: number;
}
```

### Aim Constraint

Point a bone with a secondary up vector (camera aim):

```typescript
interface AimConstraint {
    type: "Aim";
    bone: string;
    target: {
        bone?: string;
        position?: [number, number, number];
    };
    upVector?: [number, number, number];
    forwardAxis: "X+" | "X-" | "Y+" | "Y-" | "Z+" | "Z-";
    weight: number;
}
```

### Copy Transform

Copy position/rotation from another bone:

```typescript
interface CopyTransformConstraint {
    type: "CopyTransform";
    bone: string;           // Bone to constrain
    target: string;         // Bone to copy from

    // Which components to copy
    copyPosition: boolean;
    copyRotation: boolean;
    copyScale: boolean;

    // Offset from target
    positionOffset?: [number, number, number];
    rotationOffset?: [number, number, number];  // Euler angles

    weight: number;
}
```

### Spring Constraint

Soft constraint with spring physics (for cloth, secondary motion):

```typescript
interface SpringConstraint {
    type: "Spring";
    bone: string;
    target: {
        bone?: string;
        position?: [number, number, number];
    };

    // Spring parameters
    stiffness: number;      // 0–1 (0=loose, 1=stiff)
    damping: number;        // 0–1 (reduces oscillation)
    maxVelocity: number;    // Max movement per frame

    weight: number;
}
```

## Control Shapes

Control shapes are visible manipulators that animators drag:

```typescript
interface ControlShape {
    name: string;
    bone: string;           // Attached to this bone
    shape: "Circle" | "Square" | "Cross" | "Cube" | "Sphere" | "Pyramid";
    size: number;

    // Visual properties
    color: [number, number, number, number];  // RGBA
    opacity: number;

    // Offset from bone
    position: [number, number, number];
    rotation: [number, number, number];  // Euler angles

    // What to control
    controlsParameter?: string;  // Updates Lua parameter
    constraint?: string;         // Updates constraint target

    // Hide in gameplay
    debugOnly: boolean;
}

// Example: hand control shape
const handControl: ControlShape = {
    name: "Hand_Control",
    bone: "HandTarget",
    shape: "Sphere",
    size: 5.0,
    color: [1, 0, 0, 1],
    opacity: 0.8,
    position: [0, 0, 0],
    constraint: "ArmIK",  // Drag this to move arm IK target
    debugOnly: true
};
```

## Constraint Graph

Constraints can be organized as a directed acyclic graph (DAG), similar to the Material Editor but for bone constraints:

```typescript
class ConstraintGraph {
    private constraints: Map<string, Constraint> = new Map();
    private evaluationOrder: string[] = [];

    /// Add constraint node
    addConstraint(id: string, constraint: Constraint) {
        this.constraints.set(id, constraint);
        this.rebuildEvaluationOrder();
    }

    /// Evaluate all constraints in dependency order
    evaluate(skeleton: Skeleton) {
        for (const constraintId of this.evaluationOrder) {
            const constraint = this.constraints.get(constraintId)!;
            constraint.evaluate(skeleton);  // Modifies bone transforms
        }
    }

    /// Topological sort to ensure dependencies are evaluated in order
    private rebuildEvaluationOrder() {
        // E.g., if ArmIK depends on HandTarget, evaluate HandTarget first
        // if HandTarget depends on character position, evaluate position first
        // ...
    }
}
```

## Retargeting

Animators can apply a rig built on one skeleton to a different skeleton (different proportions, bone names):

```typescript
interface SkeletonRetargeting {
    sourceSkeletonPath: string;
    targetSkeletonPath: string;

    // Bone mapping: source → target
    boneMapping: Map<string, string>;

    // Scaling (if target is larger/smaller)
    scaleFactor: number;

    // Root motion handling
    rootBone: string;
    preserveRootMotion: boolean;
}

class RetargetingPanel {
    /// Auto-generate bone mapping (by name)
    autoMapBones(source: Skeleton, target: Skeleton) {
        const mapping = new Map<string, string>();

        for (const sourceBone of source.bones) {
            // Try exact match
            if (target.findBone(sourceBone.name)) {
                mapping.set(sourceBone.name, sourceBone.name);
            } else {
                // Try fuzzy match (e.g., "Armature.Arm_L" → "rig.ArmLeft")
                const candidates = target.findBonesByPattern(sourceBone.name);
                if (candidates.length === 1) {
                    mapping.set(sourceBone.name, candidates[0].name);
                }
            }
        }

        return mapping;
    }

    /// Test retargeting on viewport
    testRetargeting(sourceRig: ControlRig, targetSkeleton: Skeleton, mapping: Map<string, string>) {
        // Clone source rig, remap bone references
        const retargetedRig = new ControlRig();

        for (const [sourceId, constraint] of sourceRig.constraints) {
            const retargeted = this.remapConstraint(constraint, mapping);
            retargetedRig.constraints.set(sourceId, retargeted);
        }

        // Pose target skeleton with retargeted rig
        this.previewSkeleton = targetSkeleton;
        this.evaluateRig(retargetedRig, targetSkeleton);
    }

    private remapConstraint(constraint: Constraint, mapping: Map<string, string>): Constraint {
        // Deep clone constraint, replace bone names
        const remapped = structuredClone(constraint);

        // Remap all bone references
        if (mapping.has(constraint.bone)) {
            remapped.bone = mapping.get(constraint.bone)!;
        }
        if (constraint.targetBone && mapping.has(constraint.targetBone)) {
            remapped.targetBone = mapping.get(constraint.targetBone)!;
        }
        // ... remap other bone references

        return remapped;
    }
}
```

## Interactive Posing

In the viewport, animators can drag control shapes to pose the skeleton:

```typescript
class ControlRigViewport {
    private selectedControl: ControlShape | null = null;
    private dragStartPos: [number, number] = [0, 0];

    onMouseDown(event: MouseEvent) {
        // Check if dragging a control shape
        const worldPos = this.screenToWorld(event.clientX, event.clientY);
        this.selectedControl = this.pickControl(worldPos);

        if (this.selectedControl) {
            this.dragStartPos = [event.clientX, event.clientY];
        }
    }

    onMouseMove(event: MouseEvent) {
        if (!this.selectedControl) return;

        const deltaScreen = [
            event.clientX - this.dragStartPos[0],
            event.clientY - this.dragStartPos[1]
        ];
        const deltaWorld = this.screenDeltaToWorld(deltaScreen);

        // Move control
        const bone = this.skeleton.findBone(this.selectedControl.bone);
        bone.position[0] += deltaWorld[0];
        bone.position[1] += deltaWorld[1];
        bone.position[2] += deltaWorld[2];

        // Evaluate all constraints
        this.controlRig.evaluate(this.skeleton);

        // Update mesh
        this.updateMesh();
        this.render();

        this.dragStartPos = [event.clientX, event.clientY];
    }

    /// Pick control shape under mouse
    private pickControl(worldPos: [number, number, number]): ControlShape | null {
        let closest: ControlShape | null = null;
        let closestDist = Infinity;

        for (const control of this.controlRig.controlShapes) {
            const bone = this.skeleton.findBone(control.bone);
            const controlWorldPos = bone.worldPosition;
            const dist = distance(worldPos, controlWorldPos);

            if (dist < control.size && dist < closestDist) {
                closest = control;
                closestDist = dist;
            }
        }

        return closest;
    }

    render() {
        // Draw skeleton
        this.drawSkeleton();

        // Draw control shapes
        for (const control of this.controlRig.controlShapes) {
            this.drawControl(control);
        }

        // Draw constraints (IK chains, poles, etc.)
        for (const constraint of this.controlRig.constraints.values()) {
            this.drawConstraint(constraint);
        }
    }

    private drawControl(control: ControlShape) {
        const bone = this.skeleton.findBone(control.bone);
        const pos = bone.worldPosition;

        this.gl.pushMatrix();
        this.gl.translate(pos);

        // Draw shape (sphere, cube, etc.)
        const color = this.selectedControl === control ? [1, 1, 0, 1] : control.color;
        this.drawShape(control.shape, control.size, color);

        this.gl.popMatrix();
    }
}
```

## File Format

Control Rigs are saved as `.uzcontrolrig` JSON:

```json
{
  "version": 1,
  "metadata": {
    "title": "CharacterRig",
    "skeleton": "res://models/character.uzskeleton",
    "createdDate": "2026-03-12T10:00:00Z"
  },
  "constraints": [
    {
      "id": "constraint-001",
      "name": "LeftArmIK",
      "type": "FABRIK",
      "rootBone": "LeftShoulder",
      "endBone": "LeftHand",
      "targetBone": "LeftHandTarget",
      "pole": {
        "enable": true,
        "bone": "LeftElbowPole"
      },
      "iterations": 15,
      "weight": 1.0
    },
    {
      "id": "constraint-002",
      "name": "HeadLookAt",
      "type": "LookAt",
      "bone": "Head",
      "targetBone": "LookAtTarget",
      "forwardAxis": "Z+",
      "pitchLimit": [-30, 30],
      "yawLimit": [-60, 60],
      "weight": 1.0
    }
  ],
  "controlShapes": [
    {
      "name": "LeftHandCtrl",
      "bone": "LeftHandTarget",
      "shape": "Sphere",
      "size": 8.0,
      "color": [1, 0, 0, 1],
      "position": [0, 0, 0],
      "constraint": "constraint-001",
      "debugOnly": true
    },
    {
      "name": "LookAtCtrl",
      "bone": "LookAtTarget",
      "shape": "Circle",
      "size": 10.0,
      "color": [0, 1, 0, 1],
      "position": [0, 0, 100],
      "constraint": "constraint-002",
      "debugOnly": true
    }
  ]
}
```

## C++ Runtime Evaluation

```cpp
/// Evaluate all constraints on skeleton
class ControlRig {
public:
    /// Apply all constraints to skeleton (modifies bone transforms)
    void Evaluate(Skeleton& skeleton, float deltaTime) {
        // Sort constraints by dependency
        const auto& sortedConstraints = GetSortedConstraints();

        for (const auto& constraint : sortedConstraints) {
            switch (constraint->type) {
                case ConstraintType::FABRIK:
                    static_cast<FABRIKConstraint*>(constraint)->Evaluate(skeleton);
                    break;
                case ConstraintType::TwoBoneIK:
                    static_cast<TwoBoneIKConstraint*>(constraint)->Evaluate(skeleton);
                    break;
                case ConstraintType::LookAt:
                    static_cast<LookAtConstraint*>(constraint)->Evaluate(skeleton);
                    break;
                // ... other constraint types
            }
        }
    }
};

/// Example: FABRIK IK solver
class FABRIKConstraint : public Constraint {
    void Evaluate(Skeleton& skeleton) override {
        auto root = skeleton.GetBone(rootBoneName);
        auto end = skeleton.GetBone(endBoneName);
        auto target = skeleton.GetBone(targetBoneName);

        if (!root || !end || !target) return;

        // FABRIK: iteratively move bones toward target
        glm::vec3 targetPos = target->worldTransform.position;

        for (int iter = 0; iter < iterations; ++iter) {
            // Forward pass: move from root to end
            for (auto bone = end; bone != root->parent; bone = bone->parent) {
                glm::vec3 childDir = glm::normalize(bone->worldTransform.position - bone->parent->worldTransform.position);
                float chainLength = glm::distance(bone->parent->worldTransform.position, bone->worldTransform.position);

                bone->worldTransform.position = bone->parent->worldTransform.position + childDir * chainLength;
            }

            // Backward pass: move from end back to target
            end->worldTransform.position = targetPos;
            // ... (continue FABRIK iterations)
        }

        // Apply weight blend
        for (auto bone = root; bone != nullptr; bone = bone->nextSibling) {
            bone->worldTransform.position = glm::mix(bone->restPosition, bone->worldTransform.position, weight);
        }
    }
};
```

## Comparison

| Feature | UE5 Control Rig | Godot SkeletonModifier | Blender Armature | **UzEngine** |
|---|---|---|---|---|
| **FABRIK IK** | Yes | Limited | Blender's FABRIK addon | **Yes** |
| **Two-Bone IK** | Yes | Yes | Basic IK | **Yes** |
| **Look-At** | Yes | Yes | Track To / Look At | **Yes** |
| **Pole targets** | Yes | Yes | Pole angle control | **Yes** |
| **Constraint graph** | Yes (visual editor) | Linear evaluation | DAG | **Yes (DAG)** |
| **Control shapes** | Yes (visual guides) | Limited | Bone shapes | **Yes** |
| **Retargeting** | Yes (automation) | Manual | Yes (constraint-based) | **Yes (auto + manual)** |
| **Real-time preview** | Yes (live IK) | Yes | Yes (real-time) | **Yes (interactive)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Basic IK (FABRIK, Two-Bone IK), Look-At constraint, constraint evaluation, interactive posing in viewport, `.uzcontrolrig` format |
| **Phase 4** | Advanced constraints (Spring, Copy Transform, Aim), control shape editor, retargeting system, constraint debugging visualization, multi-rig composition (reuse rigs across characters) |

