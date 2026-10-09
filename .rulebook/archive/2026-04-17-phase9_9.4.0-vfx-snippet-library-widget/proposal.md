# Proposal: phase9_9.4.0-vfx-snippet-library-widget

## Why
Phase 8.8.0 shipped the `VfxSnippetIndex` scanner + tag filter. Artists can't browse it — no widget renders the thumbnails or forwards drag events to the active system editor.

## What Changes
Tile view widget over `VfxSnippetIndex`: thumbnail + display name + tag chips. Filter toolbar on top (reuses 9.1.0 search bar). Drag source emits the asset path; drop target on the open VfxSystem adds the emitter via `VfxSystem::emitters.push_back`.

## Impact
- Affected specs: docs/engine/25-vfx/25-snippet-library.md
- Affected code: vscode extension / asset browser panel + VfxSystem editor
- Breaking change: NO
- User benefit: Drag-drop emitter reuse across systems.
- Estimated effort: L

## Source
UE5 Niagara Emitter Library tile view.
