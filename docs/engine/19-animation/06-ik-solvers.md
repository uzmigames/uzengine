# Animation System — IK Solvers

> **Status**: Planned (Phase 4)
> **Source**: `engine/animation/ik_solvers.h/.cpp`
> **Depends on**: Skeletal mesh (§19.3), animation state machine (§19.5)

## Purpose

Inverse Kinematics (IK) solves the opposite problem of forward kinematics: given an end-effector goal (hand position, foot position), compute the joint angles that reach it. Used for:

- **Foot IK** — feet stay planted on uneven terrain
- **Hand IK** — hands reach toward objects
- **Look-at** — head looks toward target
- **Two-bone IK** — elbows/knees bend naturally

## FABRIK (Forward And Backward Reaching IK)

Most general-purpose IK solver. Iterative algorithm that reaches toward a goal:

```cpp
class FABRIKSolver {
public:
    struct Chain {
        std::vector<uint32_t> joint_ids;  // root to end-effector
        std::vector<float> bone_lengths;  // distance between joints
        Vec3 target = {0, 0, 0};          // goal position for end-effector
        float tolerance = 0.01f;          // acceptable distance to target
    };

    /// Solve IK for a chain.
    /// Returns number of iterations performed.
    int Solve(Chain& chain, const std::vector<Transform>& current_pose,
              std::vector<Transform>& out_pose, int max_iterations = 10) {
        out_pose = current_pose;

        for (int iteration = 0; iteration < max_iterations; ++iteration) {
            // ─── Forward Pass (backward reach) ──────────────────────
            // Start from end-effector, pull toward root
            for (int i = (int)chain.joint_ids.size() - 1; i > 0; --i) {
                uint32_t joint_id = chain.joint_ids[i];
                uint32_t parent_id = chain.joint_ids[i - 1];

                Vec3 joint_pos = out_pose[joint_id].position;
                Vec3 parent_pos = out_pose[parent_id].position;

                // Direction from parent to joint
                Vec3 dir = (joint_pos - parent_pos).Normalized();

                // New parent position (pull back by bone length)
                out_pose[parent_id].position = joint_pos - dir * chain.bone_lengths[i - 1];
            }

            // ─── Backward Pass (forward reach) ──────────────────────
            // Start from root, push toward end-effector goal
            Vec3 root_pos = out_pose[chain.joint_ids[0]].position;  // fix root

            for (size_t i = 0; i < chain.joint_ids.size() - 1; ++i) {
                uint32_t joint_id = chain.joint_ids[i];
                uint32_t child_id = chain.joint_ids[i + 1];

                Vec3 child_pos = out_pose[child_id].position;

                // Direction from joint to child
                Vec3 dir = (child_pos - out_pose[joint_id].position).Normalized();

                // New child position (push forward by bone length)
                out_pose[child_id].position = out_pose[joint_id].position + dir * chain.bone_lengths[i];
            }

            // ─── Convergence Check ──────────────────────────────────
            Vec3 end_effector_pos = out_pose[chain.joint_ids.back()].position;
            float distance = (end_effector_pos - chain.target).Length();

            if (distance < chain.tolerance) {
                return iteration + 1;  // converged
            }

            // Pull end-effector toward target
            Vec3 dir_to_target = (chain.target - end_effector_pos).Normalized();
            out_pose[chain.joint_ids.back()].position = end_effector_pos + dir_to_target * distance;
        }

        return max_iterations;  // didn't converge
    }
};
```

**Advantages:**
- Works for chains of any length
- Handles multiple chains
- Stable and predictable

**Disadvantages:**
- Loss of joint rotations (post-processing needed)
- Slower than CCD
- Requires rotation reconstruction

## CCD (Cyclic Coordinate Descent)

Faster but less stable. Good for real-time constraints:

