# Skeleton, Control Rig & Socket Editor

> **Status**: Planned (Phase 3)
> **Owner**: `game.dll` (skeleton/rig runtime), VSCode extension (editor UI)
> **Dependencies**: skeletal-mesh.md, animation-system.md, physics-system.md

---

## 1. Overview & Goals

The Skeleton, Control Rig & Socket Editor is a unified toolset inside the VSCode
extension that allows technical animators and riggers to author, inspect, and tune
every aspect of a character's skeletal setup without leaving the editor.

### Target Users

| Persona | Primary Tasks |
|---------|---------------|
| **Technical Animator** | Build IK control rigs, tune FK/IK blends, author retarget maps |
| **Rigger** | Define bone hierarchies, set up sockets, configure ragdoll bodies |
| **Character Artist** | Preview LOD bone reduction, validate skin weights per LOD |
| **Gameplay Programmer** | Attach weapons via sockets, query rig controls from Lua scripts |

### Design Principles

1. **AI-friendly file formats** -- all formats are plain-text INI-like, diffable, mergeable
2. **Non-destructive workflow** -- edits produce new asset versions; original meshes untouched
3. **Live preview** -- every change is reflected in the 3D viewport within one frame
4. **Integration-first** -- Jolt Physics ragdoll, animation state machine, and Lua API
   are first-class citizens, not afterthoughts
5. **Multipart characters** -- modular armor/outfit systems are a core use case

### Scope

| Feature | File Format | Runtime Component |
|---------|------------|-------------------|
| Skeleton Editor | `.uzskel` | `Skeleton`, `BoneInfo` |
| Control Rig Editor | `.uzrig` | `ControlRig`, `IKChain` |
| Socket Editor | `.uzskel` (sockets section) | `SocketRegistry`, `CSocket` |
| Ragdoll Setup Editor | `.uzphys` | `PhysicsAsset`, Jolt `RagdollSettings` |
| LOD Bone Reduction | `.uzskel` (lod section) | `BoneLODMask` |

---

## 2. Skeleton Editor

### 2.1 Layout

```
+--------------------------------------------------------------------+
| Skeleton Editor: HumanoidSkeleton.uzskel                    [X]   |
+------------------+-----------------------------+-------------------+
| BONE HIERARCHY   |     3D VIEWPORT             | PROPERTIES        |
|                  |                             |                   |
| v Root           |         O  (head)           | Bone: spine_02    |
|   v Pelvis       |         |                   |                   |
|     v Spine_01   |     O---+---O  (arms)       | -- Local Transform|
|       v Spine_02 |     |   |   |               | Pos: 0, 0, 12.5  |
|         v Spine_03|    O   O   O               | Rot: 0, 0, 0, 1  |
|           v Neck  |    |   |   |               | Scl: 1, 1, 1     |
|             Head  |    O   O   O               |                   |
|           v Clav_L|   /    |    \              | -- Bone Info      |
|             Arm_L |  O     O     O  (legs)     | Length: 12.5 cm   |
|               ...  |  |    |     |             | Radius: 2.0 cm    |
|           v Clav_R|  O     O     O             | Parent: Spine_01  |
|             Arm_R |                             | Index: 4          |
|     v Thigh_L    |   [ T-Pose ] [ A-Pose ]    |                   |
|       Calf_L     |   [ Custom ] [ Reset  ]    | -- Reference Pose |
|         Foot_L   |                             | [Store Current]   |
|     v Thigh_R    |   Bones: 67  Selected: 1   | [Reset to Bind]   |
|       ...        |   LOD0: 67  LOD1: 42       |                   |
+------------------+-----------------------------+-------------------+
| STATUS: Skeleton loaded | 67 bones | Modified                      |
+--------------------------------------------------------------------+
```

### 2.2 3D Viewport

The viewport renders the skeleton using a DX12 debug draw pass:

- **Joints**: solid spheres (radius proportional to bone radius property)
- **Bones**: lines connecting parent joint to child joint
- **Axes**: RGB arrows at each joint (R=X, G=Y, B=Z), scaled to bone length
- **Selection**: selected bone highlighted in yellow, unselected in white
- **Mesh overlay**: optional semi-transparent mesh rendering (toggle via toolbar)

Viewport controls:

| Input | Action |
|-------|--------|
| Left-click bone | Select bone in hierarchy and properties |
| Ctrl+click | Multi-select bones |
| Middle-drag | Orbit camera around skeleton center |
| Scroll | Zoom in/out |
| Right-drag | Pan camera |
| F | Frame selected bone |
| Numpad 1/3/7 | Front / Side / Top orthographic view |

### 2.3 Bone Hierarchy Tree (Left Panel)

A collapsible tree view showing parent-child relationships:

- **Icons**: bone icon for regular bones, star for root, lock for immutable bones
- **Rename**: double-click to rename (validates uniqueness)
- **Reparent**: drag-and-drop a bone onto a new parent
- **Context menu** (right-click):
  - Add Child Bone
  - Delete Bone (and reparent children to grandparent)
  - Delete Bone + Children
  - Duplicate Bone Chain
  - Copy Bone Path (e.g., `root/pelvis/spine_01`)
  - Select Children (recursive)
  - Mirror Selection (L <-> R name convention)

### 2.4 Properties Inspector (Right Panel)

Displays editable properties for the selected bone:

| Property | Type | Editable | Notes |
|----------|------|----------|-------|
| Name | string | Yes | Must be unique within skeleton |
| Parent | dropdown | Yes | Reparent via dropdown |
| Index | int | No | Auto-assigned, display only |
| Local Position | Vec3 | Yes | Bind-pose local translation |
| Local Rotation | Quat/Euler | Yes | Toggle quat vs euler display |
| Local Scale | Vec3 | Yes | Usually (1,1,1) |
| Bone Length | float | Yes | Distance to first child (visual) |
| Bone Radius | float | Yes | Joint sphere radius (visual + ragdoll hint) |
| Tags | string[] | Yes | Custom tags: "ik_target", "weapon_bone" |

### 2.5 Reference Pose Panel

| Button | Action |
|--------|--------|
| T-Pose | Reset skeleton to standard T-pose (arms horizontal) |
| A-Pose | Reset skeleton to A-pose (arms 45 degrees down) |
| Store Current | Save current viewport pose as custom reference |
| Reset to Bind | Restore original bind pose from file |

The reference pose is stored in the `.uzskel` file as an optional `[reference_pose]`
section. It is used by additive animation computation and retarget mapping.

---

## 3. Bone Operations

### 3.1 Add Bone

1. Select a parent bone in the hierarchy tree
2. Right-click > "Add Child Bone"
3. A new bone is created at the parent's tip (parent position + parent direction * parent length)
4. Default name: `new_bone_NNN` (auto-incremented)
5. The new bone is immediately selected for editing

```cpp
// SkeletonEditor::add_bone()
int32_t add_bone(Skeleton& sk, int32_t parent_idx, std::string_view name) {
    BoneInfo bone;
    bone.name = std::string(name);
    bone.parent_index = parent_idx;
    bone.bind_translation = {0.0f, 0.0f, 0.0f}; // local to parent
    bone.bind_rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    bone.bind_scale = {1.0f, 1.0f, 1.0f};

    int32_t idx = static_cast<int32_t>(sk.bones.size());
    sk.bones.push_back(bone);
    sk.name_to_index[bone.name] = idx;
    sk.compute_inverse_bind_poses();
    return idx;
}
```

### 3.2 Remove Bone

Two modes:

- **Remove + Reparent Children**: children become children of the removed bone's parent
- **Remove + Delete Children**: entire subtree is deleted

Both modes update all bone indices across the skeleton and any referencing assets
(sockets, ragdoll bodies, control rig chains). A validation pass runs after removal.

### 3.3 Reparent Bone

Drag-and-drop in the hierarchy tree. The bone's local transform is recomputed so its
world-space position does not change:

```cpp
void reparent_bone(Skeleton& sk, int32_t bone_idx, int32_t new_parent_idx) {
    glm::mat4 old_world = compute_bone_world(sk, bone_idx);
    sk.bones[bone_idx].parent_index = new_parent_idx;

    // Recompute local transform to preserve world position
    glm::mat4 new_parent_world = compute_bone_world(sk, new_parent_idx);
    glm::mat4 new_local = glm::inverse(new_parent_world) * old_world;
    decompose(new_local,
              sk.bones[bone_idx].bind_translation,
              sk.bones[bone_idx].bind_rotation,
              sk.bones[bone_idx].bind_scale);

    sk.compute_inverse_bind_poses();
}
```

### 3.4 Merge Skeletons (Multipart Character Assembly)

Combines two skeletons into one master skeleton. Used for modular character assembly
where separate mesh pieces each have their own bone subset.

```
Master Skeleton (humanoid_base.uzskel) = 67 bones
  + Armor Skeleton (heavy_armor.uzskel) = 12 additional bones (pauldron_l, pauldron_r, ...)
  = Merged Skeleton = 79 bones (original 67 preserved, 12 appended)
```

Merge rules:
- Bones with matching names are unified (not duplicated)
- New bones are appended after the last existing bone
- Parent references are remapped to the merged index space
- A merge log records which bones came from which source

### 3.5 Retarget Mapping Table

Maps bones between two skeletons for animation retargeting:

