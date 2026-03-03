# Tasks: tools-asset-validator

## 1. AssetValidator Core Class

- [ ] 1.1 Create `tools/asset_validator/asset_validator.h` defining `AssetValidator` class with `Validate(asset_path, config) -> ValidationReport` public method
- [ ] 1.2 Create `tools/asset_validator/asset_validator.cpp` implementing the `AssetValidator` class with dispatch to geometry and texture sub-validators
- [ ] 1.3 Define `ValidationReport` struct: `{asset_path, passed, errors: [{rule, severity, message, value, threshold}]}`
- [ ] 1.4 Define `ValidationConfig` struct with `weld_tolerance` (default 0.0001), per-LOD face budgets, and per-role texture size limits
- [ ] 1.5 Implement JSON serialization for `ValidationReport` using the engine's existing JSON library

## 2. Geometry Validation Rules

- [ ] 2.1 Implement `DuplicateVertexRule::Check`: compare all vertex positions within `weld_tolerance`, report duplicate count and percentage
- [ ] 2.2 Implement `DegenerateTriangleRule::Check`: compute per-face area using cross product, flag faces with area < epsilon (1e-8), report percentage of total faces
- [ ] 2.3 Implement `VertexBudgetRule::Check`: compare triangle count against per-LOD tier thresholds (LOD0=100K, LOD1=33K, LOD2=11K) read from `ValidationConfig`
- [ ] 2.4 Implement `BoneCountRule::Check`: count bones per mesh and fail if count exceeds 256 (GPU skinning shader limit)
- [ ] 2.5 Define `LintRule` interface (pure virtual `Check(asset_path, config) -> vector<ValidationError>`) that all geometry and texture rules implement

## 3. Texture Validation Rules

- [ ] 3.1 Implement `PowerOfTwoDimensionRule::Check`: verify width and height are members of {64, 128, 256, 512, 1024, 2048, 4096, 8192}; report actual values on failure
- [ ] 3.2 Implement `NormalMapZChannelRule::Check`: sample blue channel mean; fail if mean < 64 (indicates inverted Z / wrong export coordinate frame)
- [ ] 3.3 Implement `TextureSizeBudgetRule::Check`: read file size, compare against configurable per-role limits (albedo/normal/roughness, default 8MB each)
- [ ] 3.4 Implement `ColorSpaceMismatchRule::Check`: detect PNG files with embedded sRGB color profile assigned to non-color material slots (normal/roughness/metallic)
- [ ] 3.5 Add PNG metadata reader utility to extract embedded ICC profile and color space header fields

## 4. Audio Validation Rules

- [ ] 4.1 Implement `AudioSampleRateRule::Check`: verify sample rate is XAudio2-compatible (44100 or 48000 Hz); report actual rate on failure
- [ ] 4.2 Implement `AudioBitDepthRule::Check`: verify bit depth is 16 or 24; flag 8-bit audio as warning
- [ ] 4.3 Implement `AudioDurationRule::Check`: compare duration against configurable maximum (default 300 seconds for music, 30 seconds for SFX)

## 5. IPC Integration

- [ ] 5.1 Add `tools.validate_asset(asset_path)` IPC handler in `engine/src/ipc/handlers/tools_handlers.cpp` returning serialized `ValidationReport` JSON
- [ ] 5.2 Register the IPC handler in the engine's IPC dispatch table
- [ ] 5.3 Integrate `AssetValidator::Validate` as a pre-import check in `engine/src/importer/gltf_importer.cpp` — call before import proceeds; abort on error-severity failures in strict mode

## 6. Import Pipeline Integration

- [ ] 6.1 Integrate validation call in `engine/src/importer/texture_importer.cpp` before texture conversion begins
- [ ] 6.2 Define `ValidationMode` enum: `STRICT` (block on errors), `WARN_ONLY` (log but proceed), `DISABLED`
- [ ] 6.3 Add `validation_mode` field to import settings with default `STRICT`
- [ ] 6.4 Implement "Import Anyway" override: when validation returns errors, prompt user for override confirmation and store override decision in `.import.json`

## 7. CI Mode

- [ ] 7.1 Add `--validate-assets <dir>` CLI argument to the engine entry point
- [ ] 7.2 Implement `--fail-on-error` flag: exit code 1 if any error-severity diagnostic is found, 0 if only warnings
- [ ] 7.3 Write plain-text validation report to stdout in CI mode (one line per error: `[ERROR] rule | asset_path | message`)

## 8. VSCode Extension Integration

- [ ] 8.1 Update `vscode-extension/src/panels/AssetBrowser.ts` to show a yellow warning badge on assets with validation warnings
- [ ] 8.2 Add import dialog validation report display: show `ValidationReport` before final import confirmation step
- [ ] 8.3 Register "Validate Asset" context menu action in the Asset Browser that calls `tools.validate_asset` IPC and shows results in Output panel

## 9. Testing (≥95% line + branch coverage)

- [ ] 9.1 Write `tests/tools/test_asset_validator_geometry.cpp`: synthetic mesh with duplicate vertices, degenerate triangles, oversized face count, >256 bones — assert each rule fires correctly
- [ ] 9.2 Write `tests/tools/test_asset_validator_texture.cpp`: synthetic PNG with non-power-of-2 dimensions, inverted Z normal map, oversized file, sRGB profile on roughness slot — assert each rule fires
- [ ] 9.3 Write `tests/tools/test_asset_validator_audio.cpp`: synthetic WAV at 22050 Hz, 8-bit, >300s — assert sample rate, bit depth, and duration rules fire
- [ ] 9.4 Write `tests/tools/test_validation_report_json.cpp`: assert `ValidationReport` JSON serialization round-trips correctly with all fields present
- [ ] 9.5 Write `tests/tools/test_asset_validator_clean_asset.cpp`: valid mesh and valid texture — assert `passed: true` and empty `errors` array
- [ ] 9.6 Verify ≥95% line and branch coverage across all `tools/asset_validator/` source files using OpenCppCoverage
