# Proposal: phase3-animation-compression

## Why
Raw animation data is extremely large: a 5-second animation at 30 fps with 67 bones produces approximately 10,050 keyframes, each storing position (12 bytes), rotation (16 bytes), and scale (12 bytes), totaling roughly 400 KB per animation clip before any compression. A typical action game ships 500-2000 clips, so uncompressed animation data alone would consume 200 MB to 800 MB. ACL (Animation Compression Library) is an open-source library achieving 8:1 compression ratios with sub-millimeter reconstruction error, used in production by Fortnite, The Division 2, and Cyberpunk 2077.

## What Changes
- Integrate ACL library into the asset pipeline build via vcpkg or as a submodule, used only at import time
- Modify the `.uzanim` importer to run ACL compression after sampling the source glTF/FBX animation curves, storing the ACL binary blob in the `.uzanim` file instead of raw keyframe arrays
- Add runtime decompression via `acl::decompress_pose()` called inside `AnimAsset::sample_pose()` per frame, writing decompressed bone transforms into the existing pose buffer
- Add per-animation error tolerance config in the `.uzanim` metadata (`compression_error_mm`, default 0.1 mm) allowing cinematics to use tighter tolerances than gameplay clips
- Log compression ratio (original size / compressed size) and max reconstruction error to the import log for each animation asset processed

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/asset-pipeline.md`, `docs/specs/skeletal-mesh.md`
- Affected code: `src/assets/AnimAsset.cpp`, `tools/importer/AnimImporter.cpp`, `CMakeLists.txt` (ACL dependency)
- Breaking change: YES — existing `.uzanim` files must be re-imported; old uncompressed format is not forward-compatible
- User benefit: Animation data disk and memory footprint is reduced 8x, enabling significantly larger animation budgets without hitting memory limits, and reducing streaming load times for animation-heavy scenes
