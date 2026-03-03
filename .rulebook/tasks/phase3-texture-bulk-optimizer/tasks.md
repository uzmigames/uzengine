# Tasks: phase3-texture-bulk-optimizer

## 1. Format Detector

- [ ] 1.1 Create `src/asset/importer/format_detector.h` defining `FormatDetector` class with `Detect(filename) -> FormatDetectorResult` method
- [ ] 1.2 Create `src/asset/importer/format_detector.cpp` implementing filename pattern matching table
- [ ] 1.3 Define `FormatDetectorResult` struct: `{format: BC7|BC5|BC4|BC1, srgb: bool, source: "filename_pattern"|"fallback", confidence: float}`
- [ ] 1.4 Implement sRGB-true patterns: albedo, diffuse, basecolor, base_color, col, color, `_d` suffix, `_Albedo`, `_BaseColor`
- [ ] 1.5 Implement sRGB-false patterns: normal, nrm, nor, normalmap, bump, roughness, rough, rgh, metallic, metal, met, ao, ambient_occlusion, occlusion, cavity, height, displacement, disp, emissive, emit, glow, mask, opacity, alpha, packed, orm, rma
- [ ] 1.6 Map format by channel count: normal maps → BC5 (2-channel); single-channel (AO/roughness/metallic/height) → BC4; albedo with alpha → BC7; albedo without alpha → BC7 or BC1
- [ ] 1.7 Return fallback BC7 UNORM_SRGB with confidence 0.5 when no pattern matches

## 2. Alpha Analyzer

- [ ] 2.1 Create `src/asset/importer/alpha_analyzer.h` and `src/asset/importer/alpha_analyzer.cpp`
- [ ] 2.2 Implement `AlphaAnalyzer::Analyze(image_path) -> AlphaResult`: `{has_alpha: bool, min_alpha: uint8, max_alpha: uint8}`
- [ ] 2.3 Implement short-circuit: return `has_alpha: true` immediately on first sub-255 alpha pixel found (avoid full scan for common case)
- [ ] 2.4 Return `has_alpha: false` for images without an alpha channel (RGB-only PNG, JPEG)

## 3. Bulk Import Job

- [ ] 3.1 Create `src/asset/importer/bulk_import_job.h` defining `BulkImportJob` class
- [ ] 3.2 Create `src/asset/importer/bulk_import_job.cpp`: scan source folder for `*.png`, `*.jpg`, `*.tga`, `*.hdr`, `*.exr` files; create a processing queue
- [ ] 3.3 Dispatch compression tasks to the thread pool via `JobSystem::dispatch`; run BC7/BC5/BC4 compression in parallel across all CPU threads
- [ ] 3.4 Write `.import.json` sidecar and `.dds` output for each processed file
- [ ] 3.5 Report progress via shared memory segment polled by the IPC handler: `{processed, total, errors[], current_file}`
- [ ] 3.6 Support cancellation: check a `cancel_requested` atomic flag between each file processing

## 4. IPC Handlers

- [ ] 4.1 Add `asset.bulkImport { folder, settings? } -> { ok, job_id, queued_count }` IPC handler in `src/ipc/handlers/asset_handlers.cpp`
- [ ] 4.2 Add `asset.bulkStatus { job_id } -> { processed, total, errors[], eta_seconds, current_file }` IPC handler
- [ ] 4.3 Add `asset.bulkCancel { job_id } -> { ok }` IPC handler
- [ ] 4.4 Implement `eta_seconds` estimation: `(total - processed) / (processed / elapsed_seconds)` with floor of 1 second

## 5. Texture Importer Integration

- [ ] 5.1 Extend `src/asset/importer/texture_importer.cpp` to accept `batch_settings` override parameter
- [ ] 5.2 Add `srgb_source` field to output `.import.json` (see phase2-texture-srgb-autodetect for detection logic)
- [ ] 5.3 Add `has_alpha` analysis pass via `AlphaAnalyzer` and record result in `.import.json`
- [ ] 5.4 Downgrade BC7 to BC1 when `has_alpha: false` and `format: BC7` and texture is albedo (reduces GPU memory by 50%)

## 6. VSCode Bulk Import Panel

- [ ] 6.1 Create `src/vscode_ext/panels/bulk_texture_optimizer.ts` as a `WebviewPanel` registered under command `uzengine.bulkImportTextures`
- [ ] 6.2 Implement source folder selection dialog, format override options, and per-file preview table
- [ ] 6.3 Implement `asset.bulkImport` IPC call on submit; poll `asset.bulkStatus` every 500ms to update progress bar
- [ ] 6.4 Implement per-row format override dropdown and sRGB override checkbox in the preview table
- [ ] 6.5 Highlight rows in yellow where `FormatDetectorResult.confidence < 1.0` (format was guessed by fallback)
- [ ] 6.6 Add click-on-row action: preview the texture in the Texture Preview panel before committing

## 7. Testing (≥95% line + branch coverage)

- [ ] 7.1 Write `tests/core/test_format_detector.cpp`: test all Substance Painter standard suffixes (albedo, normal, roughness, metallic, ao, orm, emissive); assert correct `format` and `srgb` for each; test `texture001.png` returns fallback with `confidence: 0.5`
- [ ] 7.2 Write `tests/core/test_format_detector_quixel.cpp`: test `_Albedo`, `_Normal`, `_Roughness`, `_Cavity`, `_Displacement` Quixel Megascans suffixes; assert all detected correctly
- [ ] 7.3 Write `tests/core/test_alpha_analyzer.cpp`: fully-opaque 4-channel PNG (has_alpha=false); fully-transparent PNG (has_alpha=true, min_alpha=0); partially-transparent PNG (has_alpha=true)
- [ ] 7.4 Write `tests/core/test_bulk_import_job.cpp`: create temp folder with 5 test PNGs (albedo, normal, roughness, metallic, unknown); run `BulkImportJob`; verify all 5 produce correct `.dds` format and `.import.json` with `srgb_source` set correctly
- [ ] 7.5 Write `tests/core/test_bulk_import_cancel.cpp`: start bulk import of 100 files; cancel after 10 processed; assert job stops cleanly with `processed <= 15` (small buffer for in-flight tasks)
- [ ] 7.6 Verify ≥95% line and branch coverage across all `src/asset/importer/bulk_import_job.cpp`, `format_detector.cpp`, and `alpha_analyzer.cpp` using OpenCppCoverage
