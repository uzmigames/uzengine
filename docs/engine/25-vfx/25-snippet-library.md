# VFX snippet library

Browsable index of pre-authored VFX emitter assets. Artists discover + reuse
effects by tag search and thumbnail preview instead of filesystem diving.

Reference: UE5 `UNiagaraSystemEmitterMergeData` + Emitter Library UI.

## Dependencies

- **8.2.0 tagging** — each entry carries the asset's tag vector; `Filter`
  delegates to `VfxAssetTagMatches`.
- **8.7.0 thumbnails** — each entry derives a `.thumb.png` path via
  `VfxThumbnailPathForAsset`.

## Scanning

`VfxSnippetIndex::Scan(contentDir)` walks the content directory recursively,
reads the top-level `name` and `tags` JSON fields from each `.uzvfxsys` /
`.uzfx` file, derives the thumbnail path, and builds a flat
`VfxSnippetEntry` vector sorted lexicographically by `relativePath`. The
sort ensures a deterministic view regardless of filesystem enumeration order.

Malformed files are skipped with a `VFX/Warning`; the scan completes for
all remaining files.

## Entry schema

```cpp
struct VfxSnippetEntry {
    std::string              relativePath;   // unique key
    std::string              displayName;    // from JSON "name" or stem
    std::vector<std::string> tags;           // from JSON "tags"
    std::string              thumbnailPath;  // derived .thumb.png
    std::string              format;         // "uzvfxsys" or "uzfx"
};
```

## Filter

```cpp
auto results = index.Filter("fire -ambient OR magic");
// Returns vector<const VfxSnippetEntry*> — stable pointers into the index.
```

Delegates to `VfxAssetTagMatches(query, entry.tags)` per entry (phase 8.2.0
grammar: AND default, `OR` alternatives, `-`/`!` negation, case-insensitive).

## Widget integration (deferred)

The widget renders a tile view of filtered entries, showing thumbnail +
display name. Clicking an entry emits a drag source that the VFX system
editor accepts as a new emitter reference. Layout, input, and rendering
live in the editor shell — the library API exposes everything the widget
needs:

- `Entries()` — full sorted list.
- `Filter(query)` — filtered pointers.
- `Count()` — total indexed.
- `AddEntry(entry)` — test helper (also usable for programmatic injection).

## Tests

`runtime/tests/engine/vfx/test_vfx_snippet_library.cpp` covers: empty
index, AddEntry sort order, thumbnail path derivation, Filter empty query
(all returned), single-tag match, AND composition, `-` negation, `OR`
composition, Clear, format preserved.