```ini
[retarget_map name="Mixamo_to_UzHumanoid"]
source_skeleton = res://skeletons/mixamo_y_bot.uzskel
target_skeleton = res://skeletons/humanoid.uzskel

; Format: source_bone = target_bone
mixamorig:Hips     = pelvis
mixamorig:Spine    = spine_01
mixamorig:Spine1   = spine_02
mixamorig:Spine2   = spine_03
mixamorig:Neck     = neck
mixamorig:Head     = head
mixamorig:LeftShoulder   = clavicle_l
mixamorig:LeftArm        = upperarm_l
mixamorig:LeftForeArm    = lowerarm_l
mixamorig:LeftHand       = hand_l
; ... (all mapped bones)
```

The retarget mapping is stored in a `.uzretarget` file (INI-like, AI-friendly).

---

## 4. Control Rig Editor

### 4.1 Overview

The Control Rig provides animator-friendly manipulators (IK handles, FK controls,
space switches) that sit on top of the raw bone hierarchy. Controls are separate
objects with their own hierarchy, connected to bones via constraints.

### 4.2 Layout

```
+--------------------------------------------------------------------+
| Control Rig Editor: humanoid_rig.uzrig                      [X]   |
+------------------+-----------------------------+-------------------+
| CONTROL TREE     |     3D VIEWPORT             | PROPERTIES        |
|                  |                             |                   |
| v Root_Ctrl      |         [+]  head_ctrl     | Control: hand_l_ik|
|   v COG_Ctrl     |          |                  |                   |
|     v Spine_FK   |     [O]--+--[O]  (arms)    | -- Control Shape  |
|       FK_Spine01 |      |   |   |              | Shape: circle     |
|       FK_Spine02 |     [#]  |  [#]  (IK hands)| Size: 0.15        |
|       FK_Spine03 |      |   |   |              | Color: #FF4444    |
|     v Arm_L_IK   |     [ ]  |  [ ]  (FK hips) |                   |
|       hand_l_ik  |      |   |   |              | -- IK Settings    |
|       elbow_l_pv |     [O]  |  [O]  (legs)    | Chain Start: upper|
|     v Arm_R_IK   |      |   |   |              | Chain End: hand_l |
|       hand_r_ik  |     [O]  |  [O]  (feet)    | Pole Vector: elbow|
|     v Leg_L_IK   |                             | Solver: TwoBone   |
|       foot_l_ik  |   Legend:                   |                   |
|       knee_l_pv  |   [+] = cross  [O] = circle | -- Space Switch  |
|     v Leg_R_IK   |   [#] = box    [ ] = sphere | Spaces: world,   |
|       foot_r_ik  |                             |   local, COG      |
|       knee_r_pv  |   [ FK/IK ] blend: 1.0     | Current: world    |
+------------------+-----------------------------+-------------------+
```

### 4.3 IK Handle Creation

Workflow for creating a new IK control:

1. In the Skeleton Editor, select the chain end bone (e.g., `hand_l`)
2. Menu > "Create IK Handle" or press `I`
3. The editor auto-detects the chain:
   - Walks up the parent chain until it finds a branch point or a tagged bone
   - For `hand_l`: chain = `upperarm_l -> lowerarm_l -> hand_l` (3 bones, 2 segments)
4. Creates three controls:
   - `hand_l_ik` -- end effector (box shape, positioned at hand)
   - `elbow_l_pv` -- pole vector (diamond shape, positioned at elbow hint)
   - `arm_l_fkik_switch` -- FK/IK blend slider (0.0 = pure FK, 1.0 = pure IK)

```cpp
struct IKChain {
    std::string     name;           // "arm_l"
    int32_t         bone_start;     // upperarm_l index
    int32_t         bone_end;       // hand_l index
    int32_t         bone_count;     // 3 (start, mid, end)
    IKSolverType    solver;         // TwoBone or FABRIK
    int32_t         max_iterations; // FABRIK only (default: 10)
    float           tolerance;      // FABRIK only (default: 0.001)
    ControlHandle   effector;       // hand_l_ik control
    ControlHandle   pole_vector;    // elbow_l_pv control
    float           fk_ik_blend;    // 0.0 = FK, 1.0 = IK
};

enum class IKSolverType : uint8_t {
    TwoBone = 0,    // Analytical, 2-segment chains (arms, legs)
    FABRIK  = 1,    // Iterative, arbitrary-length chains (spine, tail)
};
```

### 4.4 Control Shapes

Controls are rendered as colored wireframe shapes in the viewport. They are visual
manipulators only -- they do not affect the mesh.

| Shape | Icon | Typical Use | Wireframe |
|-------|------|-------------|-----------|
| Box | `[#]` | IK end effectors (hands, feet) | 12-edge wireframe cube |
| Circle | `[O]` | FK rotations (spine, neck) | 32-segment wireframe ring |
| Sphere | `[ ]` | COG, root control | 3-axis wireframe sphere |
| Cross | `[+]` | Head aim, look-at targets | 6-line axis cross |
| Diamond | `<>` | Pole vectors (elbows, knees) | 8-edge wireframe octahedron |
| Arrow | `->` | Space switch indicators | Directional arrow |

Properties per control:

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| name | string | auto | Unique within rig |
| shape | enum | Box | See table above |
| size | float | 0.1 | World-space scale |
| color | Color | #FFFF00 | RGB hex, shown in viewport |
| visible | bool | true | Can hide for cleanliness |
| selectable | bool | true | false = cannot be picked in viewport |

### 4.5 Control Hierarchy

The control hierarchy is independent of the skeleton bone hierarchy. Controls can
be nested under other controls for organizational purposes:

```
Root_Ctrl (sphere, yellow)
  |-- COG_Ctrl (sphere, orange)        -- drives pelvis
  |     |-- Spine_FK (group)
  |     |     |-- FK_Spine01 (circle)
  |     |     |-- FK_Spine02 (circle)
  |     |     +-- FK_Spine03 (circle)
  |     |-- Hip_FK (group)
  |     |     |-- FK_Hip_L (circle)
  |     |     +-- FK_Hip_R (circle)
  |     +-- Leg_L_IK (group)
  |           |-- foot_l_ik (box)
  |           +-- knee_l_pv (diamond)
  +-- Head_Aim (cross)                 -- aim constraint target
```

Controls do NOT need a 1:1 mapping to bones. A single control can drive multiple
bones (e.g., `COG_Ctrl` drives the pelvis bone plus offsets the entire skeleton).

### 4.6 FK/IK Switch

Each limb has an FK/IK blend parameter:

```
fk_ik_blend = 0.0  -->  Pure FK (direct bone rotations from animation)
fk_ik_blend = 0.5  -->  50/50 blend
fk_ik_blend = 1.0  -->  Pure IK (solver drives bone chain to reach effector)
```

The blend is applied per-frame during pose evaluation:

```cpp
void evaluate_fk_ik_blend(const IKChain& chain, const Pose& fk_pose,
                          const Pose& ik_pose, float blend, Pose& out) {
    for (int i = chain.bone_start; i <= chain.bone_end; ++i) {
        out.bones[i].position = glm::mix(fk_pose.bones[i].position,
                                         ik_pose.bones[i].position, blend);
        out.bones[i].rotation = glm::slerp(fk_pose.bones[i].rotation,
                                            ik_pose.bones[i].rotation, blend);
        out.bones[i].scale    = glm::mix(fk_pose.bones[i].scale,
                                         ik_pose.bones[i].scale, blend);
    }
}
```

### 4.7 Space Switching

Controls can operate in different coordinate spaces:

| Space | Description |
|-------|-------------|
| World | Control transform is in world space (stays put when character moves) |
| Local | Control transform is relative to its parent control |
| Parent | Control follows the driven bone's parent in the skeleton |
| COG | Control follows the Center of Gravity control |
| Custom | Control follows an arbitrary other control |

Space switching is a runtime property. Changing space preserves the control's
visual position (the local transform is recomputed).

```cpp
enum class ControlSpace : uint8_t {
    World  = 0,
    Local  = 1,
    Parent = 2,
    COG    = 3,
    Custom = 4,
};

struct SpaceSwitch {
    ControlHandle       control;
    ControlSpace        current_space;
    ControlHandle       custom_target;   // only used when space == Custom
    std::vector<ControlSpace> available; // spaces this control supports
};
```

### 4.8 Constraints

Constraints connect controls to bones or controls to other controls:

| Constraint | Description | Parameters |
|------------|-------------|------------|
| Position | Lock control position to target | weight (0-1), offset Vec3 |
| Orientation | Lock control rotation to target | weight (0-1), offset Quat |
| Aim | Point control's forward axis at target | aim_axis, up_axis, up_vector |
| Parent | Fully follow target transform | weight (0-1), maintain_offset bool |
| Scale | Match target scale | weight (0-1) |
| Pole Vector | Hint mid-joint direction for IK | (automatic with IK chains) |

```cpp
struct RigConstraint {
    std::string         name;
    ConstraintType      type;           // Position, Orientation, Aim, Parent, Scale
    ControlHandle       source;         // control being driven
    ControlHandle       target;         // control or bone driving it
    float               weight;         // 0.0 - 1.0
    glm::vec3           offset_pos;     // position offset
    glm::quat           offset_rot;     // rotation offset
    glm::vec3           aim_axis;       // Aim constraint: which axis points at target
    glm::vec3           up_axis;        // Aim constraint: which axis is "up"
};

enum class ConstraintType : uint8_t {
    Position    = 0,
    Orientation = 1,
    Aim         = 2,
    Parent      = 3,
    Scale       = 4,
};
```

