# Proposal: Documentation Fixes

## Why
Internal audit (2026-03-13) found 2 CRITICAL and 4 HIGH documentation issues. Key renderer
subsystems (passes, shadows) have NO documentation, docs reference deleted files, and AGENTS.md
has outdated paths. This makes it impossible for AI agents and new contributors to understand
the rendering pipeline.

## What Changes
- Add docs for undocumented modules: renderer passes, shadows, scene_lighting, vertex_layout (D-01)
- Update all docs referencing deleted forward_pbr_pass to use pbr_pass (D-02)
- Update engine_math.h docs to reflect split headers (D-03)
- Fix outdated paths in AGENTS.md (D-04)
- Complete docs/engine/12-rendering/ with shadow system and deferred pipeline (D-05)
- Complete camera docs in 14-camera/ (D-06)
- Add missing param/return docs to terse /// comments (D-07)

## Impact
- Affected specs: none
- Affected code: docs/**, AGENTS.md, runtime/engine/**/*.h (doc comments only)
- Breaking change: NO
- User benefit: Complete, accurate documentation for all engine systems
