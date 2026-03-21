# FBX Skeletal Animation Pipeline — Spec Delta

## ADDED Requirements

### Requirement: FBX File Parsing via ufbx
The FbxImporter SHALL parse FBX binary files using ufbx v0.14+ (`ufbx_load_memory()`).
The importer MUST extract mesh geometry (positions, normals, UVs, indices), skeleton hierarchy (bones, parent indices, bind pose), skin weights (up to 4 influences per vertex), and material references.
The importer MUST handle both FBX 7.x (modern) and FBX 6.x (legacy Mixamo) formats.

#### Scenario: Load MushroomMonster Static Mesh
Given a valid FBX file containing a single skinned mesh (MushroomMonster_Low.fbx)
When FbxImporter::Import() is called with the file bytes
Then the result contains a valid .uzmesh binary with correct vertex count, index count, and UVs
And the result materials array contains texture path references for BaseColor, Normal, and ORM

#### Scenario: Invalid FBX File
Given a corrupted or empty byte buffer
When FbxImporter::Import() is called
Then result.success is false
And result.error contains a human-readable error message

#### Scenario: FBX With No Mesh
Given a valid FBX file containing only a skeleton (no mesh geometry)
When FbxImporter::Import() is called
Then result.success is false
And result.error indicates no mesh found

### Requirement: Skinned Vertex Format
The engine MUST support a skinned vertex layout (`VertexSkinnedPosNormUV`) containing:
position (float3), normal (float3), uv (float2), bone_ids (uint8[4]), bone_weights (uint8[4]).
Total stride MUST be 40 bytes. Bone weights MUST be normalized so their uint8 values sum to 255.

#### Scenario: Vertex Weight Normalization
Given an FBX mesh where a vertex has 3 bone influences with weights [0.5, 0.3, 0.2]
When bone weights are packed to uint8
Then bone_weights = [128, 76, 51, 0] (approximately, sum = 255)
And the fourth bone_id is 0 with weight 0

#### Scenario: More Than 4 Influences
Given an FBX vertex with 6 bone influences
When bone weights are extracted
Then only the 4 highest-weight influences are kept
And the remaining 2 are discarded
And the 4 kept weights are renormalized to sum to 255

### Requirement: Skeleton Extraction
The Skeleton class MUST store a flat array of joints with parent indices, bind pose transforms (position, rotation, scale), and inverse bind matrices.
Joint ordering MUST guarantee that parent joints appear before children (topological order).
The skeleton MUST be validated for no cycles and valid parent references.

#### Scenario: Extract Skeleton From FBX
Given an FBX file with a bone hierarchy (Root → Spine → Chest → Head)
When the skeleton is extracted
Then joints array has correct count matching FBX bone count
And each joint has valid parent_id (UINT32_MAX for root, valid index for children)
And inverse bind matrices are computed from ufbx skin cluster bind transforms

#### Scenario: Skeleton Validation Detects Cycle
Given a malformed skeleton where joint A parents to B and B parents to A
When Skeleton::Validate() is called
Then it returns false with an error message describing the cycle

### Requirement: GPU Skinning via Vertex Shader
The skinned vertex shader MUST perform linear blend skinning with up to 4 bone influences per vertex.
Joint matrices (max 128) MUST be uploaded as a GPU buffer (StructuredBuffer or cbuffer).
The final vertex position MUST be: `skinned_pos = sum(weight[i] * (joint_world[i] * inverse_bind[i]) * bind_pos)`.
Normals MUST be skinned and renormalized.

#### Scenario: Bind Pose Rendering
Given a skeleton with identity animation (bind pose)
When joint matrices are computed as world_matrix[i] * inverse_bind[i]
Then all final matrices are identity (or very close)
And the rendered mesh looks identical to the static (unskinned) mesh

#### Scenario: Single Joint Rotation
Given a skeleton where one joint is rotated 45 degrees
When the skinned mesh is rendered
Then vertices influenced by that joint are visibly deformed
And vertices with zero weight for that joint remain unchanged

### Requirement: Animation Sequence Playback
AnimSequence MUST store per-joint keyframes (time, position, rotation, scale).
AnimSequence::Sample(float time) MUST interpolate between keyframes: lerp for position/scale, slerp for rotation.
Time wrapping MUST support loop mode (time modulo duration).

#### Scenario: Sample At Exact Keyframe Time
Given an AnimSequence with keyframes at t=0.0 and t=1.0
When Sample(0.0) is called
Then the returned transforms exactly match the first keyframe values

#### Scenario: Sample Between Keyframes
Given keyframes at t=0.0 (pos=[0,0,0]) and t=1.0 (pos=[10,0,0])
When Sample(0.5) is called
Then the returned position is approximately [5,0,0] (linear interpolation)

#### Scenario: Looping Playback
Given an AnimSequence with duration 2.0 seconds and loop enabled
When time advances to 3.0 seconds
Then Sample() wraps to 1.0 seconds (3.0 mod 2.0)
And the animation plays seamlessly from the wrapped time

### Requirement: Animation Controller State Machine
AnimationController MUST support named states, each referencing an AnimSequence.
Transitions between states MUST be evaluated based on parameter conditions (float comparison, bool check, trigger).
During transitions, joint transforms MUST be crossfade-blended (lerp/slerp) over the specified blend time.

#### Scenario: Idle To Walk Transition
Given an AnimationController with Idle (default) and Walk states
And a transition Idle→Walk with condition "is_walking == true" and blend_time 0.2s
When SetBool("is_walking", true) is called
Then the controller transitions from Idle to Walk
And during the 0.2s transition, poses are blended between Idle and Walk

#### Scenario: No Matching Transition
Given the controller is in Idle state
And the only transition requires "speed > 5.0"
When speed parameter is 2.0
Then no transition occurs
And the Idle animation continues playing

#### Scenario: Trigger Parameter Auto-Reset
Given a transition condition using trigger parameter "jump"
When SetTrigger("jump") is called
Then the transition fires on the next Update()
And the trigger automatically resets to false after being consumed

### Requirement: FBX Animation Import From Separate Files
The importer MUST load animation data from standalone FBX files (no mesh, only skeleton + animation).
The animation MUST be matched to the skeleton by joint name.
Joints present in the animation but not in the skeleton MUST be silently ignored.

#### Scenario: Load Walk Animation
Given a separate FBX file (Mushroom_Walk.FBX) containing walk animation curves
When the animation is loaded and matched to the MushroomMonster skeleton
Then an AnimSequence is created with keyframes for all matching joints
And the duration matches the FBX animation stack duration

#### Scenario: Animation-Skeleton Name Mismatch
Given an animation FBX with bone names that partially differ from the skeleton
When animation is loaded
Then joints with matching names are animated
And joints with no matching animation data retain their bind pose