Constraints are evaluated in order (top to bottom in the constraint list). Reordering
changes the result.

### 4.9 Control Rig Lua API

The control rig is accessible from Lua scripts for procedural animation:

```lua
-- Get control rig on an entity
local rig = self:get_rig()

-- Read/write IK blend
local blend = rig:get_blend("arm_l")      -- returns 0.0 - 1.0
rig:set_blend("arm_l", 1.0)               -- switch to IK

-- Get/set control transform
local ctrl = rig:get_control("hand_l_ik")
ctrl.position = Vec3(1.0, 1.2, 0.5)       -- move IK target
ctrl.rotation = Quat(0, 0, 0, 1)          -- set IK target rotation

-- Space switching
rig:set_space("hand_l_ik", "world")        -- switch to world space
local space = rig:get_space("hand_l_ik")   -- returns "world"

-- Query chain bones
local bones = rig:get_chain_bones("arm_l") -- {"upperarm_l","lowerarm_l","hand_l"}

-- Enable/disable constraint
rig:set_constraint_weight("head_aim", 0.5) -- half-weight aim constraint

-- Programmatic IK target (e.g., reach for object)
function M:on_update(ctx, dt)
    local target_pos = ctx.scene:find_node("PickupItem").position
    local rig = self:get_rig()
    rig:set_blend("arm_r", 1.0)
    local ctrl = rig:get_control("hand_r_ik")
    ctrl.position = target_pos
end
```

---

## 5. Socket System

### 5.1 Overview

A **Socket** is a named transform attached to a bone. Sockets define attachment points
for weapons, shields, visual effects, cameras, and any other entity that needs to
follow a bone's motion.

### 5.2 Socket Types

| Type | Convention | Typical Use |
|------|-----------|-------------|
| Weapon | `weapon_*` | `weapon_r`, `weapon_l`, `weapon_back` |
| Shield | `shield_*` | `shield_l`, `shield_back` |
| FX | `fx_*` | `fx_muzzle`, `fx_footstep_l`, `fx_footstep_r`, `fx_blood_chest` |
| Camera | `cam_*` | `cam_head`, `cam_shoulder`, `cam_firstperson` |
| Accessory | `acc_*` | `acc_hat`, `acc_belt_front`, `acc_necklace` |
| Custom | user-defined | Any name not matching a convention prefix |

### 5.3 Socket Data

```cpp
struct Socket {
    std::string     name;               // unique within skeleton
    int32_t         bone_index;         // parent bone
    glm::vec3       local_position;     // offset from bone
    glm::quat       local_rotation;     // rotation offset from bone
    glm::vec3       local_scale;        // scale (usually 1,1,1)
    SocketType      type;               // inferred from name prefix
    bool            visible_in_editor;  // show gizmo in editor viewport
};

struct SocketRegistry {
    std::vector<Socket>                          sockets;
    std::unordered_map<std::string, uint32_t>    name_to_index;

    const Socket* find(std::string_view name) const;
    glm::mat4 get_world_transform(uint32_t idx,
                                  const std::vector<glm::mat4>& bone_world) const;
};
```

### 5.4 Socket Editor Panel

```
+------------------------------------------------------+
| SOCKETS: HumanoidSkeleton.uzskel                     |
+------------------------------------------------------+
| [+] Add Socket   [Copy] [Paste]   [Delete]           |
+------------------------------------------------------+
| Name              | Bone        | Type     | Visible  |
|-------------------|-------------|----------|----------|
| weapon_r          | hand_r      | Weapon   | [x]      |
| weapon_l          | hand_l      | Weapon   | [x]      |
| weapon_back       | spine_03    | Weapon   | [x]      |
| shield_l          | lowerarm_l  | Shield   | [x]      |
| fx_muzzle         | weapon_r    | FX       | [x]      |
| fx_footstep_l     | foot_l      | FX       | [ ]      |
| fx_footstep_r     | foot_r      | FX       | [ ]      |
| cam_head          | head        | Camera   | [x]      |
| cam_shoulder      | clavicle_r  | Camera   | [x]      |
+------------------------------------------------------+

Selected: weapon_r
+------------------------------------------------------+
| SOCKET INSPECTOR                                      |
+------------------------------------------------------+
| Name:     [weapon_r          ]                        |
| Bone:     [hand_r         v]  (dropdown)              |
| Type:     Weapon (auto from prefix)                   |
|                                                       |
| Position: X [0.02 ] Y [0.08 ] Z [-0.01]              |
| Rotation: X [0.0  ] Y [90.0 ] Z [0.0  ] (euler deg) |
| Scale:    X [1.0  ] Y [1.0  ] Z [1.0  ]              |
|                                                       |
| [Preview Attachment...]  [Copy Transform]             |
+------------------------------------------------------+
```

### 5.5 Socket Viewport Gizmo

Each visible socket is drawn in the 3D viewport as:

- A colored axis tripod (smaller than bone axes, 50% scale)
- A label with the socket name (togglable)
- Color coding by type:
  - Weapon: red
  - Shield: blue
  - FX: green
  - Camera: magenta
  - Accessory: yellow
  - Custom: white

Sockets can be selected and transformed interactively using the standard
translate/rotate gizmos.

### 5.6 Socket Preview

The "Preview Attachment" button opens a dialog where a mesh asset can be loaded
and placed at the socket transform. This is a viewport-only preview; it does not
modify any file.

### 5.7 Sockets in `.uzskel`

Sockets are stored in the same `.uzskel` file as the skeleton:

```ini
[uzskeleton version=1 name="HumanoidSkeleton" bone_count=67]

; ... bone definitions ...

[socket name=weapon_r bone=hand_r]
position = [0.02, 0.08, -0.01]
rotation = [0.0, 0.707, 0.0, 0.707]
scale    = [1.0, 1.0, 1.0]

[socket name=weapon_l bone=hand_l]
position = [-0.02, 0.08, 0.01]
rotation = [0.0, -0.707, 0.0, 0.707]
scale    = [1.0, 1.0, 1.0]

[socket name=fx_muzzle bone=hand_r]
position = [0.0, 0.5, 0.0]
rotation = [0.0, 0.0, 0.0, 1.0]
scale    = [1.0, 1.0, 1.0]

[socket name=cam_head bone=head]
position = [0.0, 0.05, 0.12]
rotation = [0.0, 0.0, 0.0, 1.0]
scale    = [1.0, 1.0, 1.0]
```

### 5.8 Socket Lua API

```lua
-- Attach a child entity to a socket
local weapon = ctx.scene:spawn_prefab("res://prefabs/sword.uzprefab")
self:attach_to_socket(weapon, "weapon_r")

-- Detach
self:detach_from_socket(weapon)

-- Query socket world transform
local xform = self:get_socket_transform("fx_muzzle")
-- xform.position, xform.rotation, xform.forward

-- Spawn VFX at socket
local pos = self:get_socket_transform("fx_muzzle").position
ctx.vfx:spawn("res://vfx/muzzle_flash.uzparticles", pos)

-- Iterate all sockets
local sockets = self:get_sockets()
for _, s in ipairs(sockets) do
    ctx:log(s.name .. " on bone " .. s.bone)
end
```

### 5.9 ECS Component

```cpp
// Attached to entities that are socketed onto another entity
struct CSocketAttachment {
    entt::entity    parent_entity;      // entity that owns the skeleton
    uint32_t        socket_index;       // index into SocketRegistry
    bool            inherit_scale;      // default: true
};
```

Each frame, the socket system updates socketed entities:

```cpp
void update_socket_attachments(entt::registry& world) {
    auto view = world.view<CSocketAttachment, CTransform>();
    for (auto [entity, attachment, transform] : view.each()) {
        auto& parent_skel = world.get<CSkeletalMesh>(attachment.parent_entity);
        glm::mat4 socket_world = parent_skel.socket_registry->get_world_transform(
            attachment.socket_index, parent_skel.bone_world_matrices);

        transform.position = glm::vec3(socket_world[3]);
        transform.rotation = glm::quat_cast(socket_world);
        if (attachment.inherit_scale) {
            transform.scale = extract_scale(socket_world);
        }
    }
}
```

---

## 6. Multipart Character System

### 6.1 Overview

Multipart characters allow modular composition of visual meshes on top of a shared
master skeleton. This enables equipment systems where swapping armor, helmets, or
accessories replaces mesh geometry without rebuilding the character.

### 6.2 Architecture

