# Proposal: phase14_14.7.0-ue5-content-browser-grid

## Why

UE5's Content Browser is the reference asset-management panel —
a grid of thumbnail tiles with per-asset labels, type-coloured
tile borders (Blueprint purple, Material teal, Texture orange,
etc.), and a rename-in-place affordance. The phase-13 module
picker dialog is close but lists text-only tiles without
thumbnails. The user will need to pick VFX systems / sprite
textures / materials from an asset library at some point — a
reusable content-browser widget covers that.

## What Changes

1. New `VfxContentBrowserWidget` — grid layout of 96x96 px tiles
   with:
   - Thumbnail region: 96x96 square at the top (texture SRV fed
     by the host, nullable — falls back to a checkerboard).
   - Label strip: 24 px below the thumbnail, 10 px MSDF centred,
     ellipsis on overflow.
   - Type-coloured 2-px border on the tile top edge (colour from
     host-provided `VfxAssetKind::tintColor`).
   - Rounded corners `radius.xs=2`.
2. Hover state: `cardBgHover` fill behind the label strip.
3. Selected state: `cardBgSelected` fill behind the entire tile
   + `accentPrimary` 2-px border on all four edges.
4. Grid flow: left-to-right, top-to-bottom; `gap = 8 px` between
   tiles; auto-reflow on resize.
5. Context menu on right-click (integrates with existing
   `VfxContextMenuWidget`).
6. Drag-and-drop source: dragging a tile emits
   `onDragStart(assetId)` so callers can wire it to target drop
   sites (e.g. drop a texture onto a sprite-render module).

## Impact

- Affected specs: `specs/ue5-content-browser-grid/spec.md` (new).
- Affected code: `runtime/engine/vfx/editor/vfx_content_browser_widget.{h,cpp}` (new).
- Breaking change: NO — additive widget; nothing in phase 13
  references it.
- User benefit: the editor gains a proper asset-picker surface
  reusable across every future texture / system / material
  lookup.
