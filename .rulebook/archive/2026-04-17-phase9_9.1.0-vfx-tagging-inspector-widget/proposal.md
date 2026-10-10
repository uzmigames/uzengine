# Proposal: phase9_9.1.0-vfx-tagging-inspector-widget

## Why
Phase 8.2.0 shipped the tag data model + query grammar + JSON round-trip, but authors have no way to edit tags through the editor — they have to hand-write the `"tags"` JSON. The widget closes the loop.

## What Changes
Add a "Tags" row to the VFX inspector overlay: editable chip list with add/remove, plus a search bar at the top of the asset browser that calls `VfxAssetTagMatches`. Reuses the existing `VfxAssetTagMatches` helper.

## Impact
- Affected specs: docs/engine/25-vfx/19-asset-tagging.md
- Affected code: runtime/engine/vfx/editor/vfx_editor_overlay.* + asset browser widget
- Breaking change: NO
- User benefit: Authors author tags without hand-editing JSON.
- Estimated effort: M

## Source
UE5 Content Browser tag filter + chip editor.