```
+------------------------------------------+
|         MultipartCharacter               |
|                                          |
|  Master Skeleton (humanoid.uzskel)       |
|  |                                       |
|  +-- MeshPart: "torso"                   |
|  |   mesh: res://meshes/torso_plate.uzmesh
|  |   bone_mapping: [pelvis, spine_01,    |
|  |     spine_02, spine_03, clavicle_l,   |
|  |     clavicle_r, neck]                 |
|  |   lod: [0, 1, 2]                      |
|  |                                       |
|  +-- MeshPart: "legs"                    |
|  |   mesh: res://meshes/legs_plate.uzmesh|
|  |   bone_mapping: [pelvis, thigh_l,     |
|  |     calf_l, foot_l, thigh_r, calf_r,  |
|  |     foot_r]                           |
|  |   lod: [0, 1]                         |
|  |                                       |
|  +-- MeshPart: "head"                    |
|  |   mesh: res://meshes/head_human.uzmesh|
|  |   bone_mapping: [neck, head,          |
|  |     jaw, eye_l, eye_r, ...]           |
|  |   lod: [0, 1, 2, 3]                  |
|  |                                       |
|  +-- MeshPart: "arms"                    |
|      mesh: res://meshes/arms_plate.uzmesh|
|      bone_mapping: [clavicle_l,          |
|        upperarm_l, lowerarm_l, hand_l,   |
|        clavicle_r, upperarm_r,           |
|        lowerarm_r, hand_r]              |
|      lod: [0, 1]                         |
+------------------------------------------+
```

### 6.3 C++ Classes

```cpp
struct MeshPart {
    std::string                 slot_name;      // "torso", "legs", "head", "arms"
    ResourceHandle<SkeletalMesh> mesh;          // the mesh asset
    std::vector<int32_t>        bone_mapping;   // indices into master skeleton
    int32_t                     current_lod;    // which LOD level to render
    bool                        visible;        // can be hidden (e.g., helmet off)
};

struct MultipartCharacter {
    ResourceHandle<Skeleton>    base_skeleton;
    std::vector<MeshPart>       parts;
    SocketRegistry              sockets;        // sockets from base skeleton

    // Add/replace a mesh part by slot name
    void set_part(std::string_view slot, ResourceHandle<SkeletalMesh> mesh);

    // Remove a mesh part (slot becomes empty)
    void clear_part(std::string_view slot);

    // Get bone palette for a specific part (subset of full palette)
    void compute_part_palette(uint32_t part_idx,
                              const std::vector<glm::mat4>& full_palette,
                              std::vector<glm::mat4>& part_palette_out) const;
};
```

### 6.4 Bone Mapping

Each mesh part references a subset of bones from the master skeleton. The
`bone_mapping` vector maps the part's local bone index to the master skeleton's
bone index:

```
Part "torso" local bone 0 = master bone 1 (pelvis)
Part "torso" local bone 1 = master bone 2 (spine_01)
Part "torso" local bone 2 = master bone 3 (spine_02)
...
```

This allows parts authored against different skeleton variants (as long as bone
names match) to share the same master skeleton at runtime.

### 6.5 Seam Blending

Where mesh parts meet (e.g., torso meets arms at the shoulder), vertices near the
seam must blend smoothly. This is handled by:

1. **Shared boundary bones**: both parts include the boundary bones (e.g., `clavicle_l`
   is in both "torso" and "arms" bone mappings)
2. **Vertex overlap zone**: mesh parts overlap by 1-2 cm at seams
3. **Alpha blending**: vertices in the overlap zone use vertex color alpha to blend
   between parts (0 = transparent at outer edge, 1 = opaque)

This is enforced at mesh authoring time. The engine does not auto-blend seams.

### 6.6 Runtime Equipment Swapping

```lua
-- Equip heavy armor torso
function M:equip_torso(ctx, armor_id)
    local mesh_path = "res://meshes/armor/" .. armor_id .. "_torso.uzmesh"
    self:set_mesh_part("torso", mesh_path)
end

-- Remove helmet (head slot becomes empty, shows underlying "head" base mesh)
function M:unequip_helmet(ctx)
    self:clear_mesh_part("helmet")
    self:set_mesh_part_visible("head", true)
end
```

### 6.7 Per-Part LOD

Each mesh part has independent LOD levels. The head (always visible, close to camera)
can maintain LOD0 while the boots use LOD2.

```cpp
// Per-frame LOD selection based on distance
void update_multipart_lod(MultipartCharacter& character,
                          const glm::vec3& camera_pos,
                          const glm::vec3& char_pos) {
    float dist = glm::distance(camera_pos, char_pos);
    for (auto& part : character.parts) {
        int max_lod = part.mesh->lod_count - 1;
        if (dist < 10.0f)       part.current_lod = 0;
        else if (dist < 25.0f)  part.current_lod = std::min(1, max_lod);
        else if (dist < 50.0f)  part.current_lod = std::min(2, max_lod);
        else                    part.current_lod = max_lod;
    }
}
```

### 6.8 ECS Component

```cpp
struct CMultipartCharacter {
    MultipartCharacter  character;
    bool                dirty;          // true when a part changed, triggers re-upload
};
```

---

## 7. Ragdoll Setup Editor

### 7.1 Overview

The Ragdoll Setup Editor configures physics bodies and joint constraints for
ragdoll simulation. It works directly with the `.uzphys` file format defined in
`skeletal-mesh.md` and integrates with Jolt Physics for validation and preview.

### 7.2 Layout

```
+--------------------------------------------------------------------+
| Ragdoll Editor: humanoid_ragdoll.uzphys                     [X]   |
+------------------+-----------------------------+-------------------+
| BODY LIST        |     3D VIEWPORT             | PROPERTIES        |
|                  |                             |                   |
| [+] Add Body     |                             | Body: pelvis      |
| [Auto-Generate]  |      ( )  head              |                   |
|                  |       |                     | -- Shape           |
| v pelvis    [C]  |   [=] | [=]  (arms)        | Shape: capsule     |
| v spine_01  [S]  |    |  []  |                 | Radius: 0.12      |
| v spine_02  [S]  |   [=] [] [=]               | Height: 0.30      |
| v spine_03  [S]  |    |  []  |                 | Offset: 0,0,0     |
| v neck      [S]  |   [=] [] [=]  (legs)       |                   |
|   head      [O]  |    |  []  |                 | -- Physics        |
| v clavicle_l[H]  |   ( )     ( )  (feet)      | Mass: 10.0 kg     |
|   upperarm_l[C]  |                             | Friction: 0.8     |
|   lowerarm_l[C]  |  Legend:                    | Restitution: 0.1  |
|   hand_l    [B]  |  [=] capsule  () sphere    | Lin Damping: 0.05 |
| v clavicle_r[H]  |  [] box                    | Ang Damping: 0.05 |
|   upperarm_r[C]  |                             |                   |
|   lowerarm_r[C]  |  [Simulate] [Reset Pose]   | -- Constraint      |
|   hand_r    [B]  |  [Export]                   | Parent: spine_01  |
| v thigh_l   [C]  |                             | Type: swing_twist  |
|   calf_l    [C]  |  Total mass: 72.0 kg       | Swing Y: 30 deg   |
|   foot_l    [B]  |  Bodies: 18                 | Swing Z: 30 deg   |
| v thigh_r   [C]  |  Constraints: 17            | Twist: -20 / +20  |
|   calf_r    [C]  |                             |                   |
|   foot_r    [B]  |                             | [Test Constraint]  |
+------------------+-----------------------------+-------------------+
```

Shape legend: `[C]` = capsule, `[S]` = swing_twist constraint, `[O]` = sphere,
`[B]` = box, `[H]` = hinge constraint.

### 7.3 Per-Bone Physics Body

Each body in the ragdoll corresponds to a bone:

```cpp
struct RagdollBody {
    std::string     name;               // usually matches bone name
    std::string     bone_name;          // which skeleton bone this body tracks
    ShapeType       shape;              // Capsule, Box, Sphere
    float           radius;             // capsule/sphere radius
    float           height;             // capsule height (0 for sphere/box)
    glm::vec3       half_extents;       // box only
    glm::vec3       offset_translation; // local offset from bone
    glm::quat       offset_rotation;    // local rotation offset
    float           mass;               // kg
    float           friction;           // 0.0 - 1.0
    float           restitution;        // 0.0 - 1.0
    float           linear_damping;     // velocity damping
    float           angular_damping;    // rotation damping
};

enum class ShapeType : uint8_t {
    Capsule = 0,
    Box     = 1,
    Sphere  = 2,
};
```

### 7.4 Joint Constraints

| Constraint Type | Use Case | Parameters |
|----------------|----------|------------|
| Swing-Twist | Shoulders, hips, spine | swing_angle_y, swing_angle_z, twist_min, twist_max |
| Hinge | Knees, elbows | axis (Vec3), min_angle, max_angle |
| Fixed | Bones that should not move relative to parent | (none) |
| Cone | Neck, wrists | cone_angle, twist_min, twist_max |

```cpp
struct RagdollConstraint {
    std::string         name;
    std::string         body_a;         // parent body
    std::string         body_b;         // child body
    ConstraintJointType type;
    glm::vec3           axis;           // hinge axis (local to body_a)
    float               swing_angle_y;  // swing-twist: half-cone Y (degrees)
    float               swing_angle_z;  // swing-twist: half-cone Z (degrees)
    float               twist_min;      // min twist (degrees)
    float               twist_max;      // max twist (degrees)
    float               cone_angle;     // cone: half-angle (degrees)
    float               min_angle;      // hinge: min angle (degrees)
    float               max_angle;      // hinge: max angle (degrees)
};

enum class ConstraintJointType : uint8_t {
    SwingTwist = 0,
    Hinge      = 1,
    Fixed      = 2,
    Cone       = 3,
};
```

### 7.5 Ragdoll Physics Body Diagram