```cpp
class CCDSolver {
public:
    int Solve(const std::vector<uint32_t>& chain,
              const std::vector<float>& bone_lengths,
              Vec3 target,
              std::vector<Transform>& pose,
              int max_iterations = 5) {
        for (int iteration = 0; iteration < max_iterations; ++iteration) {
            // Iterate backward from end-effector to root
            for (int i = (int)chain.size() - 2; i >= 0; --i) {
                uint32_t joint_id = chain[i];
                uint32_t end_id = chain.back();

                // Current positions
                Vec3 joint_pos = pose[joint_id].position;
                Vec3 end_pos = pose[end_id].position;

                // Vectors
                Vec3 to_end = (end_pos - joint_pos).Normalized();
                Vec3 to_target = (target - joint_pos).Normalized();

                // Compute rotation from end→end to end→target
                Vec3 axis = Cross(to_end, to_target);
                if (axis.Length() > 0.001f) {
                    float angle = std::acos(std::clamp(Dot(to_end, to_target), -1.0f, 1.0f));

                    // Rotate this joint
                    Quat rotation = Quat::FromAxisAngle(axis.Normalized(), angle);
                    pose[joint_id].rotation = rotation * pose[joint_id].rotation;

                    // Recompute child positions (forward kinematics)
                    UpdateChildPositions(joint_id, pose);
                }
            }

            // Check convergence
            Vec3 end_pos = pose[chain.back()].position;
            if ((end_pos - target).Length() < 0.01f) {
                return iteration + 1;
            }
        }

        return max_iterations;
    }

private:
    void UpdateChildPositions(uint32_t joint_id, std::vector<Transform>& pose) {
        // Simple FK: update all children
        // (In practice, maintain parent->child relationships)
    }
};
```

## Two-Bone IK (Elbow/Knee)

Closed-form solution for 3-joint chains (shoulder→elbow→hand):

```cpp
class TwoBoneIKSolver {
public:
    /// Solve IK for elbow/knee joint.
    /// Returns true if solution found.
    bool Solve(Vec3 shoulder_pos, Vec3 elbow_pos, float upper_arm_len, float forearm_len,
               Vec3 hand_target, Quat pole_vector,
               Vec3& out_elbow_pos) {
        // Distance from shoulder to hand target
        float hand_distance = (hand_target - shoulder_pos).Length();

        // Check if target is reachable
        float max_reach = upper_arm_len + forearm_len;
        if (hand_distance > max_reach) {
            // Target unreachable, extend arm
            out_elbow_pos = shoulder_pos + (hand_target - shoulder_pos).Normalized() * upper_arm_len;
            return false;
        }

        if (hand_distance < std::abs(upper_arm_len - forearm_len)) {
            // Target too close
            out_elbow_pos = shoulder_pos + (hand_target - shoulder_pos).Normalized() * upper_arm_len;
            return false;
        }

        // Law of cosines: angle at shoulder
        float cos_shoulder = (hand_distance * hand_distance + upper_arm_len * upper_arm_len
                             - forearm_len * forearm_len) / (2.0f * upper_arm_len * hand_distance);
        cos_shoulder = std::clamp(cos_shoulder, -1.0f, 1.0f);
        float angle_shoulder = std::acos(cos_shoulder);

        // Angle at elbow
        float cos_elbow = (upper_arm_len * upper_arm_len + forearm_len * forearm_len
                          - hand_distance * hand_distance) / (2.0f * upper_arm_len * forearm_len);
        cos_elbow = std::clamp(cos_elbow, -1.0f, 1.0f);
        float angle_elbow = std::acos(cos_elbow);

        // Elbow position
        Vec3 dir = (hand_target - shoulder_pos).Normalized();
        out_elbow_pos = shoulder_pos + dir * std::cos(angle_shoulder) * upper_arm_len;
        out_elbow_pos += Perpendicular(dir) * std::sin(angle_shoulder) * upper_arm_len;

        return true;
    }

private:
    Vec3 Perpendicular(const Vec3& v) const {
        // Find vector perpendicular to v
        if (std::abs(v.x) < 0.9f) {
            return Cross(v, {1, 0, 0}).Normalized();
        } else {
            return Cross(v, {0, 1, 0}).Normalized();
        }
    }
};
```

## Look-At IK

Rotate head/neck to look toward a target:

```cpp
class LookAtIKSolver {
public:
    /// Rotate joint to face target.
    Quat Solve(Vec3 joint_pos, Vec3 forward_axis, Vec3 target) {
        Vec3 to_target = (target - joint_pos).Normalized();

        // Rotation from forward to target
        Vec3 axis = Cross(forward_axis, to_target);
        if (axis.Length() < 0.001f) {
            // Already facing target (or opposite)
            if (Dot(forward_axis, to_target) > 0.0f) {
                return {0, 0, 0, 1};  // no rotation
            } else {
                return Quat::FromAxisAngle({0, 1, 0}, Pi);  // flip 180
            }
        }

        float angle = std::acos(std::clamp(Dot(forward_axis, to_target), -1.0f, 1.0f));
        return Quat::FromAxisAngle(axis.Normalized(), angle);
    }
};
```

