# FBX Skeletal Animation Pipeline — Tasks

## 1. Dependency Setup
- [x] 1.1 Add ufbx (v0.14.3) to `scripts/setup.ps1` using Clone-Repo from GitHub (ufbx/ufbx, tag v0.14.3). Added verification entry.
- [x] 1.2 Downloaded ufbx to `runtime/thirdparty/ufbx/` — ufbx.c, ufbx.h, LICENSE only.
- [x] 1.3 Added ufbx.c compilation to `runtime/build.zig` (C11, single file) + include path.
- [x] 1.4 Runtime builds successfully with ufbx linked (exit code 0, runtime.lib 299MB).

## 2. FBX Mesh Import (Static — No Bones Yet)
- [x] 2.1 Create `fbx_importer.h/.cpp` implementing `IAssetImporter` for `.fbx` extension. Created both files in `runtime/engine/asset/importers/` following MeshImporter pattern.
- [x] 2.2 Parse FBX via `ufbx_load_memory()`, extract first mesh geometry: positions, normals, UVs, indices. Uses ufbx_triangulate_face() for n-gon triangulation, ufbx_get_vertex_vec3/vec2 for attribute extraction.
- [x] 2.3 Generate tangents via UV-space edge algorithm (same as glTF importer). Duplicated GenerateFlatNormals and GenerateTangentsForPosNormUV in anonymous namespace.
- [x] 2.4 Apply meshoptimizer vertex cache + fetch optimization. Uses meshopt_optimizeVertexCache + meshopt_optimizeVertexFetch + meshopt_optimizeVertexFetchRemap for tangent remapping.
- [x] 2.5 Write output as .uzmesh via `WriteUzMesh()` with `VertexPosNormUV` layout. Single LOD, per-material submeshes, uint32 indices.
- [x] 2.6 Register FbxImporter in `AssetImporterRegistry`. Added to uzcook.cpp alongside MeshImporter.
- [x] 2.7 Extract PBR material references from FBX (baseColor, normal, metallic-roughness texture paths). Reads material->pbr.base_color/normal_map/roughness/metalness, resolves texture paths via relative_filename/filename.

## 3. fbx_test Demo — Static Mesh
- [x] 3.1 Create `runtime/demos/fbx_test/build.zig` + `build.zig.zon` following pbr_test pattern. Deploys FBX, textures, cloud noise.
- [x] 3.2 Create main.cpp + fbx_test.h + fbx_test.cpp. Thin demo using `uz::LoadFbx()` runtime API.
- [x] 3.3 Load FBX via `uz::LoadFbx()` (runtime core function in mesh.h/.cpp). Auto-caches as .uzmesh.
- [ ] 3.4 Render static mesh with existing PBR pipeline (no bones, just geometry + UVs)
- [ ] 3.5 Visual verification: screenshot shows MushroomMonster geometry with correct UVs

## 4. Material & Textures
- [x] 4.1 Load BaseColor, Normal, OcclusionRoughnessMetallic PNGs from `assets/textures/` — paths set in demo.
- [x] 4.2 Create material matching UzEngine PBR material format (albedo, normal, ORM split). ORM texture assigned to roughness/metallic/ao paths.
- [ ] 4.3 Apply material to mesh — render with full PBR lighting
- [ ] 4.4 Visual verification: screenshot shows textured MushroomMonster with correct PBR

## 5. Asset Format Architecture (3 separate assets: .uzskel + .uzmesh + .uzanim)

### 5A. Skeleton Asset (.uzskel) — shared across meshes + animations
- [x] 5A.1 Create `runtime/engine/animation/skeleton.h` — Skeleton class with BoneInfo (name, parentIndex, bindTranslation/Rotation/Scale, inverseBindWorld). kMaxBones=256. FindBoneIndex, GetBone, GetRootBoneIndex, GetChildBoneIndices. Name-to-index hash map.
- [x] 5A.2 Create `runtime/engine/animation/skeleton.cpp` — ComputeInverseBindMatrices() walks hierarchy forward (topological order), computes world = local * parent, inverseBindWorld = inverse(world). BuildNameIndex() with duplicate name warning.
- [x] 5A.3 Create `runtime/engine/animation/skeleton_io.h/.cpp` — .uzskel binary format: magic 'UZSK', version 1, 16B header. Per-bone: nameLen(uint16) + name + parentIndex(int16) + TRS(3+4+3 floats). SaveSkeleton/LoadSkeleton (disk), WriteSkeleton/ReadSkeleton (memory). Auto-calls BuildNameIndex + ComputeInverseBindMatrices on load.
- [x] 5A.4 Create `runtime/engine/animation/bone_transform.h` — BoneTransform: Vec3 position, Quat rotation, Vec3 scale. Identity(), ToMatrix() via Mat4::TRS, Interpolate(a,b,t) with lerp/slerp, ComposeWith(parent).
- [x] 5A.5 Add `engine/animation` to build.zig include paths + addCppDir. Add UZ_DECLARE_LOG_CATEGORY(Animation) to log.h.