```
                        ( )  head         sphere r=0.11, mass=3.0
                         |
                        [=]  neck         capsule r=0.05, h=0.08, mass=2.0
                         |
                        [=]  spine_03     capsule r=0.10, h=0.12, mass=5.0
                         |
                        [=]  spine_02     capsule r=0.11, h=0.15, mass=6.0
                         |
                        [=]  spine_01     capsule r=0.11, h=0.15, mass=6.0
                         |
              +---------[=]---------+     pelvis capsule r=0.12, h=0.30, mass=10.0
              |                     |
   [=]--[=]--[=]               [=]--[=]--[=]    arms (capsule each)
   hand  fore  upper           upper fore  hand
   2.0   3.0   4.0             4.0   3.0   2.0   (mass in kg)
              |                     |
             [=]                   [=]    thigh capsule r=0.07, h=0.44, mass=6.0
              |                     |
             [=]                   [=]    calf  capsule r=0.06, h=0.40, mass=4.0
              |                     |
             []                    []     foot  box 0.10x0.05x0.20, mass=1.5
                                                 Total: ~72.0 kg
```

### 7.6 Collision Group Assignment

Ragdoll bodies are assigned collision groups to prevent self-collision between
adjacent bodies while allowing collision with the world and other ragdolls:

| Group | Description | Collides With |
|-------|-------------|---------------|
| Ragdoll_Self | All bodies of this ragdoll | World, Other_Ragdoll, Props |
| Ragdoll_Adjacent | Adjacent body pairs (e.g., thigh+calf) | Nothing (filtered out) |
| World | Static/dynamic world geometry | Everything |
| Other_Ragdoll | Bodies from other characters | World, Ragdoll_Self |

Adjacent body filtering is automatic: bodies connected by a constraint do not
collide with each other.

### 7.7 Animation-Ragdoll Blending

Smooth transition between animation and ragdoll (e.g., death, stumble):

```cpp
struct CRagdollBlend {
    float   blend_weight;       // 0.0 = full animation, 1.0 = full ragdoll
    float   blend_speed;        // units per second for transition
    bool    active;             // is ragdoll simulation running?
};

void update_ragdoll_blend(entt::registry& world, float dt) {
    auto view = world.view<CRagdollBlend, CSkeletalMesh>();
    for (auto [entity, blend, skel] : view.each()) {
        if (blend.active && blend.blend_weight < 1.0f) {
            blend.blend_weight = std::min(1.0f, blend.blend_weight + blend.blend_speed * dt);
        }
        if (!blend.active && blend.blend_weight > 0.0f) {
            blend.blend_weight = std::max(0.0f, blend.blend_weight - blend.blend_speed * dt);
        }

        // Blend poses
        Pose anim_pose = skel.current_anim_pose;
        Pose ragdoll_pose = read_ragdoll_pose(entity);
        blend_poses(anim_pose, ragdoll_pose, blend.blend_weight, skel.final_pose);
    }
}
```

Lua API for ragdoll blending:

```lua
-- Activate ragdoll with smooth blend
self:activate_ragdoll(0.3)   -- blend_speed = 0.3 (reaches full ragdoll in ~3.3s)

-- Partial ragdoll (stumble effect, only upper body goes limp)
self:activate_ragdoll_partial("spine_02", 0.5) -- from spine_02 up, blend speed 0.5

-- Deactivate (blend back to animation)
self:deactivate_ragdoll(0.5)

-- Direct blend weight control
self:set_ragdoll_blend(0.7)  -- 70% ragdoll, 30% animation
```

### 7.8 Editor Validation

The "Simulate" button runs a short Jolt Physics simulation (2 seconds) to validate
the ragdoll:

1. Instantiate ragdoll in bind pose
2. Apply gravity
3. Render result frame-by-frame in viewport
4. Report issues:
   - Bodies penetrating each other at bind pose
   - Constraints that cause instability (too loose/tight)
   - Mass distribution warnings (e.g., head heavier than torso)
   - Missing bodies for important bones

### 7.9 Auto-Generate

The "Auto-Generate" button creates a default ragdoll from the skeleton:

1. For each bone with children: create a capsule body
2. Capsule is oriented along bone direction, radius = 10% of bone length
3. Mass is proportional to capsule volume (density = 1000 kg/m3, ~water)
4. Default constraints:
   - Spine joints: swing-twist (30 degrees swing, +-20 twist)
   - Limb mid-joints (elbow, knee): hinge
   - Limb root-joints (shoulder, hip): swing-twist
   - Fixed for terminal bones (fingers, toes) unless explicitly enabled

---

## 8. LOD Bone Reduction

### 8.1 Overview

At lower LOD levels, many bones (fingers, facial bones, twist bones) can be
collapsed or removed without visible quality loss. The LOD bone mask system defines
which bones are active at each LOD level.

### 8.2 LOD Bone Mask

```
LOD0 (0-10m):   67 bones  -- all bones active (full quality)
LOD1 (10-25m):  42 bones  -- fingers collapsed to hand, face simplified
LOD2 (25-50m):  24 bones  -- twist bones removed, single spine bone
LOD3 (50m+):    12 bones  -- minimal skeleton (root, pelvis, limb segments)
```

The mask is a per-LOD bitset indicating which bones are active:

```cpp
struct BoneLODMask {
    uint32_t    lod_count;                          // 1-4
    std::vector<std::vector<bool>> bone_active;     // [lod][bone_index]
    std::vector<int32_t> collapse_target;           // [bone_index] -> which bone
                                                    // to collapse into (-1 = root)
};
```

When a bone is inactive at a LOD level, its transform is replaced by its
`collapse_target` bone's transform. Vertices skinned to that bone will deform
as though attached to the collapse target.

### 8.3 Auto-Simplify Rules

The editor can auto-generate LOD bone masks using these rules:

| LOD | Action | Bones Removed |
|-----|--------|---------------|
| LOD1 | Collapse leaf bones | Fingers (20), toe details (10) |
| LOD2 | Collapse twist bones + facial | Twist bones (8), facial (12) |
| LOD3 | Collapse to major joints | Everything except root, pelvis, spine, limb segments |

```
Auto-simplify example for humanoid (67 bones):

LOD0: root pelvis spine_01 spine_02 spine_03 neck head jaw
      clavicle_l upperarm_l upperarm_twist_l lowerarm_l lowerarm_twist_l
      hand_l thumb_01_l thumb_02_l thumb_03_l index_01_l index_02_l index_03_l
      middle_01_l middle_02_l middle_03_l ring_01_l ring_02_l ring_03_l
      pinky_01_l pinky_02_l pinky_03_l
      (... right side mirrored)
      thigh_l thigh_twist_l calf_l calf_twist_l foot_l ball_l
      thigh_r thigh_twist_r calf_r calf_twist_r foot_r ball_r
      eye_l eye_r brow_l brow_r cheek_l cheek_r nose
      = 67 bones

LOD1: root pelvis spine_01 spine_02 spine_03 neck head jaw
      clavicle_l upperarm_l lowerarm_l hand_l
      clavicle_r upperarm_r lowerarm_r hand_r
      thigh_l calf_l foot_l
      thigh_r calf_r foot_r
      eye_l eye_r
      = 22 bones (fingers -> hand, twist -> parent, face -> head)

LOD2: root pelvis spine_02 neck head
      upperarm_l lowerarm_l hand_l
      upperarm_r lowerarm_r hand_r
      thigh_l calf_l foot_l
      thigh_r calf_r foot_r
      = 15 bones (clavicles -> spine, eyes -> head, jaw -> head)

LOD3: root pelvis spine_02 head
      upperarm_l hand_l
      upperarm_r hand_r
      thigh_l foot_l
      thigh_r foot_r
      = 11 bones (minimal)
```

### 8.4 LOD Preview

The viewport shows:

- Current LOD level selector (dropdown or slider)
- Bone count at selected LOD
- Triangle count at selected LOD (from mesh LOD)
- Active bones highlighted in green, collapsed bones in red (dimmed)
- Side-by-side comparison mode: LOD0 on left, selected LOD on right

### 8.5 LOD in `.uzskel`

```ini
[uzskeleton version=1 name="HumanoidSkeleton" bone_count=67]

; ... bone definitions ...
; ... socket definitions ...

[lod level=1 active_bones=22]
; Bones active at LOD1 (others collapse to their collapse_target)
active = root, pelvis, spine_01, spine_02, spine_03, neck, head, jaw
active = clavicle_l, upperarm_l, lowerarm_l, hand_l
active = clavicle_r, upperarm_r, lowerarm_r, hand_r
active = thigh_l, calf_l, foot_l
active = thigh_r, calf_r, foot_r
active = eye_l, eye_r

[lod level=2 active_bones=15]
active = root, pelvis, spine_02, neck, head
active = upperarm_l, lowerarm_l, hand_l
active = upperarm_r, lowerarm_r, hand_r
active = thigh_l, calf_l, foot_l
active = thigh_r, calf_r, foot_r

[lod level=3 active_bones=11]
active = root, pelvis, spine_02, head
active = upperarm_l, hand_l
active = upperarm_r, hand_r
active = thigh_l, foot_l
active = thigh_r, foot_r

[lod_collapse_targets]
; Format: bone_name = collapse_target_bone
; Bones not listed collapse to their parent
thumb_01_l  = hand_l
thumb_02_l  = hand_l
thumb_03_l  = hand_l
index_01_l  = hand_l
index_02_l  = hand_l
index_03_l  = hand_l
; ... (all finger bones -> hand)
upperarm_twist_l = upperarm_l
lowerarm_twist_l = lowerarm_l
thigh_twist_l    = thigh_l
calf_twist_l     = calf_l
; ... (all twist bones -> parent)
eye_l  = head
eye_r  = head
brow_l = head
brow_r = head
jaw    = head
; ... (all facial bones -> head)
```

