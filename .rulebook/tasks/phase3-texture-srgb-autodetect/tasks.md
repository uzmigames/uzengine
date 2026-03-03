# Tasks: phase3-texture-srgb-autodetect

## 1. SRGBDetector Module

- [ ] 1.1 Create `src/asset/importer/srgb_detector.h` defining `SRGBDetector` class with `Detect(filename) -> SRGBResult` static method
- [ ] 1.2 Create `src/asset/importer/srgb_detector.cpp` implementing the detection algorithm
- [ ] 1.3 Define `SRGBResult` struct: `{srgb: bool, source: SRGBSource, confidence: float}` where `SRGBSource` enum = `FILENAME_PATTERN | FALLBACK`
- [ ] 1.4 Implement sRGB-true pattern list: `albedo`, `diffuse`, `basecolor`, `base_color`, `col`, `color`, `_d` suffix
- [ ] 1.5 Implement sRGB-false pattern list: `normal`, `nrm`, `nor`, `normalmap`, `bump`, `roughness`, `rough`, `rgh`, `metallic`, `metal`, `met`, `ao`, `ambient_occlusion`, `occlusion`, `cavity`, `height`, `displacement`, `disp`, `emissive`, `emit`, `glow`, `mask`, `opacity`, `alpha`, `packed`, `orm`, `rma`
- [ ] 1.6 Match case-insensitively on the base filename without extension
- [ ] 1.7 Return `{srgb: true, source: FALLBACK, confidence: 0.5}` when no pattern matches (conservative safe default)
- [ ] 1.8 Return `{source: FILENAME_PATTERN, confidence: 1.0}` for all pattern matches

## 2. Alpha Analyzer Module (Shared)

- [ ] 2.1 Create `src/asset/importer/alpha_analyzer.h` and `src/asset/importer/alpha_analyzer.cpp` (shared with `phase3-texture-bulk-optimizer`)
- [ ] 2.2 Implement `AlphaAnalyzer::Analyze(image_path) -> AlphaResult`: `{has_alpha: bool, min_alpha: uint8, max_alpha: uint8}`
- [ ] 2.3 Implement short-circuit early return on first sub-255 alpha pixel to avoid full scan
- [ ] 2.4 Handle RGB-only images (no alpha channel): return `{has_alpha: false, min_alpha: 255, max_alpha: 255}`

## 3. Texture Importer Integration

- [ ] 3.1 Integrate `SRGBDetector::Detect` and `AlphaAnalyzer::Analyze` into `src/asset/importer/texture_importer.cpp`
- [ ] 3.2 Implement priority order: (1) explicit `srgb` override in `.import.json`, (2) filename pattern detection, (3) alpha analysis heuristic, (4) fallback `srgb: true`
- [ ] 3.3 Write `srgb_source` string and `has_alpha` bool to the output `.import.json` sidecar file

## 4. `.import.json` Schema Extensions

- [ ] 4.1 Add `srgb: bool` field to `.import.json` schema
- [ ] 4.2 Add `srgb_source: string` field: `"filename_pattern" | "user_override" | "fallback"`
- [ ] 4.3 Add `has_alpha: bool` field
- [ ] 4.4 Add `has_alpha_source: string` field: `"analysis" | "user_override"`
- [ ] 4.5 Document all new fields in `docs/specs/assets/asset-pipeline.md` texture section

## 5. VSCode Texture Inspector Integration

- [ ] 5.1 Update `src/vscode_ext/panels/texture_inspector.ts` to show an `sRGB: YES [source: filename_pattern]` badge in the metadata strip
- [ ] 5.2 Show a yellow warning badge when `srgb_source == "fallback"` with tooltip: "sRGB was not auto-detected. Click to verify."
- [ ] 5.3 Implement click-to-confirm on the warning badge: opens a dialog to manually confirm or override the sRGB setting; sets `srgb_source: "user_override"` in `.import.json`

## 6. Linter Rule

- [ ] 6.1 Implement a VSCode extension linter that scans all `.import.json` files in the workspace
- [ ] 6.2 Emit a warning diagnostic for any texture with `srgb_source: "fallback"` and `srgb: true`: "sRGB defaulted to true — verify this is not a non-color texture (normal/roughness/metallic)"
- [ ] 6.3 Register the linter as a VSCode `DiagnosticsCollection` with provider identifier `uzengine-texture-srgb`

## 7. Documentation

- [ ] 7.1 Document the full Substance Painter suffix-to-sRGB mapping table in `docs/specs/engine/optimization-systems.md`
- [ ] 7.2 Document Quixel Megascans convention mapping (`_Albedo`, `_Normal`, `_Roughness`, `_Cavity`, `_Displacement`)
- [ ] 7.3 Document Unity convention mapping (identical to Substance Painter)
- [ ] 7.4 Add migration note: for existing projects re-importing textures, the new sRGB detection may change the sRGB flag — artists should audit `.import.json` files where `srgb_source` changed

## 8. Testing (≥95% line + branch coverage)

- [ ] 8.1 Write `tests/core/test_srgb_detector.cpp`: test all Substance Painter standard suffixes — `hero_BaseColor.png` → `srgb: true`; `hero_Normal.png` → `srgb: false`; `hero_Roughness.png` → `srgb: false`; `hero_Metallic.png` → `srgb: false`; `hero_AO.png` → `srgb: false`; `hero_ORM.png` → `srgb: false`; `hero_Emissive.png` → `srgb: false`
- [ ] 8.2 Write `tests/core/test_srgb_detector_quixel.cpp`: test `_Albedo` → `srgb: true`; `_Normal` → `srgb: false`; `_Roughness` → `srgb: false`; `_Cavity` → `srgb: false`; `_Displacement` → `srgb: false`; assert all return `source: FILENAME_PATTERN`
- [ ] 8.3 Write `tests/core/test_srgb_detector_ambiguous.cpp`: `texture001.png` → assert `source: FALLBACK` with `confidence: 0.5`; `photo_diffuse.png` → assert `srgb: true` pattern match
- [ ] 8.4 Write `tests/core/test_srgb_detector_case_insensitive.cpp`: `HERO_ROUGHNESS.PNG` → assert `srgb: false`; `HeroAlbedo.png` → assert `srgb: true`
- [ ] 8.5 Write `tests/core/test_alpha_analyzer_srgb.cpp`: verify `AlphaAnalyzer` correctly returns `has_alpha: false` for a 100×100 fully-opaque 4-channel PNG; `has_alpha: true` for a PNG with one transparent pixel
- [ ] 8.6 Write `tests/core/test_texture_importer_srgb_priority.cpp`: import a texture with explicit `srgb: false` in `.import.json` whose filename would be detected as `srgb: true`; assert explicit override takes priority; assert `srgb_source: "user_override"` in output
- [ ] 8.7 Verify ≥95% line and branch coverage across all `src/asset/importer/srgb_detector.cpp` and `src/asset/importer/alpha_analyzer.cpp` using OpenCppCoverage