### 5B. Skinned Vertex Format — extend .uzmesh for bone data
- [x] 5B.1 Add `SkinnedVertex` to `runtime/engine/renderer/mesh/vertex.h` — 56 bytes: Vec3 position, Vec3 normal, Vec2 texcoord, Vec4 tangent, uint8_t boneIndices[4], uint8_t boneWeights[4]. static_assert(sizeof==56).
- [x] 5B.2 Extend `UzMeshHeader::flags` — kUzMeshFlag_Skinned (0x1). When set: vertexStride=56, reserved[0]=skeletonPathLength. Skeleton filepath string appended after index data.
- [x] 5B.3 Bump `kUzMeshVersion` to 2 — v2 reader accepts v1 (backward compat) + v2 with optional skinned flag. Updated ReadUzMesh/WriteUzMesh. Added skeletonPath to UzMeshFile.

### 5C. FBX Skeleton Extraction
- [x] 5C.1 Extract skeleton hierarchy from FBX — ExtractSkeleton() collects bone_nodes from skin->clusters, builds node-to-candidate map, resolves parents by walking bone_node->parent chain, topological sort by depth, extracts bind-pose local TRS via ufbx_matrix_to_transform + ComputeLocalFromWorldTRS (inverts ComposeWith). All in fbx_importer.cpp anonymous namespace.
- [x] 5C.2 Compute inverse bind matrices — Handled automatically by Skeleton::ComputeInverseBindMatrices() called at end of ExtractSkeleton(). Walks hierarchy forward computing world = local * parent, then inverts.
- [x] 5C.3 Extract per-vertex bone weights from ufbx_skin_deformer — PackBoneWeights() reads skin->vertices[vertexIdx].weight_begin/num_weights, takes top 4 (pre-sorted descending by ufbx), normalizes to uint8 sum=255, adjusts largest weight for rounding. Parallel arrays (allBoneIndices/allBoneWeights) remapped alongside tangents via meshopt remap table.
- [x] 5C.4 Write .uzskel file — WriteSkeleton() serializes skeleton as AssetImportSidecar with extension=".uzskel". Skeleton path derived from sourceName (.fbx -> .uzskel) and stored in UZMS header via WriteUzMesh(skeletonPath).
- [x] 5C.5 Detect static vs skinned — `mesh->skin_deformers.count > 0` triggers skinned path (SkinnedVertex 56B + UZMS format). Static meshes keep exact original UZMH format (Vertex 48B, unchanged behavior).
- [x] 5C.6 Verify: logs skeleton bone count, root name, max depth. Logs weight distribution (1/2/3/4-bone influence counts). Final import log includes stride, [SKINNED] tag.

## 6. GPU Skinning
- [ ] 6.1 Create skinned vertex shader (`runtime/shaders/runtime/skinned_vs.hlsl`) — linear blend skinning with 4 bone influences, joint palette via StructuredBuffer or cbuffer (max 128 joints)
- [ ] 6.2 Create `SkeletalMeshRenderer` (`runtime/engine/renderer/skeletal_mesh_renderer.h/.cpp`) — owns joint palette GPU buffer, creates skinned PSO with skinned vertex input layout
- [ ] 6.3 Upload bind pose joint matrices (inverse_bind * identity = identity at rest) to GPU buffer
- [ ] 6.4 Render skinned mesh in bind pose using skinned vertex shader
- [ ] 6.5 Visual verification: screenshot shows MushroomMonster in bind pose (should look identical to static mesh)

## 7. Animation Asset (.uzanim) — separate files, linked to skeleton by joint names

### 7A. Animation Clip Data Structure
- [ ] 7A.1 Create `runtime/engine/animation/animation_clip.h` — AnimationClip: name (string), duration (float), sample_rate (float), loop (bool), skeleton_name (string, for validation). Vector<BoneChannel> channels. BoneChannel: bone_name (string, matched by name not index), vector<Keyframe<Vec3>> pos_keys, vector<Keyframe<Quat>> rot_keys, vector<Keyframe<Vec3>> scl_keys. Keyframe<T>: time (float), value (T).
- [ ] 7A.2 Create `runtime/engine/animation/animation_clip.cpp` — SamplePose(time, Skeleton&) → Pose (vector<BoneTransform>). For each channel: find bone index by name in skeleton, find bracketing keyframes, interpolate (lerp pos/scale, slerp rotation). Looping: wrap time via fmod. Missing channels use bind pose.
- [ ] 7A.3 Create `runtime/engine/animation/pose.h` — Pose: vector<BoneTransform> local_transforms (indexed by bone). BlendPoses(a, b, alpha) → lerp/slerp each bone. Identity pose from Skeleton bind.

### 7B. .uzanim Binary Format
- [ ] 7B.1 Create `runtime/engine/animation/animation_io.h/.cpp` — .uzanim format: magic 'ZNAN' (4B), version (4B), duration (4B float), sample_rate (4B float), channel_count (4B), flags (4B, bit 0=loop) = 24B header. Per channel: bone_name_length (uint16), bone_name (chars), pos_key_count (uint16), rot_key_count (uint16), scl_key_count (uint16). Then keyframe arrays (time float + value). SaveAnimation(clip, path), LoadAnimation(path) → AnimationClip.

