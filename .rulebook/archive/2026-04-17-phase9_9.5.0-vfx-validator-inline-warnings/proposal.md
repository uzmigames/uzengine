# Proposal: phase9_9.5.0-vfx-validator-inline-warnings

## Why
Phase 8.9.0 shipped the 8-rule validation engine + JUnit XML export but nothing runs the rules at author time or in CI. Without it the rules never fire.

## What Changes
- **Editor side:** run `VfxValidateAsset(ctx)` on save + every 500ms during edit; render errors/warnings as inline markers in the inspector and a problems-panel summary.
- **CI side:** new `uzvalidate` CLI wrapping `VfxValidateAsset`, scanning a content dir, writing JUnit XML to stdout/file.

## Impact
- Affected specs: docs/engine/25-vfx/26-validator-ci-rules.md
- Affected code: editor overlay + new `runtime/tools/uzvalidate/` binary
- Breaking change: NO
- User benefit: Broken VFX assets fail CI before landing in main.
- Estimated effort: M

## Source
UE5 Niagara Validator runner + GitHub Actions JUnit ingest.
