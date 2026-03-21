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

## 5. Skeletal Mesh — Bones & Weights
- [ ] 5.1 Extend `uzmesh_format.h` with `VertexSkinnedPosNormUV` struct (position[3], normal[3], uv[2], bone_ids[4] as uint8, bone_weights[4] as uint8) — 40 bytes
- [ ] 5.2 Add `UzMeshHeader::flags` bit 0 = skinned mesh flag, add skeleton joint count field in reserved space
- [ ] 5.3 Extract skeleton from FBX: `ufbx_bone` hierarchy, joint names, parent indices, bind pose transforms
- [ ] 5.4 Compute inverse bind matrices from ufbx skin cluster bind transforms
- [ ] 5.5 Extract per-vertex bone weights from `ufbx_skin_deformer` — limit to 4 influences, normalize weights to sum=1.0, pack to uint8
- [ ] 5.6 Create `Skeleton` class (`runtime/engine/animation/skeleton.h/.cpp`) — joint hierarchy, bind pose, inverse bind matrices, `ComputeJointMatrices()`
- [ ] 5.7 Write skinned .uzmesh with `VertexSkinnedPosNormUV` layout
- [ ] 5.8 Verify: load FBX, print skeleton joint count and hierarchy to log

## 6. GPU Skinning
- [ ] 6.1 Create skinned vertex shader (`runtime/shaders/runtime/skinned_vs.hlsl`) — linear blend skinning with 4 bone influences, joint palette via StructuredBuffer or cbuffer (max 128 joints)
- [ ] 6.2 Create `SkeletalMeshRenderer` (`runtime/engine/renderer/skeletal_mesh_renderer.h/.cpp`) — owns joint palette GPU buffer, creates skinned PSO with skinned vertex input layout
- [ ] 6.3 Upload bind pose joint matrices (inverse_bind * identity = identity at rest) to GPU buffer
- [ ] 6.4 Render skinned mesh in bind pose using skinned vertex shader
- [ ] 6.5 Visual verification: screenshot shows MushroomMonster in bind pose (should look identical to static mesh)

## 7. Animation Sequence
- [ ] 7.1 Create `AnimSequence` class (`runtime/engine/animation/anim_sequence.h/.cpp`) — per-joint keyframes (time, position, rotation, scale), duration, sample rate
- [ ] 7.2 Parse FBX animation from separate animation FBX files using `ufbx_load_memory()` — extract `ufbx_anim_stack`, `ufbx_anim_layer`, per-bone animation curves
- [ ] 7.3 Implement `AnimSequence::Sample(float time)` — interpolate keyframes (lerp position/scale, slerp rotation), return array of local joint transforms
- [ ] 7.4 Compute final joint matrices: `world_matrix[i] = parent_world[i] * local_pose[i]`, then `final[i] = world_matrix[i] * inverse_bind[i]`
- [ ] 7.5 Upload animated joint matrices to GPU each frame
- [ ] 7.6 Load `Mushroom_Idle.FBX` animation file, play on MushroomMonster mesh
- [ ] 7.7 Visual verification: screenshot (or multiple frames) shows MushroomMonster playing idle animation

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