### 8.6 GPU Skinning with LOD Masks

When rendering at a non-zero LOD level, the bone palette is modified:

```cpp
void compute_lod_bone_palette(const Pose& pose, const Skeleton& sk,
                              const BoneLODMask& lod_mask, int lod_level,
                              std::vector<glm::mat4>& palette_out) {
    // First: compute full world-space matrices
    std::vector<glm::mat4> world_mats(sk.bones.size());
    compute_world_matrices(pose, sk, world_mats);

    // Then: for inactive bones, redirect to collapse target
    for (int i = 0; i < sk.bones.size(); ++i) {
        if (lod_mask.bone_active[lod_level][i]) {
            palette_out[i] = world_mats[i] * sk.bones[i].inverse_bind_world;
        } else {
            int target = lod_mask.collapse_target[i];
            if (target < 0) target = 0; // fallback to root
            palette_out[i] = world_mats[target] * sk.bones[i].inverse_bind_world;
        }
    }
}
```

---

## 9. File Formats

### 9.1 `.uzskel` -- Skeleton Definition + Sockets + LOD

Complete example:

```ini
[uzskeleton version=1 name="HumanoidSkeleton" bone_count=67]

; ============================================================
; BONES
; ============================================================

[bone id=0 name=root parent=-1]
bind_translation = [0.0, 0.0, 0.0]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]
bind_scale       = [1.0, 1.0, 1.0]

[bone id=1 name=pelvis parent=0]
bind_translation = [0.0, 0.0, 95.0]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

[bone id=2 name=spine_01 parent=1]
bind_translation = [0.0, 0.0, 10.5]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

[bone id=3 name=spine_02 parent=2]
bind_translation = [0.0, 0.0, 12.5]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

[bone id=4 name=spine_03 parent=3]
bind_translation = [0.0, 0.0, 12.5]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

[bone id=5 name=neck parent=4]
bind_translation = [0.0, 0.0, 8.0]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

[bone id=6 name=head parent=5]
bind_translation = [0.0, 0.0, 6.0]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

; ... (bones 7-66)

; ============================================================
; SOCKETS
; ============================================================

[socket name=weapon_r bone=hand_r]
position = [0.02, 0.08, -0.01]
rotation = [0.0, 0.707, 0.0, 0.707]
scale    = [1.0, 1.0, 1.0]

[socket name=weapon_l bone=hand_l]
position = [-0.02, 0.08, 0.01]
rotation = [0.0, -0.707, 0.0, 0.707]
scale    = [1.0, 1.0, 1.0]

[socket name=shield_l bone=lowerarm_l]
position = [0.0, 0.15, 0.0]
rotation = [0.0, 0.0, 0.0, 1.0]
scale    = [1.0, 1.0, 1.0]

[socket name=weapon_back bone=spine_03]
position = [0.0, -0.15, 0.1]
rotation = [0.383, 0.0, 0.0, 0.924]
scale    = [1.0, 1.0, 1.0]

[socket name=fx_muzzle bone=hand_r]
position = [0.0, 0.5, 0.0]
rotation = [0.0, 0.0, 0.0, 1.0]
scale    = [1.0, 1.0, 1.0]

[socket name=fx_footstep_l bone=foot_l]
position = [0.0, -0.02, 0.05]
rotation = [0.0, 0.0, 0.0, 1.0]
scale    = [1.0, 1.0, 1.0]

[socket name=fx_footstep_r bone=foot_r]
position = [0.0, -0.02, 0.05]
rotation = [0.0, 0.0, 0.0, 1.0]
scale    = [1.0, 1.0, 1.0]

[socket name=cam_head bone=head]
position = [0.0, 0.05, 0.12]
rotation = [0.0, 0.0, 0.0, 1.0]
scale    = [1.0, 1.0, 1.0]

; ============================================================
; REFERENCE POSE (optional)
; ============================================================

[reference_pose type=t_pose]
; Override bind pose for retargeting reference
; Only bones that differ from bind need to be listed
clavicle_l.rotation  = [0.0, 0.0, -0.707, 0.707]
clavicle_r.rotation  = [0.0, 0.0,  0.707, 0.707]
upperarm_l.rotation  = [0.0, 0.0, -0.707, 0.707]
upperarm_r.rotation  = [0.0, 0.0,  0.707, 0.707]

; ============================================================
; LOD BONE MASKS
; ============================================================

[lod level=1 active_bones=22]
active = root, pelvis, spine_01, spine_02, spine_03, neck, head, jaw
active = clavicle_l, upperarm_l, lowerarm_l, hand_l
active = clavicle_r, upperarm_r, lowerarm_r, hand_r
active = thigh_l, calf_l, foot_l
active = thigh_r, calf_r, foot_r
active = eye_l, eye_r

[lod level=2 active_bones=15]
active = root, pelvis, spine_02, neck, head
active = upperarm_l, lowerarm_l, hand_l
active = upperarm_r, lowerarm_r, hand_r
active = thigh_l, calf_l, foot_l
active = thigh_r, calf_r, foot_r

[lod level=3 active_bones=11]
active = root, pelvis, spine_02, head
active = upperarm_l, hand_l
active = upperarm_r, hand_r
active = thigh_l, foot_l
active = thigh_r, foot_r

[lod_collapse_targets]
thumb_01_l  = hand_l
thumb_02_l  = hand_l
thumb_03_l  = hand_l
index_01_l  = hand_l
index_02_l  = hand_l
index_03_l  = hand_l
middle_01_l = hand_l
middle_02_l = hand_l
middle_03_l = hand_l
ring_01_l   = hand_l
ring_02_l   = hand_l
ring_03_l   = hand_l
pinky_01_l  = hand_l
pinky_02_l  = hand_l
pinky_03_l  = hand_l
; ... (right hand mirrored)
upperarm_twist_l = upperarm_l
lowerarm_twist_l = lowerarm_l
thigh_twist_l    = thigh_l
calf_twist_l     = calf_l
; ... (right side mirrored)
eye_l  = head
eye_r  = head
brow_l = head
brow_r = head
jaw    = head
nose   = head
cheek_l = head
cheek_r = head
```

### 9.2 `.uzrig` -- Control Rig Definition

```ini
[uzrig version=1 name="HumanoidRig"]
skeleton = res://skeletons/humanoid.uzskel

; ============================================================
; CONTROLS
; ============================================================

[control name=root_ctrl]
shape    = sphere
size     = 0.3
color    = #FFFF00
parent   = none
drives_bone = root

[control name=cog_ctrl]
shape    = sphere
size     = 0.25
color    = #FF8800
parent   = root_ctrl
drives_bone = pelvis

[control name=fk_spine_01]
shape    = circle
size     = 0.15
color    = #00FF88
parent   = cog_ctrl
drives_bone = spine_01

[control name=fk_spine_02]
shape    = circle
size     = 0.15
color    = #00FF88
parent   = fk_spine_01
drives_bone = spine_02

[control name=fk_spine_03]
shape    = circle
size     = 0.15
color    = #00FF88
parent   = fk_spine_02
drives_bone = spine_03

[control name=head_aim]
shape    = cross
size     = 0.2
color    = #FF00FF
parent   = root_ctrl
; does not drive a bone directly -- used as aim target

[control name=hand_l_ik]
shape    = box
size     = 0.1
color    = #FF4444
parent   = root_ctrl
drives_bone = hand_l

[control name=elbow_l_pv]
shape    = diamond
size     = 0.08
color    = #FF4444
parent   = root_ctrl
; pole vector for arm_l IK chain

[control name=foot_l_ik]
shape    = box
size     = 0.12
color    = #4444FF
parent   = root_ctrl
drives_bone = foot_l

[control name=knee_l_pv]
shape    = diamond
size     = 0.08
color    = #4444FF
parent   = root_ctrl

; ... (right side mirrored)

; ============================================================
; IK CHAINS
; ============================================================

[ik_chain name=arm_l]
bone_start = upperarm_l
bone_end   = hand_l
solver     = TwoBone
effector   = hand_l_ik
pole_vector = elbow_l_pv
default_blend = 1.0

[ik_chain name=arm_r]
bone_start = upperarm_r
bone_end   = hand_r
solver     = TwoBone
effector   = hand_r_ik
pole_vector = elbow_r_pv
default_blend = 1.0

[ik_chain name=leg_l]
bone_start = thigh_l
bone_end   = foot_l
solver     = TwoBone
effector   = foot_l_ik
pole_vector = knee_l_pv
default_blend = 1.0

[ik_chain name=leg_r]
bone_start = thigh_r
bone_end   = foot_r
solver     = TwoBone
effector   = foot_r_ik
pole_vector = knee_r_pv
default_blend = 1.0

[ik_chain name=spine]
bone_start = spine_01
bone_end   = spine_03
solver     = FABRIK
effector   = fk_spine_03
max_iterations = 10
tolerance  = 0.001
default_blend = 0.0

; ============================================================
; CONSTRAINTS
; ============================================================

[constraint name=head_aim_constraint]
type   = aim
source = head
target = head_aim
aim_axis = [0, 0, 1]
up_axis  = [0, 1, 0]
weight = 1.0

[constraint name=cog_parent]
type   = parent
source = cog_ctrl
target = root_ctrl
weight = 1.0
maintain_offset = true

; ============================================================
; SPACE SWITCHES
; ============================================================

[space_switch control=hand_l_ik]
available = world, local, cog
default   = world

[space_switch control=hand_r_ik]
available = world, local, cog
default   = world

[space_switch control=foot_l_ik]
available = world, local, cog
default   = world

[space_switch control=foot_r_ik]
available = world, local, cog
default   = world

[space_switch control=head_aim]
available = world, local, cog
default   = world
```

