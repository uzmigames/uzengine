## 1. MirrorDataAsset
- [ ] 1.1 Create `src/animation/MirrorDataAsset.h` defining `MirrorDataAsset` struct with `std::vector<BonePair> pairs` where `BonePair` holds `left_bone_name` (string) and `right_bone_name` (string), and `skeleton_asset` (AssetHandle) identifying the target skeleton
- [ ] 1.2 Create `src/animation/MirrorDataAsset.cpp` implementing `MirrorDataAsset::load_from_file(const char* path)` parsing a `.uzmirror` INI file with `[mirror_data]` header and `[[pair]]` blocks containing `left` and `right` bone name fields
- [ ] 1.3 Implement `MirrorDataAsset::save_to_file(const char* path)` for editor export of mirror data assets
- [ ] 1.4 Implement `MirrorDataAsset::resolve_indices(const Skeleton& skeleton)` that converts bone name pairs to bone index pairs (`BonePairIndices`), logging a warning for any name not found in the skeleton; resolved index pairs are cached and used at runtime to avoid per-frame name lookups
- [ ] 1.5 Register `.uzmirror` extension in the asset registry (`src/assets/AssetRegistry.cpp`)

## 2. Mirror Transform Computation
- [ ] 2.1 Create `src/animation/MirrorPoseProcessor.h` declaring `MirrorPoseProcessor` with method `apply_mirror(Pose& pose, const MirrorDataAsset& mirror_data)`
- [ ] 2.2 Implement the mirror transform algorithm in `MirrorPoseProcessor.cpp`: for each bone in the pose, negate the X-axis component of its local translation (mirrors across the YZ plane)
- [ ] 2.3 For paired bones (those listed in `MirrorDataAsset::pairs`), additionally negate the Y and Z components of the rotation quaternion (equivalent to flipping the rotation axis across the mirror plane), then swap the full transform between the left and right bone of each pair
- [ ] 2.4 Bones not listed in any pair still receive the X-translation negation; bones listed in pairs receive both the negation and the swap

## 3. AnimationPlayer API
- [ ] 3.1 Add `AnimationPlayer::play_mirrored(AnimHandle anim_handle, AssetHandle mirror_data_handle)` to `src/animation/` that sets a `is_mirrored` flag and `mirror_data_handle` on the active clip slot
- [ ] 3.2 In the blend tree node config for the state machine, add a `mirrored` (bool, default false) field so individual states can be tagged as mirrored within the blend graph
- [ ] 3.3 Add `AnimationPlayer::stop_mirrored()` that clears the mirrored mode and resets the clip to non-mirrored playback

## 4. AnimatorSystem Post-Process Integration
- [ ] 4.1 In `AnimatorSystem::update()` (`src/animation/AnimatorSystem.cpp`), run `MirrorPoseProcessor::apply_mirror()` as a post-process pass after blend tree evaluation and before all IK passes (foot IK, aim offset, jiggle), so IK operates on already-mirrored geometry
- [ ] 4.2 Skip the mirror post-process when neither the active clip's `is_mirrored` flag nor the state node's `mirrored` flag is set, to keep the common non-mirrored path free of overhead

## 5. Blend Tree Mixed Mirroring
- [ ] 5.1 Allow blend tree nodes to mix mirrored and non-mirrored clip states within the same blend graph by tracking `is_mirrored` per clip slot independently; the final blended pose is assembled from individually-mirrored slots before cross-blend
- [ ] 5.2 When blending a mirrored clip with a non-mirrored clip, apply the mirror transform to the mirrored clip's pose before blending so both poses are in the same orientation space

## 6. Asset Pipeline Integration
- [ ] 6.1 In the asset pipeline editor (`src/asset_pipeline/`), add a "Mirror Data" asset type to the asset browser and a creation wizard that auto-populates bone pairs by detecting common naming conventions (Bone_L / Bone_R, Left_Bone / Right_Bone, _l / _r suffix patterns)
- [ ] 6.2 Log a warning during asset pipeline import if the skeleton has an odd number of uniquely-named bones that cannot be auto-paired, prompting the user to manually verify the pairing

## 7. Testing
- [ ] 7.1 Create `tests/animation/test_mirror_data_asset.cpp` with Google Test cases: load valid `.uzmirror`, reject missing `left` or `right` in a pair, round-trip save/load preserves all pairs, `resolve_indices()` with unknown bone name logs warning and skips that pair
- [ ] 7.2 Create `tests/animation/test_mirror_pose_processor.cpp` covering: X-translation of all bones is negated, paired bones have their transforms swapped, unpaired bones do not have their transforms swapped, applying mirror twice returns the original pose (double-mirror identity)
- [ ] 7.3 Add blend tree integration tests: mirrored clip state produces a left-punch pose when given a right-punch animation source, non-mirrored clip state is unchanged, mixing mirrored and non-mirrored states produces correctly oriented blend result
- [ ] 7.4 Add post-process order tests: IK resolves correctly after mirror is applied (IK target positions are on the correct side)
- [ ] 7.5 Add `AnimationPlayer::play_mirrored()` API tests: calling play_mirrored sets `is_mirrored` flag, calling `stop_mirrored()` clears it, mirror data handle is stored correctly
- [ ] 7.6 Verify line coverage >= 95% and branch coverage >= 95% across `MirrorDataAsset.cpp` and `MirrorPoseProcessor.cpp`

## 8. Documentation
- [ ] 8.1 Update `docs/specs/animation-system.md` with `MirrorDataAsset` format, `play_mirrored()` API, mirror transform algorithm (X-negate + pair-swap), post-process pass order relative to IK
- [ ] 8.2 Update `docs/specs/skeletal-mesh.md` with a note on which animations benefit from runtime mirroring and the authoring convention for bone naming to enable auto-pairing
