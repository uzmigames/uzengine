# Proposal: phase10_10.11.0-editor-save-pipeline-hook

## Why
Phase 9.5 (editor inline validator warnings) and 9.7 (thumbnail-on-save) both need an extension point on the VFX editor Save action. The save path today writes the JSON synchronously with no before/after hooks — there's nowhere to run validation or trigger a capture.

## What Changes
Add `IEditorSaveExtension` interface + `RegisterSaveExtension` on the VFX editor controller. Each extension implements `PreSave(asset) → bool` (can abort the save) and `PostSave(asset, outputPath)`. Validator + thumbnail both plug in here.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_editor_controller.* (or equivalent save dispatcher)
- Breaking change: NO
- User benefit: Downstream save-time pipelines (validation, thumbnails, auto-commit) share one extension path.
- Estimated effort: S

## Source
Standard save-hook pattern (VS Code onWillSave / onDidSave).
