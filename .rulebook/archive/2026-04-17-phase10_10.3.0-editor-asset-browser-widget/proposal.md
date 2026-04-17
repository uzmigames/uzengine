# Proposal: phase10_10.3.0-editor-asset-browser-widget

## Why
Phase 9 tasks 9.1 (tag filter search bar), 9.4 (snippet library tile view), 9.7 (thumbnail display) and 9.2 (parent picker) all need an asset-browser widget that can list assets, filter them, and render thumbnails. The overlay has no such widget today — every task that needs it ships blocked.

## What Changes
New `VfxAssetBrowserWidget` component: scrollable grid of tiles, each with thumbnail + label + tag chips. Hookable data source (extensible to `.uzvfx`, `.uzvfxsys`, `.uzvfxsnap` etc). Filter toolbar at the top (reuses 10.1 text input). Selection + drag source via callbacks the caller attaches.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: new runtime/engine/vfx/editor/vfx_asset_browser_widget.*
- Breaking change: NO
- User benefit: Foundation for asset authoring / reuse flows.
- Estimated effort: L

## Source
UE5 Content Browser + Niagara Emitter Library tile view.
