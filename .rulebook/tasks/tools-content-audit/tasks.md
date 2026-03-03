# Tasks: tools-content-audit

## 1. ContentAuditor Core Class

- [ ] 1.1 Create `tools/content_audit/content_auditor.h` defining `ContentAuditor` class with `Audit(project_root) -> ContentAuditReport` public method
- [ ] 1.2 Create `tools/content_audit/content_auditor.cpp` implementing the main audit orchestration loop
- [ ] 1.3 Define `ContentAuditReport` struct with arrays: `unused_assets[]`, `duplicate_groups[]`, `vram_budget`, `missing_lods[]`
- [ ] 1.4 Define `UnusedAsset` struct: `{path, asset_type, disk_size_bytes}`
- [ ] 1.5 Define `DuplicateGroup` struct: `{xxhash64, paths[], disk_size_bytes, wasted_bytes}`

## 2. Dependency Graph Builder

- [ ] 2.1 Implement `.uzscene` file walker in `tools/content_audit/dependency_walker.cpp`: parse all scene files in `project_root`, extract all referenced asset paths
- [ ] 2.2 Implement `.uzmat` file walker: parse all material files, extract all referenced texture and shader asset paths
- [ ] 2.3 Build a ref-counted asset DAG: each node is an asset path, each edge is a "referenced by" relationship
- [ ] 2.4 Implement unused asset detection: collect all assets in the project file tree and identify those with zero in-edges in the DAG
- [ ] 2.5 Add support for transitive dependencies (e.g., `.uzmat` references `.uzscene` which is never referenced — both are unused)

## 3. Texture Fingerprinting

- [ ] 3.1 Add xxHash64 dependency (via vcpkg or header-only include) to `tools/content_audit/CMakeLists.txt`
- [ ] 3.2 Implement `TextureFingerprinter::Fingerprint(path) -> uint64_t` in `tools/content_audit/texture_fingerprinter.cpp`: read raw pixel data (not filename), compute xxHash64
- [ ] 3.3 Group all textures by fingerprint and collect groups with >1 member as `duplicate_groups`
- [ ] 3.4 Report wasted bytes: `(group.paths.size() - 1) * disk_size_bytes` for each duplicate group

## 4. VRAM Estimator

- [ ] 4.1 Implement `VramEstimator::EstimateTexture(path, format) -> size_bytes` in `tools/content_audit/vram_estimator.cpp`: compute `width × height × bytes_per_pixel` for the given BC format
- [ ] 4.2 Implement `VramEstimator::EstimateMesh(path) -> size_bytes`: compute `(vertex_count × vertex_stride + index_count × 2)` summed across all LODs
- [ ] 4.3 Aggregate VRAM estimates into per-category totals (albedo textures, normal maps, depth buffers, mesh vertex buffers, index buffers)
- [ ] 4.4 Compare per-category totals against budget thresholds from `game.project.toml` `[vram_budgets]` section

## 5. LOD Coverage Checker

- [ ] 5.1 Implement `.uzmesh` LOD count reader in `tools/content_audit/lod_checker.cpp`: open each `.uzmesh` binary and read the LOD table header
- [ ] 5.2 Flag all `.uzmesh` files with `lod_count == 1` as `missing_lods` entries
- [ ] 5.3 Compute projected overdraw cost for single-LOD meshes based on triangle count × estimated screen coverage

## 6. JSON Report Output

- [ ] 6.1 Implement `ContentAuditReport` JSON serialization to `content-audit-report.json` in project root
- [ ] 6.2 Include top-level metadata: `generated_at`, `project_root`, `total_assets_scanned`, `total_wasted_vram_mb`
- [ ] 6.3 Sort `unused_assets` by `disk_size_bytes` descending for prioritized cleanup view

## 7. CI Integration

- [ ] 7.1 Add `--audit-content` CLI flag to engine entry point
- [ ] 7.2 Add `--output <path>` flag: write JSON report to specified path
- [ ] 7.3 Add `--fail-if-unused-assets` flag: exit code 1 if `unused_assets` array is non-empty
- [ ] 7.4 Add `--fail-if-vram-exceeds <MB>` flag: exit code 1 if total VRAM estimate exceeds threshold
- [ ] 7.5 Write human-readable summary to stdout: total unused assets (count + MB), duplicate groups count, VRAM total vs. budget

## 8. VSCode Extension Panel

- [ ] 8.1 Create `vscode-extension/src/panels/ContentAuditPanel.ts` as a `WebviewPanel` that reads `content-audit-report.json`
- [ ] 8.2 Implement sortable table with columns: `asset_path`, `category`, `size_mb`, `status` (unused/duplicate/over-budget/missing-lods)
- [ ] 8.3 Add fix actions per row: "Delete Asset" (removes file and updates references), "Assign LOD" (opens LOD assignment dialog), "Swap Duplicate" (replaces all uses of one with another)
- [ ] 8.4 Register `uzengine.auditContent` VSCode command in `vscode-extension/src/commands/auditContent.ts` that triggers the audit and opens the panel

## 9. Testing (≥95% line + branch coverage)

- [ ] 9.1 Write `tests/tools/test_content_auditor.cpp`: create a synthetic project with known unused assets, duplicate textures, and single-LOD meshes; assert `ContentAuditReport` matches expected findings exactly
- [ ] 9.2 Write `tests/tools/test_dependency_walker.cpp`: parse synthetic `.uzscene` and `.uzmat` files; assert dependency graph edges match expected reference list
- [ ] 9.3 Write `tests/tools/test_texture_fingerprinter.cpp`: two PNG files with identical pixel data but different names; assert same fingerprint; two different PNGs; assert different fingerprints
- [ ] 9.4 Write `tests/tools/test_vram_estimator.cpp`: known-dimension textures and known-vertex-count meshes; assert VRAM estimate equals manually computed expected value
- [ ] 9.5 Write `tests/tools/test_lod_checker.cpp`: synthetic `.uzmesh` with one LOD and another with four; assert only the single-LOD mesh is flagged
- [ ] 9.6 Verify ≥95% line and branch coverage across all `tools/content_audit/` source files using OpenCppCoverage