### 9.3 `.uzphys` -- Ragdoll Physics Asset (extended)

This extends the format defined in `skeletal-mesh.md` with additional fields
for the ragdoll editor:

```ini
[uzphysics version=2 name="HumanoidRagdoll"]
skeleton = res://skeletons/humanoid.uzskel
total_mass = 72.0

; ============================================================
; BODIES
; ============================================================

[body name=pelvis]
bone     = pelvis
shape    = capsule
radius   = 0.12
height   = 0.30
offset_translation = [0, 0, 0]
offset_rotation    = [0, 0, 0, 1]
mass     = 10.0
friction = 0.8
restitution = 0.1
linear_damping  = 0.05
angular_damping = 0.05

[body name=spine_01]
bone     = spine_01
shape    = capsule
radius   = 0.11
height   = 0.15
offset_translation = [0, 0, 0]
offset_rotation    = [0, 0, 0, 1]
mass     = 6.0
friction = 0.8
restitution = 0.1
linear_damping  = 0.05
angular_damping = 0.05

[body name=spine_02]
bone     = spine_02
shape    = capsule
radius   = 0.11
height   = 0.15
offset_translation = [0, 0, 0]
offset_rotation    = [0, 0, 0, 1]
mass     = 6.0
friction = 0.8
restitution = 0.1
linear_damping  = 0.05
angular_damping = 0.05

[body name=spine_03]
bone     = spine_03
shape    = capsule
radius   = 0.10
height   = 0.12
offset_translation = [0, 0, 0]
offset_rotation    = [0, 0, 0, 1]
mass     = 5.0
friction = 0.8
restitution = 0.1
linear_damping  = 0.05
angular_damping = 0.05

[body name=neck]
bone     = neck
shape    = capsule
radius   = 0.05
height   = 0.08
mass     = 2.0

[body name=head]
bone     = head
shape    = sphere
radius   = 0.11
mass     = 3.0

[body name=upperarm_l]
bone     = upperarm_l
shape    = capsule
radius   = 0.05
height   = 0.28
mass     = 4.0

[body name=lowerarm_l]
bone     = lowerarm_l
shape    = capsule
radius   = 0.04
height   = 0.26
mass     = 3.0

[body name=hand_l]
bone     = hand_l
shape    = box
half_extents = [0.04, 0.02, 0.08]
mass     = 2.0

; ... (right side mirrored)

[body name=thigh_l]
bone     = thigh_l
shape    = capsule
radius   = 0.07
height   = 0.44
mass     = 6.0

[body name=calf_l]
bone     = calf_l
shape    = capsule
radius   = 0.06
height   = 0.40
mass     = 4.0

[body name=foot_l]
bone     = foot_l
shape    = box
half_extents = [0.05, 0.025, 0.10]
mass     = 1.5

; ... (right side mirrored)

; ============================================================
; CONSTRAINTS
; ============================================================

[constraint name=spine_01_joint]
body_a       = pelvis
body_b       = spine_01
type         = swing_twist
swing_angle_y = 30.0
swing_angle_z = 30.0
twist_min    = -20.0
twist_max    =  20.0

[constraint name=spine_02_joint]
body_a       = spine_01
body_b       = spine_02
type         = swing_twist
swing_angle_y = 25.0
swing_angle_z = 25.0
twist_min    = -15.0
twist_max    =  15.0

[constraint name=spine_03_joint]
body_a       = spine_02
body_b       = spine_03
type         = swing_twist
swing_angle_y = 20.0
swing_angle_z = 20.0
twist_min    = -10.0
twist_max    =  10.0

[constraint name=neck_joint]
body_a       = spine_03
body_b       = neck
type         = cone
cone_angle   = 40.0
twist_min    = -45.0
twist_max    =  45.0

[constraint name=head_joint]
body_a       = neck
body_b       = head
type         = cone
cone_angle   = 30.0
twist_min    = -30.0
twist_max    =  30.0

[constraint name=shoulder_l]
body_a       = spine_03
body_b       = upperarm_l
type         = swing_twist
swing_angle_y = 90.0
swing_angle_z = 60.0
twist_min    = -80.0
twist_max    =  80.0

[constraint name=elbow_l]
body_a       = upperarm_l
body_b       = lowerarm_l
type         = hinge
axis         = [1, 0, 0]
min          = -130.0
max          =  10.0

[constraint name=wrist_l]
body_a       = lowerarm_l
body_b       = hand_l
type         = cone
cone_angle   = 60.0
twist_min    = -30.0
twist_max    =  30.0

; ... (right side mirrored)

[constraint name=hip_l]
body_a       = pelvis
body_b       = thigh_l
type         = swing_twist
swing_angle_y = 80.0
swing_angle_z = 40.0
twist_min    = -30.0
twist_max    =  30.0

[constraint name=knee_l]
body_a       = thigh_l
body_b       = calf_l
type         = hinge
axis         = [1, 0, 0]
min          = -130.0
max          =  10.0

[constraint name=ankle_l]
body_a       = calf_l
body_b       = foot_l
type         = cone
cone_angle   = 30.0
twist_min    = -15.0
twist_max    =  15.0

; ... (right side mirrored)

; ============================================================
; COLLISION GROUPS
; ============================================================

[collision_groups]
; Bodies in the same group do not collide with each other
group_torso = pelvis, spine_01, spine_02, spine_03
group_head  = neck, head
group_arm_l = upperarm_l, lowerarm_l, hand_l
group_arm_r = upperarm_r, lowerarm_r, hand_r
group_leg_l = thigh_l, calf_l, foot_l
group_leg_r = thigh_r, calf_r, foot_r
```

---

## 10. Implementation Notes

### 10.1 C++ Class Hierarchy

```
src/engine/skeleton/
  skeleton.h/cpp              -- Skeleton, BoneInfo (already exists per skeletal-mesh.md)
  skeleton_editor.h/cpp       -- SkeletonEditor (add/remove/reparent/merge operations)
  socket_registry.h/cpp       -- SocketRegistry, Socket
  bone_lod_mask.h/cpp         -- BoneLODMask, auto-simplify logic
  retarget_map.h/cpp          -- RetargetMap loader/evaluator

src/engine/rig/
  control_rig.h/cpp           -- ControlRig, Control, IKChain, RigConstraint
  control_rig_compiler.h/cpp  -- Compiles .uzrig text into optimized runtime struct
  control_rig_evaluator.h/cpp -- Per-frame evaluation: constraints -> IK -> blend
  ik_solvers.h/cpp            -- TwoBoneIK, FABRIK implementations

src/engine/physics/
  ragdoll_setup.h/cpp         -- RagdollBody, RagdollConstraint, auto-generate
  ragdoll_builder.h/cpp       -- Builds Jolt RagdollSettings from .uzphys

src/engine/ecs/
  components.h                -- CSocket, CSocketAttachment, CControlRig,
                                 CMultipartCharacter, CRagdollBlend, CBoneLODMask

src/engine/scripting/
  lua_rig_binding.h/cpp       -- Lua API for rig:get_control(), rig:set_blend()
  lua_socket_binding.h/cpp    -- Lua API for entity:attach_to_socket()

src/game/
  systems/
    socket_system.h/cpp       -- update_socket_attachments()
    rig_system.h/cpp          -- update_control_rigs()
    ragdoll_system.h/cpp      -- update_ragdoll_blend()
    lod_system.h/cpp          -- update_bone_lod()
    multipart_system.h/cpp    -- update_multipart_characters()
```

### 10.2 C++ Class Declarations

