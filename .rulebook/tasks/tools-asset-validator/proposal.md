# Proposal: tools-asset-validator

## Why

The glTF and FBX importers process whatever geometry data they receive from source files. A mesh exported from Blender with accidental duplicate vertices produces a GPU buffer with invisible extra geometry and incorrect normal computation. A texture exported at 1000×750 (non-power-of-2) will fail to generate mipmaps correctly on some GPU hardware and produce visible seams. A mesh with 2+ million triangles and no LODs will cause frame rate collapse when placed at close range in a scene. These quality issues are invisible during import — they surface as hard-to-debug visual artifacts or performance problems much later.

The `tools-asset-validator` runs before import begins and provides a concrete pass/fail result with actionable diagnostics. For geometry: it checks for duplicate vertices (within a configurable weld tolerance), degenerate triangles (zero-area faces), T-junctions in mesh topology, and face count relative to configured budget per LOD tier. For textures: it checks width and height are powers of 2, verifies that normal maps have a valid Z-channel range (negative Z values indicate export coordinate frame errors), checks file size against configurable limits, and detects non-sRGB source textures being imported as sRGB material slots. For audio: it checks sample rate compatibility with XAudio2, bit depth, and duration.

Pre-import validation is cheaper than post-import cleanup. Finding a 12M-polygon character mesh before it occupies 400MB of GPU memory is qualitatively different from discovering it after a level artist has already placed it in 30 scene files. The validator creates a quality gate at the earliest possible point — before the asset ever enters the project.

## What Changes

- `AssetValidator` class in `tools/asset_validator/` — runs as a pre-import check before GLTF/FBX/texture import pipelines proceed
- Geometry validation rules:
  - Duplicate vertex detection: compare positions within `weld_tolerance` (default 0.0001), report count
  - Degenerate triangle detection: area < epsilon; report percentage of total faces
  - Vertex/face budget check: configurable per LOD tier (LOD0 max 100K tris, LOD1 max 33K, LOD2 max 11K — defaults, project-overridable)
  - Bone count check: > 256 bones per mesh is unsupported by GPU skinning shader
- Texture validation rules:
  - Power-of-2 dimensions: error if width or height not in {64, 128, 256, 512, 1024, 2048, 4096, 8192}
  - Normal map Z-channel: blue channel mean < 64 indicates inverted Z (export error)
  - File size budget: configurable limit per texture role (albedo, normal, roughness — default 8MB each)
  - Color space mismatch: detects PNG files with embedded sRGB color profile assigned to non-color material slots
- `ValidationReport` JSON: `{asset_path, passed, errors: [{rule, severity, message, value, threshold}]}`
- VSCode integration: import dialog shows validation report before final import confirmation; "Import Anyway" overrides warnings but not errors
- Asset Browser indicator: assets with validation warnings show a yellow badge in the Asset Browser panel
- IPC handler `tools.validate_asset(asset_path)` returns `ValidationReport` for programmatic use
- CI mode: `uzengine --validate-assets project/source_assets/ --fail-on-error`
- Unit tests: synthetic mesh/texture files with each error condition; assert report matches expected diagnostics

## Impact

- Affected specs: `docs/specs/asset-pipeline.md` (add validation step before import), `docs/guides/asset-pipeline-guide.md` (document validation rules and thresholds)
- Affected code: `tools/asset_validator/asset_validator.cpp` (new), `engine/src/importer/gltf_importer.cpp` (call validator before import), `engine/src/importer/texture_importer.cpp` (call validator), `vscode-extension/src/panels/AssetBrowser.ts` (show validation badge)
- Breaking change: NO (existing imports continue; validator is additive, validation errors block import only in strict mode)
- User benefit: Catches broken meshes and non-conforming textures before they enter the project; prevents silent GPU corruption from degenerate geometry; reduces QA cycle length by rejecting bad assets at source before they are embedded in scenes
