## 1. ACL Library Integration
- [ ] 1.1 Add `acl` to `vcpkg.json` (or as a git submodule under `third_party/acl`) and configure it as a build-time-only dependency used exclusively in the import pipeline, not linked into the runtime engine binary
- [ ] 1.2 Update `CMakeLists.txt` to conditionally include the ACL headers for `tools/importer` targets and include the ACL runtime-only header-only decompressor for `src/assets/AnimAsset.cpp`
- [ ] 1.3 Verify ACL builds clean with zero warnings under `/W4` (MSVC) and `-Wall -Wextra` (Clang/GCC); add any required suppression pragmas scoped to the ACL include block

## 2. .uzanim Format Change
- [ ] 2.1 Update `src/assets/AnimAsset.h` to add `compressed_data` field (`std::vector<uint8_t>`) and `compression_error_mm` field (float, default 0.1), alongside a version bump in the file format header (`UZANIM_VERSION` constant)
- [ ] 2.2 Update `AnimAsset::load()` in `src/assets/AnimAsset.cpp` to detect the new format version and read the ACL binary blob into `compressed_data`, skipping the legacy raw keyframe array path
- [ ] 2.3 Implement `AnimAsset::sample_pose(float time_s, Pose& out)` to call `acl::decompress_pose()` on the stored ACL blob when `compressed_data` is non-empty, writing decompressed bone transforms into the existing pose buffer
- [ ] 2.4 Retain legacy uncompressed code path behind a `#ifdef UZ_ANIM_LEGACY_FORMAT` guard to allow offline validation comparisons; the guard is OFF by default in all shipped builds

## 3. AnimImporter Compression Pass
- [ ] 3.1 In `tools/importer/AnimImporter.cpp`, after sampling the source glTF/FBX animation curves into a raw `acl::track_array`, call `acl::compress_track_list()` with the configured error tolerance
- [ ] 3.2 Read `compression_error_mm` from the `.uzanim` metadata sidecar file (or default to 0.1 mm) and pass it as the ACL `error_threshold` in meters (`compression_error_mm * 0.001f`)
- [ ] 3.3 Log the following per-clip import statistics to the import log: original uncompressed size (bytes), compressed size (bytes), compression ratio (original/compressed), and max reconstruction error reported by ACL
- [ ] 3.4 Write the ACL binary blob into the `.uzanim` output file and set the format version field so the runtime loader detects it correctly
- [ ] 3.5 Reject import if ACL reports a reconstruction error exceeding `compression_error_mm * 2` and write an error to the import log rather than silently storing a lossy result beyond tolerance

## 4. Per-Animation Error Tolerance Config
- [ ] 4.1 Define the `compression_error_mm` field in the `.uzanim` INI metadata sidecar (e.g., `animation_name.uzanim.meta`) with a default of 0.1 and document valid range (0.01 to 10.0)
- [ ] 4.2 Update `tools/importer/AnimImporter.cpp` to parse the metadata sidecar alongside the source animation file and apply per-clip tolerance before calling ACL compress
- [ ] 4.3 Emit a build warning (not error) when `compression_error_mm > 1.0` suggesting the cinematic workflow justification for the loose tolerance

## 5. Backward Compatibility and Migration
- [ ] 5.1 Add a migration guide entry to `docs/specs/asset-pipeline.md` documenting that all `.uzanim` files must be re-imported after this change and that the old uncompressed format is not forward-compatible
- [ ] 5.2 Add a compile-time assert in `AnimAsset::load()` that fails with a clear message if the binary blob is empty after loading a v2+ format file, preventing silent fallback to an empty animation
- [ ] 5.3 Update the asset pipeline CI step to run a full reimport of the sample animation set and verify the import log reports compression ratios > 2.0x for all clips

## 6. Testing
- [ ] 6.1 Create `tests/assets/test_anim_asset_compression.cpp` with Google Test cases: loading a v2 `.uzanim` file populates `compressed_data`, `sample_pose()` returns bone transforms within `compression_error_mm` tolerance of the uncompressed reference, loading a v1 (legacy) file still works via the legacy path
- [ ] 6.2 Add tests for `AnimImporter` compression pass: importer produces a compressed blob, logged compression ratio is >= 2.0x for a 5-second 30fps 64-bone test clip, importer rejects a clip where ACL error exceeds 2x tolerance
- [ ] 6.3 Add a round-trip test: import a raw glTF animation, write to `.uzanim`, reload, sample at every 0.1s interval, assert all joint positions within 0.1mm of original sampled values
- [ ] 6.4 Add edge case tests: single-frame animation, zero-length animation, animation with only rotation tracks (no position/scale), animation with a single bone
- [ ] 6.5 Add a test verifying the per-clip metadata sidecar overrides the default `compression_error_mm` value during import
- [ ] 6.6 Verify line coverage >= 95% and branch coverage >= 95% across `AnimAsset.cpp` and `AnimImporter.cpp` new code paths using OpenCppCoverage

## 7. Documentation
- [ ] 7.1 Update `docs/specs/animation-system.md` with the decompression flow, `sample_pose()` behavior with ACL, and the compression tolerance field reference
- [ ] 7.2 Update `docs/specs/asset-pipeline.md` with the new `.uzanim` format version table, `compression_error_mm` metadata sidecar format, migration instructions, and a table of expected compression ratios by animation type
- [ ] 7.3 Update `docs/specs/skeletal-mesh.md` to note that animation memory footprint is now approximately 8x smaller and that pose sampling has ACL decompression overhead of ~0.5 microseconds per bone