### 7C. FBX Animation Extraction
- [ ] 7C.1 Extract animation from FBX — Parse `ufbx_anim_stack` (first stack). For each bone in skeleton: evaluate `ufbx_anim_layer` animation curves at baked sample rate (30fps). Extract position/rotation/scale per sample. Store as BoneChannel with bone_name matching skeleton joint name.
- [ ] 7C.2 Support separate animation FBX files — Load animation-only FBX (no mesh geometry needed). Match bone names against existing .uzskel. Write .uzanim.
- [ ] 7C.3 Bake animation curves — ufbx provides raw curves; bake to uniform samples at sample_rate for runtime simplicity. Key reduction: remove keyframes where interpolation error < epsilon.

### 7D. Runtime Animation Playback
- [ ] 7D.1 Compute bone palette — For each bone: world_matrix[i] = parent_world * local_pose[i], then palette[i] = world_matrix[i] * inverse_bind[i]. Upload as StructuredBuffer<float4x4>.
- [ ] 7D.2 Upload animated joint matrices to GPU each frame via CBRingAllocator or staging buffer.
- [ ] 7D.3 Load Mushroom_Idle.FBX animation, play on MushroomMonster mesh.
- [ ] 7D.4 Visual verification: screenshot shows MushroomMonster playing idle animation

## 8. Animation Controller (Basic State Machine)
- [ ] 8.1 Create `AnimationController` class (`runtime/engine/animation/anim_controller.h/.cpp`) — states, transitions, parameters (float, bool, trigger)
- [ ] 8.2 Implement `AnimState` — name, AnimSequence reference, loop mode (Once/Loop), playback speed
- [ ] 8.3 Implement `AnimTransition` — conditions (parameter comparison), blend time, priority
- [ ] 8.4 Implement `AnimationController::Update(float dt)` — evaluate transitions, advance animation time, crossfade blend between states
- [ ] 8.5 Crossfade blending: during transition, lerp/slerp between outgoing and incoming animation joint transforms by blend weight
- [ ] 8.6 Wire AnimationController to SkeletalMeshRenderer — controller outputs joint matrices, renderer uploads them

## 9. Walk Animation Integration
- [ ] 9.1 Load `Mushroom_Walk.FBX` as a second AnimSequence
- [ ] 9.2 Set up AnimationController with 2 states: Idle (default, loop) and Walk (loop)
- [ ] 9.3 Add transition Idle→Walk (condition: `is_walking == true`, blend time 0.2s)
- [ ] 9.4 Add transition Walk→Idle (condition: `is_walking == false`, blend time 0.2s)
- [ ] 9.5 Demo triggers Walk via keyboard input (e.g., hold W key sets `is_walking = true`)
- [ ] 9.6 Visual verification: screenshot shows smooth transition between idle and walk animations

## 10. Final Integration & Polish
- [ ] 10.1 Camera orbit around character (reuse existing orbit camera from pbr_test or lua scripting)
- [ ] 10.2 Ground plane with shadow (reuse existing shadow mapping)
- [ ] 10.3 Verify full pipeline: FBX load → skeleton → textures → GPU skinning → animation → state machine → walk
- [ ] 10.4 Performance check: must maintain 60+ FPS with animated character
- [ ] 10.5 Clean up: no TODOs, no stubs, no placeholder code

## 11. Testing
- [ ] 11.1 Unit test: FbxImporter parses MushroomMonster_Low.fbx — correct vertex count, index count, submesh count
- [ ] 11.2 Unit test: Skeleton extracted from FBX — correct joint count, hierarchy (root has children, no cycles)
- [ ] 11.3 Unit test: Vertex weights normalized (sum of 4 weights per vertex == 255 as uint8)
- [ ] 11.4 Unit test: AnimSequence::Sample() — keyframe interpolation at t=0, t=mid, t=end returns valid transforms
- [ ] 11.5 Unit test: AnimationController state machine — transitions fire when conditions are met
- [ ] 11.6 Unit test: AnimationController crossfade — blend weight progresses from 0→1 over blend time
- [ ] 11.7 Integration test: Full pipeline load FBX → render skinned mesh → no crashes, no GPU errors
- [ ] 11.8 Visual test: bind pose matches static mesh render (no vertex explosion, no inverted normals)
- [ ] 11.9 Visual test: walk animation plays correctly (feet move, no bone artifacts, no T-pose flicker)
- [ ] 11.10 Verify 95%+ coverage on all new code

## 12. Documentation
- [ ] 12.1 Update `docs/engine/08-resource-system/02-importers.md` — add FbxImporter section
- [ ] 12.2 Update `docs/engine/19-animation/03-skeletal-mesh.md` — mark as implemented, update code examples
- [ ] 12.3 Update `docs/engine/19-animation/01-overview.md` — mark Phase 3 items as implemented
- [ ] 12.4 Add `docs/guides/fbx-import-guide.md` — how to import FBX characters into UzEngine

## 13. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 13.1 Update or create documentation covering the implementation
- [ ] 13.2 Write tests covering the new behavior
- [ ] 13.3 Run tests and confirm they pass
