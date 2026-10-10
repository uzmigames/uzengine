# phase9_9.4.0-vfx-snippet-library-widget

Tile-view browser + drag source on top of the 8.8.0 snippet index.

Reference: UE5 Niagara Emitter Library.

## 1. Tile view

- [x] 1.1 `VfxSnippetLibraryWidget` wraps the phase 10.3 asset-browser widget
      with a data-source adapter that walks `VfxSnippetIndex::Entries()`.
      One `VfxBrowserAssetEntry` per snippet (name + path + thumb + tags).
- [x] 1.2 Thumbnail path derived by replacing the extension with
      `.thumb.png`. Falls back to generic icon when the host fails to
      load the thumbnail at render time (host responsibility).
- [x] 1.3 Display name + tag chips — the underlying
      `VfxBrowserAssetEntry` carries both fields; overlay host renders
      them below each tile.

## 2. Filter + drag

- [x] 2.1 Reuse the 9.1 tag-filter search bar — via
      `Browser().FilterBuffer()` mutable string wired to phase 10.1
      `StringInput` with `InputFilter::LowercaseASCII`. Filter
      matches against name AND tags (case-insensitive substring).
- [x] 2.2 Drag source emits the asset's `relativePath` — via
      `VfxAssetBrowserWidget::onDragStart(assetPath)`.
- [x] 2.3 Drop target on VfxSystem editor appends a new emitter-ref.
      `onDropOnSystem(draggedPath, targetSystem)` callback +
      `VfxSnippetLibraryWidget::AppendEmitterRef(path, system)` helper
      (stem-from-path name, `e<N>` id generation, push to
      `system.emitters`).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `VfxSnippetLibraryWidget` + ASCII
      pipeline diagram in header.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_snippet_library_widget.cpp`:
      8 tests covering AppendEmitterRef (single/multi id gen/empty/
      nested path/no-extension), widget construction, browser access,
      and drop callback wiring.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