```cpp
// ---- skeleton_editor.h ----

class SkeletonEditor {
public:
    // Bone operations
    int32_t     add_bone(Skeleton& sk, int32_t parent, std::string_view name);
    void        remove_bone(Skeleton& sk, int32_t idx, bool delete_children);
    void        reparent_bone(Skeleton& sk, int32_t idx, int32_t new_parent);
    void        rename_bone(Skeleton& sk, int32_t idx, std::string_view name);
    void        set_bone_transform(Skeleton& sk, int32_t idx, const BoneTransform& t);

    // Skeleton merge
    MergeResult merge_skeletons(Skeleton& target, const Skeleton& source);

    // Retarget
    RetargetMap build_retarget_map(const Skeleton& source, const Skeleton& target);

    // Reference pose
    void        store_reference_pose(Skeleton& sk, const Pose& pose);
    void        reset_to_bind_pose(Skeleton& sk);
    void        apply_tpose(Skeleton& sk);
    void        apply_apose(Skeleton& sk);
};

struct MergeResult {
    int32_t                         bones_added;
    int32_t                         bones_unified;
    std::vector<std::pair<int,int>> remap; // source_idx -> merged_idx
};

// ---- control_rig.h ----

class ControlRig {
public:
    std::string                         name;
    ResourceHandle<Skeleton>            skeleton;
    std::vector<Control>                controls;
    std::vector<IKChain>                ik_chains;
    std::vector<RigConstraint>          constraints;
    std::vector<SpaceSwitch>            space_switches;

    const Control*  find_control(std::string_view name) const;
    const IKChain*  find_chain(std::string_view name) const;
    void            set_fk_ik_blend(std::string_view chain, float blend);
    float           get_fk_ik_blend(std::string_view chain) const;
    void            evaluate(const Pose& fk_input, Pose& output) const;
};

// ---- control_rig_compiler.h ----

class ControlRigCompiler {
public:
    // Parse .uzrig text file into runtime ControlRig
    ControlRig compile(std::string_view uzrig_text, const Skeleton& sk);

    // Serialize ControlRig back to .uzrig text
    std::string serialize(const ControlRig& rig);
};

// ---- socket_registry.h ----

class SocketRegistry {
public:
    std::vector<Socket>                       sockets;
    std::unordered_map<std::string, uint32_t> name_to_index;

    uint32_t    add_socket(const Socket& s);
    void        remove_socket(std::string_view name);
    const Socket* find(std::string_view name) const;
    glm::mat4   get_world_transform(uint32_t idx,
                    const std::vector<glm::mat4>& bone_world) const;
};
```

### 10.3 DX12 Debug Draw

The skeleton/rig/ragdoll editors use a shared DX12 debug draw pass:

```cpp
// Debug draw primitives submitted per-frame
struct DebugDrawBatch {
    std::vector<DebugLine>   lines;      // bone connections, IK chains
    std::vector<DebugSphere> spheres;    // joints, socket gizmos
    std::vector<DebugBox>    boxes;      // control shapes, ragdoll bodies
    std::vector<DebugCircle> circles;    // FK controls
    std::vector<DebugText>   labels;     // bone names, socket names
};

// Rendered after the main scene pass, before UI overlay
// Uses additive blending, no depth write (always visible)
// Pipeline: VS_DebugDraw + PS_DebugDraw (flat color, no lighting)
```

Color conventions:

| Element | Color | Hex |
|---------|-------|-----|
| Bone (unselected) | White | #FFFFFF |
| Bone (selected) | Yellow | #FFFF00 |
| Joint sphere | Light gray | #CCCCCC |
| IK effector control | Red | #FF4444 |
| FK rotation control | Green | #00FF88 |
| Pole vector | Orange | #FF8800 |
| Socket (Weapon) | Red | #FF0000 |
| Socket (Shield) | Blue | #0000FF |
| Socket (FX) | Green | #00FF00 |
| Socket (Camera) | Magenta | #FF00FF |
| Ragdoll body (wireframe) | Cyan | #00FFFF |
| Ragdoll constraint (arc) | Yellow | #FFFF00 |
| LOD active bone | Green | #00FF00 |
| LOD collapsed bone | Red (dim) | #FF000080 |

### 10.4 VSCode WebView Integration

Each editor panel runs as a VSCode WebView:

| Panel | WebView | Communication |
|-------|---------|---------------|
| Bone Hierarchy Tree | HTML tree control (VSCode TreeView API) | IPC: `skeleton.select_bone`, `skeleton.reparent` |
| Properties Inspector | HTML form (bound to selected item) | IPC: `skeleton.set_property` |
| Socket List | HTML table + form | IPC: `skeleton.add_socket`, `skeleton.set_socket` |
| Control Rig Tree | HTML tree control | IPC: `rig.select_control`, `rig.set_property` |
| Ragdoll Body List | HTML table + form | IPC: `ragdoll.select_body`, `ragdoll.set_property` |
| LOD Slider | HTML range input + stats display | IPC: `skeleton.set_lod_preview` |

### 10.5 IPC Commands

```
; Skeleton Editor
skeleton.get_bones        {}                          -> [{id, name, parent, ...}]
skeleton.select_bone      {id}                        -> {}
skeleton.add_bone         {parent_id, name}           -> {new_id}
skeleton.remove_bone      {id, delete_children}       -> {}
skeleton.reparent_bone    {id, new_parent_id}         -> {}
skeleton.set_bone_property {id, prop, value}          -> {}
skeleton.set_reference_pose {type: "tpose"|"apose"|"custom"} -> {}

; Socket Editor
skeleton.get_sockets      {}                          -> [{name, bone, pos, rot}]
skeleton.add_socket       {name, bone}                -> {}
skeleton.remove_socket    {name}                      -> {}
skeleton.set_socket       {name, bone, pos, rot, scl} -> {}

; Control Rig Editor
rig.get_controls          {}                          -> [{name, shape, ...}]
rig.select_control        {name}                      -> {}
rig.set_control_property  {name, prop, value}         -> {}
rig.create_ik_handle      {chain_end_bone}            -> {chain_name}
rig.set_fk_ik_blend       {chain, blend}              -> {}
rig.set_space             {control, space}            -> {}
rig.set_constraint_weight {name, weight}              -> {}

; Ragdoll Editor
ragdoll.get_bodies        {}                          -> [{name, bone, shape, ...}]
ragdoll.select_body       {name}                      -> {}
ragdoll.set_body_property {name, prop, value}         -> {}
ragdoll.auto_generate     {}                          -> {}
ragdoll.simulate          {duration_seconds}          -> {success, warnings[]}
ragdoll.set_constraint    {name, type, ...}           -> {}

; LOD Editor
skeleton.get_lod_info     {}                          -> {lod_count, per_lod_bone_count[]}
skeleton.set_lod_preview  {level}                     -> {}
skeleton.auto_simplify    {}                          -> {lod_masks}
skeleton.set_lod_mask     {level, active_bones[]}     -> {}
```

### 10.6 Integration with Jolt Physics

The ragdoll editor integrates with Jolt for real-time validation:

```cpp
// Validate ragdoll setup against Jolt constraints
struct RagdollValidation {
    bool        valid;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

RagdollValidation validate_ragdoll(const PhysicsAsset& asset,
                                   const Skeleton& sk,
                                   JPH::PhysicsSystem& physics) {
    RagdollValidation result;
    result.valid = true;

    // Check 1: all body bones exist in skeleton
    for (auto& body : asset.bodies) {
        if (sk.find_bone(body.bone_name) < 0) {
            result.errors.push_back("Body '" + body.name +
                "' references unknown bone '" + body.bone_name + "'");
            result.valid = false;
        }
    }

    // Check 2: constraint bodies exist
    for (auto& c : asset.constraints) {
        if (!asset.find_body(c.body_a)) {
            result.errors.push_back("Constraint '" + c.name +
                "' references unknown body_a '" + c.body_a + "'");
            result.valid = false;
        }
    }

    // Check 3: mass distribution
    float total_mass = 0;
    for (auto& body : asset.bodies) total_mass += body.mass;
    if (total_mass < 30.0f || total_mass > 200.0f) {
        result.warnings.push_back("Total mass " + std::to_string(total_mass) +
            " kg is outside typical humanoid range (30-200 kg)");
    }

    // Check 4: bind-pose penetration (requires Jolt)
    auto* ragdoll_settings = asset.build_jolt_ragdoll(sk);
    // ... test for initial penetrations ...

    return result;
}
```

---

## 11. Keyboard Shortcuts

| Shortcut | Context | Action |
|----------|---------|--------|
| `I` | Skeleton Editor, bone selected | Create IK handle on bone chain |
| `S` | Skeleton Editor, bone selected | Add socket at bone |
| `Delete` | Any editor, item selected | Delete selected item |
| `Ctrl+D` | Any editor, item selected | Duplicate selected item |
| `Ctrl+Z` | Any editor | Undo |
| `Ctrl+Shift+Z` | Any editor | Redo |
| `F` | 3D Viewport | Frame selection |
| `1` | 3D Viewport | Translate gizmo |
| `2` | 3D Viewport | Rotate gizmo |
| `3` | 3D Viewport | Scale gizmo |
| `M` | Skeleton Editor | Toggle mesh overlay |
| `B` | Skeleton Editor | Toggle bone display |
| `L` | Skeleton Editor | Toggle bone labels |
| `P` | Ragdoll Editor | Toggle physics body wireframes |
| `Space` | Ragdoll Editor | Start/stop ragdoll simulation preview |
| `Tab` | Control Rig Editor | Cycle FK/IK mode for selected limb |

---

## 12. Related Documentation

- [specs/skeletal-mesh.md](../specs/skeletal-mesh.md) -- Skeleton, SkinnedVertex, GPU skinning, morph targets
- [specs/animation-system.md](../specs/animation-system.md) -- AnimationClip, state machine, IK, root motion
- [specs/physics-system.md](../specs/physics-system.md) -- Jolt Physics integration, collision events
- [specs/editor-gizmos.md](../specs/editor-gizmos.md) -- Translate/rotate/scale gizmos, ray-cast selection
- [specs/scene-system.md](../specs/scene-system.md) -- Node tree, .uzscene format
- [specs/lua-scripting.md](../specs/lua-scripting.md) -- Lua VM, ctx API, hot-reload
