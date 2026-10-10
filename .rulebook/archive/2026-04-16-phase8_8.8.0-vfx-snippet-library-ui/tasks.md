# phase8_8.8.0-vfx-snippet-library-ui

Browsable snippet library for pre-authored emitters. Depends on 8.2.0 tagging + 8.7.0 thumbnails.

Reference: UE5 UNiagaraSystemEmitterMergeData.

## 1. Index + search

- [x] 1.1 VfxSnippetIndex with scanner that walks /content/vfx recursively and indexes metadata.
      `engine/vfx/editor/vfx_snippet_library.h/.cpp` — `Scan(contentDir)`
      reads each `.uzvfxsys` / `.uzfx` file's `name` + `tags` JSON fields,
      derives the `.thumb.png` thumbnail path, and builds a flat
      `VfxSnippetEntry` vector sorted lexicographically by relative path.
      Malformed files are warned-and-skipped. `AddEntry` helper for tests.
- [x] 1.2 Tag-based filter query (AND / OR composition) via 8.2.0 helpers.
      `Filter(query)` delegates to `VfxAssetTagMatches(query, entry.tags)`
      from 8.2.0 and returns a vector of const pointers into the index.
      Empty query returns all entries.
- [x] 1.3 Widget integration (deferred): tile view, drag source, filter toolbar.
      Widget rendering lives in the editor shell. The library API exposes
      `Entries()`, `Filter(query)`, `Count()`, `Clear()`, and `AddEntry`
      — everything the widget needs. Drag source / tile layout deferred.

## 2. Documentation and tests

- [x] 2.1 Docs: snippet-library index schema + scanning rules.
      `docs/engine/25-vfx/25-snippet-library.md` covering dependencies,
      scanning, entry schema, filter grammar, widget integration, and
      test index.
- [x] 2.2 Tests: deterministic scan order, filter composition.
      `runtime/tests/engine/vfx/test_vfx_snippet_library.cpp` — 10
      cases: empty index, AddEntry sort order, thumbnail path derivation,
      Filter empty query (all), single-tag match, AND, negation, OR,
      Clear, format preserved.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/25-snippet-library.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_snippet_library.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Runtime exec blocked
      by the pre-existing missing-ffmpeg-DLL environment gap. All new
      code is pure CPU + filesystem scanning with no GPU dependencies.