Usage in animation:

```cpp
auto head_joint = skeleton->GetJointByName("Head");
LookAtIKSolver look_at;

void Update() {
    Quat look_rotation = look_at.Solve(
        head_joint->world_pos,
        {0, 0, 1},  // forward
        player_target->GetPosition()
    );

    pose[head_joint->id].rotation = look_rotation;
}
```

## Pole Vector Constraint

Guide IK toward a preferred elbow/knee position:

```cpp
/// Pole vector pulls elbow toward a position (prevents twist).
Vec3 ApplyPoleVectorConstraint(Vec3 shoulder, Vec3 elbow, Vec3 hand,
                               Vec3 pole_position, float influence = 1.0f) {
    // Plane defined by shoulder→hand
    Vec3 hand_dir = (hand - shoulder).Normalized();

    // Project pole onto plane
    Vec3 to_pole = (pole_position - shoulder);
    Vec3 pole_on_plane = to_pole - Dot(to_pole, hand_dir) * hand_dir;
    pole_on_plane = pole_on_plane.Normalized();

    // Move elbow toward pole (within limits)
    Vec3 to_elbow = (elbow - shoulder);
    float elbow_distance = to_elbow.Length();

    Vec3 new_elbow_dir = Lerp(to_elbow.Normalized(), pole_on_plane, influence);
    return shoulder + new_elbow_dir * elbow_distance;
}
```

## Foot IK

Keep feet planted on terrain during animations:

```cpp
class FootIKSolver {
private:
    struct FootChain {
        std::vector<uint32_t> joint_ids;  // hip → knee → ankle
        float chain_length = 0.0f;
        Vec3 ankle_target = {0, 0, 0};    // desired ankle position
        float foot_height_offset = 0.0f;  // height above ground
    };

public:
    void Apply(std::vector<Transform>& pose, const World& world) {
        for (const auto& foot : feet) {
            // Raycast down from animated ankle position to find ground
            Vec3 ankle_pos = pose[foot.joint_ids.back()].position;
            PhysicsRayResult ground_hit = world.Raycast(ankle_pos + {0, 0.5f, 0}, {0, -1, 0}, 2.0f);

            if (ground_hit.hit) {
                // Foot target: on ground with offset
                foot.ankle_target = ground_hit.position + {0, foot_height_offset, 0};

                // Solve IK to keep foot on ground
                FABRIKSolver fabrik;
                FABRIKSolver::Chain chain;
                chain.joint_ids = foot.joint_ids;
                chain.target = foot.ankle_target;

                fabrik.Solve(chain, pose, pose, 3);  // quick IK, 3 iterations
            }
        }
    }
};
```

## Integration with Animation Controller

```cpp
class SkeletalMeshComponent {
private:
    FABRIKSolver ik_fabrik;
    TwoBoneIKSolver ik_two_bone;
    FootIKSolver ik_foot;

public:
    void UpdateIK(float delta_time) {
        // 1. Sample animation from state machine
        std::vector<Transform> animated_pose = anim_controller->GetPose();

        // 2. Apply IK solvers
        ik_foot.Apply(animated_pose, world);                    // feet on ground
        ik_two_bone.Solve(...);                                  // elbow/knee

        // 3. Compute final joint matrices with IK-modified pose
        skeleton->ComputeJointMatrices(animated_pose, joint_matrices);
    }
};
```

## Performance & Stability

```
Per-Frame Cost (typical):
├── FABRIK: 0.1ms for 4-joint chain, 10 iterations
├── CCD: 0.05ms for 4-joint chain, 5 iterations
├── Two-Bone: 0.01ms (closed-form)
└── Foot IK with raycasts: 0.2ms (includes physics query)

Stability Notes:
├── Use pole vectors for multiple-solution chains (elbows, knees)
├── Clamp IK influence (0.0-1.0) to prevent overly rigid behavior
├── Combine FK (animation) + IK (constraints) with blending
└── Test on uneven terrain to catch foot-penetration bugs
```

## Rules

1. **ALWAYS** validate chain reachability — don't let IK fight impossible targets
2. **NEVER** apply IK every joint simultaneously — order matters (bottom-up usually better)
3. **ALWAYS** blend IK influence (0.0-1.0) — pure IK looks robotic
4. **NEVER** use IK on every bone — only where needed (feet, hands, head)
5. **ALWAYS** post-process IK solutions with pole vectors — prevents twist artifacts
