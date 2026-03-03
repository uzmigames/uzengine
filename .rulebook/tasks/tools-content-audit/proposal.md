# Proposal: tools-content-audit

## Why

Every game project accumulates asset debt silently. A texture that was once used in a prototype scene remains in the project tree, occupying disk space and inflating PAK file size. A 4K albedo texture gets assigned to a 32-pixel UI button because no one was watching. Two artists import the same hero armor mesh from different FBX sources, resulting in duplicate GPU data. These problems are invisible until the ship date approaches — by which time a VRAM budget overrun is a crisis rather than a warning.

Content auditing converts invisible technical debt into a prioritized work list. The `tools-content-audit` tool performs a full static analysis pass over every asset in a project: it builds a dependency graph from all .uzscene and .uzmat references, identifies which assets are never referenced, fingerprints textures to detect hash-match duplicates, computes projected VRAM usage per-texture and per-mesh, and reports meshes that lack LOD levels. The output is a structured JSON report and a VSCode panel with sortable columns, so a technical artist can fix the top 10 VRAM offenders in one session.

The tool runs as a VSCode command ("UzEngine: Audit Content") and optionally as a CI pipeline step via `uzengine --audit-content --fail-on-threshold`. This dual mode means content budgets can be enforced automatically before assets reach main branch, not discovered after they are already shipped.

## What Changes

- New `ContentAuditor` class in `tools/content_audit/` (C++23, standalone executable)
- Dependency graph builder: walks all `.uzscene` and `.uzmat` files, builds a ref-counted asset DAG
- Unreferenced asset detector: assets with zero in-edges in the DAG are flagged as unused
- Texture fingerprinting: xxHash64 over raw pixel data (not filename) to detect duplicate textures regardless of import path or name
- VRAM estimator: computes expected GPU memory per texture (width × height × bytes-per-pixel for given BC format) and per mesh (vertex buffer + index buffer per LOD), aggregated into per-category budgets
- LOD coverage checker: .uzmesh files are inspected for LOD count; meshes with only one LOD are flagged with projected overdraw cost
- JSON report format: `content-audit-report.json` with arrays for `unused_assets`, `duplicate_groups`, `vram_budget`, `missing_lods`
- VSCode panel `ContentAuditPanel.ts`: reads report JSON, renders sortable table with fix actions (delete asset, assign LOD, swap duplicate)
- CI integration: `uzengine --audit-content --output report.json --fail-if-unused-assets --fail-if-vram-exceeds 2048mb` exits non-zero on threshold breach
- Unit tests: synthetic project with known unused/duplicate/oversized assets, assert report matches expected findings

## Impact

- Affected specs: `docs/specs/asset-pipeline.md` (add audit step), `docs/specs/build-system.md` (CI integration)
- Affected code: `tools/content_audit/` (new), `vscode-extension/src/panels/ContentAuditPanel.ts` (new), `vscode-extension/src/commands/auditContent.ts` (new)
- Breaking change: NO (new tool, no existing systems modified)
- User benefit: Prevents VRAM overruns from reaching shipped builds; reduces PAK file size by removing dead assets; gives technical artists a prioritized, actionable asset health report without launching an external profiler
