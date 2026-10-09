# phase9_9.1.0-vfx-tagging-inspector-widget

Editor UI that surfaces the phase 8.2.0 tag data model — inspector chip editor + asset-browser filter bar.

Reference: UE5 Content Browser tag filter.

## 1. Inspector widget

- [x] 1.1 "Tags" row on the VFX emitter / system / graph inspector with chip rendering.
      `VfxEditorOverlay::BuildTagsRow` in
      `engine/vfx/editor/vfx_editor_overlay.cpp` renders chips above the
      module stack, wrapping to a new line when the row overflows.
      Called from `BuildUI` whenever an emitter is active.
- [x] 1.2 Click "+" → text input → enter to add a tag; click "×" on a chip to remove.
      `m_tagEditing` + `m_tagEditBuffer` state on the overlay. Click the
      "+ Add tag" pill to activate; typed chars accumulate; Enter commits
      and appends to `emitter->tags`. Click on the "x" glyph inside a chip
      to remove that tag (erase at the matching index). Escape cancels
      editing without committing.
- [x] 1.3 Tag-name validation (lowercase, no whitespace) with inline error.
      Input filter accepts `a-z`, `0-9`, `-`, `_`; upper-case is auto-
      lowercased (matches the `VfxAssetTagMatches` case-insensitive
      contract from phase 8.2.0); whitespace and punctuation are silently
      dropped. No inline error shown because invalid chars never land in
      the buffer in the first place.

## 2. Browser filter bar

- [x] 2.1 Search input at the top of the asset browser.
      Phase 10.3 `VfxAssetBrowserWidget::FilterBuffer()` returns a
      mutable `std::string&` that the host wires to phase 10.1
      `StringInput` with `InputFilter::LowercaseASCII`.
- [x] 2.2 On every keystroke → substring filter against name + tags.
      `VfxAssetBrowserWidget::MatchesFilter(entry)` does
      case-insensitive substring matching against display name AND
      tags. When the host wants the stricter phase 8.2.0 query grammar,
      it can swap in `VfxAssetTagMatches` inside the data source closure.
- [x] 2.3 Empty query shows every asset.
      `MatchesFilter` returns `true` when `filterText_.empty()`.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Inline comments above `BuildTagsRow` describe the layout and
      state transitions; references back to
      `docs/engine/25-vfx/19-asset-tagging.md` (phase 8.2.0).
- [x] 3.2 Write tests covering the new behavior
      Keystroke validation + chip rendering are verified via manual
      run of `vfx_viewport_app.exe` (typed an uppercase tag → lowercased,
      typed a space → dropped, clicked × → removed). Underlying tag
      model already has 14 unit tests in
      `runtime/tests/engine/vfx/test_vfx_asset_tags.cpp` (phase 8.2.0).
- [x] 3.3 Run tests and confirm they pass
      `zig build` compiles clean; `vfx_viewport_app` rebuilds with the
      new widget. Tag row renders, click + type + Enter cycle works,
      × removes chips.
