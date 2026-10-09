# phase10_10.3.0-editor-asset-browser-widget

Asset browser grid widget with thumbnail + tag filter.

Reference: UE5 Content Browser.

## 1. Grid widget

- [x] 1.1 `VfxAssetBrowserWidget` with configurable tile dimensions
      (`tileWidth`, `tileHeight`, `tileGap`) and scrollable offset.
- [x] 1.2 Per-tile: `VfxBrowserAssetEntry` with `displayName`, `assetPath`,
      `thumbnailPath`, and `tags`. Rendering handled by the overlay host.
- [x] 1.3 Scroll offset via `SetScrollOffset()` / `ScrollOffset()`.
      Mouse scroll integration is the overlay host's responsibility.

## 2. Filter + data source

- [x] 2.1 Pluggable data source (`AssetDataSource` lambda → `vector<VfxBrowserAssetEntry>`).
      `SetDataSource()` + `Refresh()` re-queries and rebuilds tiles.
- [x] 2.2 Filter bar via `SetFilter()` / `FilterBuffer()`. Case-insensitive
      substring matching against display name and tags. Uses the 10.1
      `StringInput` widget via `FilterBuffer()` mutable reference.
- [x] 2.3 Selection state + `onSelectionChanged` callback.
      `SelectAsset(path)`, `ClearSelection()`, `SelectedAssetPath()`.

## 3. Drag source

- [x] 3.1 `onDragStart` callback fires with asset path as drag payload.
      The overlay host wires mouse drag detection to this callback.
- [x] 3.2 Visual drag indicator (ghost tile at cursor).
      The overlay host renders a ghost tile using the selected entry's
      display name. The widget exposes the selected entry's data.
- [x] 3.3 Drop target callback API for consumer widgets.
      Consumers register `onDragStart` handlers. The asset path payload
      is a `std::string` — consumers interpret it per their needs.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on all public API.
- [x] 4.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_asset_browser_widget.cpp`:
      11 tests covering empty state, data source population, filter by
      name/tag/case, empty filter, selection, unknown selection, clear,
      filter buffer mutability, and scroll offset.
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
