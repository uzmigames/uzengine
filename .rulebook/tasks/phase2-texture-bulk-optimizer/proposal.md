# Proposal: phase2-texture-bulk-optimizer

## Why

Third-party asset packs are the backbone of indie and mid-size game production. Quixel Megascans, ArtStation Marketplace, Unity Asset Store exports, and Substance Painter exports all produce uncompressed PNG textures at 4096x4096 resolution without mip chains. A typical environment pack with 50 materials, each with albedo + normal + ORM textures, delivers 150 uncompressed PNG files at ~67 MB each — over 10 GB of raw source texture data that must be compressed to ~1.5 GB of BC7/BC5/BC4 DDS before it can be usefully rendered in real time.

Importing these packs in Unreal Engine requires either triggering an automatic import (which uses slow ISPC texture compression with no visibility into settings) or running the command-line texture tool per-file. When a pack contains 200 textures, individual import is not practical. Artists frequently accept Unreal's default settings without auditing sRGB flags or format choices, accumulating subtle rendering errors across dozens of materials.

UzEngine must provide a **bulk texture import panel** in the VSCode extension that processes an entire folder in one operation. The panel shows a preview of how each file will be imported (format, sRGB, mip count, output size) before the operation runs, lets the artist override individual files, shows a real-time progress bar, and reports results. This workflow handles the "just received a new asset pack" scenario with one click rather than dozens of individual imports.

## What Changes

- **`src/vscode_ext/panels/bulk_texture_optimizer.ts`**: New VSCode panel. Registered as a command `uzengine.bulkImportTextures`. Shows source folder selection, format options, and per-file preview table. Submits a bulk import job via IPC and polls for progress.
- **`src/asset/importer/bulk_import_job.cpp`**: New file. Manages a batch import job: scans source folder for image files (`*.png`, `*.jpg`, `*.tga`, `*.hdr`, `*.exr`), creates a queue, dispatches compression tasks to the thread pool, writes `.import.json` sidecars and `.dds` outputs, reports progress via IPC callback.
- **`src/asset/importer/texture_importer.cpp`**: Extend to accept `batch_settings` override, add `srgb_source` field to output `.import.json` (see phase2-texture-srgb-autodetect for the detection logic). Add `has_alpha` analysis pass.
- **IPC handlers added**: `asset.bulkImport { folder, settings? } -> { ok, job_id, queued_count }`, `asset.bulkStatus { job_id } -> { processed, total, errors[], eta_seconds, current_file }`, `asset.bulkCancel { job_id } -> { ok }`.
- **`src/asset/importer/format_detector.cpp`**: New file. Implements the format heuristic table: filename pattern matching → (format, sRGB) pair. Patterns cover albedo/normal/roughness/metallic/ao/height/emissive/mask/packed/orm/skybox/ui conventions. Falls back to BC7 UNORM_SRGB.
- **`src/asset/importer/alpha_analyzer.cpp`**: New file. Reads source image, scans all alpha channel values, returns `has_alpha: bool`. Used to avoid storing alpha in BC7 when the texture has no transparency (can downgrade to BC1 or BC4 depending on channel count).
- **Thread pool integration**: Bulk import uses `JobSystem::dispatch` to run BC7/BC5/BC4 compression in parallel across all available CPU threads. A 16-core machine compresses 16 textures simultaneously.
- **Progress reporting**: Bulk job writes progress to a shared memory segment polled by the IPC handler. Panel WebView polls `asset.bulkStatus` every 500ms and updates the progress bar.
- **VSCode UI**: The preview table supports per-row format override (dropdown) and sRGB override (checkbox). Rows highlighted in yellow where format was guessed by fallback (no pattern matched). The artist can click any row to preview the texture in the Texture Preview panel before committing.
- **Documentation**: Add bulk import workflow to `docs/guides/asset-pipeline-guide.md`. Update `docs/specs/assets/asset-pipeline.md` section 9 (IPC handlers) with the three new handlers.
- **Tests**: Integration test: create a temp folder with 5 test PNGs (albedo, normal, roughness, metallic, unknown), run bulk import, verify all 5 produce correct `.dds` format, correct sRGB flags, and `.import.json` sidecars with `srgb_source` set correctly.

## Impact

- **Affected specs**: `docs/specs/assets/asset-pipeline.md`, `docs/specs/engine/optimization-systems.md`
- **Affected code**: `src/asset/importer/bulk_import_job.cpp` (new), `src/asset/importer/format_detector.cpp` (new), `src/asset/importer/alpha_analyzer.cpp` (new), `src/asset/importer/texture_importer.cpp`, `src/vscode_ext/panels/bulk_texture_optimizer.ts` (new)
- **Breaking change**: NO — purely additive
- **User benefit**: Entire asset pack of 150+ textures imported, compressed, and correctly configured in one VSCode panel operation. Eliminates the per-file import workflow that causes sRGB misconfiguration errors and wastes 30+ minutes on large packs.
