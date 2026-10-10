#pragma once

/// VFX snippet library — browsable index of pre-authored emitter assets.
///
/// Artists drag-drop emitter snippets from the library into the active
/// VFX system. The index is built by scanning a content directory for
/// `.uzvfxsys` / `.uzfx` files, loading each file's metadata (name, tags,
/// thumbnail path) without fully parsing the module stack, and caching the
/// result in a flat `VfxSnippetEntry` vector.
///
/// # Dependencies
/// - Tag metadata (phase 8.2.0) — each entry carries the asset's tag list.
/// - Thumbnail capture (phase 8.7.0) — entries derive the `.thumb.png` path.
/// - Tag query (phase 8.2.0) — `Filter()` delegates to `VfxAssetTagMatches`.
///
/// # Scanning
/// `VfxSnippetIndex::Scan(contentDir)` walks the directory recursively,
/// reads the top-level JSON fields `name` and `tags` from each recognised
/// file, and derives the thumbnail path via `VfxThumbnailPathForAsset`.
/// The scan produces a deterministic order: entries are sorted
/// lexicographically by relative path so authors and tests get a stable
/// view regardless of filesystem enumeration order.
///
/// # Thread safety
/// Not thread-safe. Scan from the main / editor thread. The resulting
/// index is immutable between scans and can be read freely.
///
/// @ref UE5 UNiagaraSystemEmitterMergeData + Emitter Library UI
/// @ref docs/engine/25-vfx/25-snippet-library.md

#include "engine/vfx/asset/vfx_asset_tags.h"
#include "engine/vfx/baker/vfx_thumbnail.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Index entry
// ─────────────────────────────────────────────────────────────────────────────

/// One discoverable VFX snippet in the library.
///
/// Lightweight metadata loaded during `Scan`; no GPU resources, no module
/// graph, no module stack. The widget loads the full asset lazily when the
/// author drops it into a system.
struct VfxSnippetEntry {
    /// Relative path from the content root (e.g. `"vfx/fireball.uzvfxsys"`).
    /// Serves as the unique key; two entries with the same path are
    /// considered identical (duplicates are suppressed during scan).
    std::string relativePath;

    /// Display name extracted from the asset's `"name"` JSON field.
    /// Falls back to the filename stem when absent.
    std::string displayName;

    /// Tags extracted from the asset's `"tags"` JSON field (phase 8.2.0).
    /// Empty when the asset has no tags or the `"tags"` key is missing.
    std::vector<std::string> tags;

    /// Expected thumbnail path derived via `VfxThumbnailPathForAsset`.
    /// The file may not exist (the asset has never been saved with the
    /// capturer enabled); the widget shows a fallback icon in that case.
    std::string thumbnailPath;

    /// File format discriminator. `"uzvfxsys"` for compound systems,
    /// `"uzfx"` for standalone emitter graphs.
    std::string format;
};

// ─────────────────────────────────────────────────────────────────────────────
// Index
// ─────────────────────────────────────────────────────────────────────────────

/// Scans a content directory and builds a flat, filterable index of VFX
/// snippet assets. Immutable between scans.
class VfxSnippetIndex {
public:
    VfxSnippetIndex() = default;

    /// Recursively scans `contentDir` for `.uzvfxsys` and `.uzfx` files,
    /// reads lightweight metadata from each, and builds the index. Clears
    /// any previous entries. Safe to call multiple times (e.g. after a
    /// hot-reload).
    ///
    /// # Arguments
    /// - `contentDir` — root content directory (e.g. `"content/vfx"`).
    ///   Must exist; logs `VFX/Warning` and produces an empty index if not.
    ///
    /// # Side effects
    /// - Logs `VFX/Info` with the number of indexed files.
    /// - Reads each file's JSON to extract `name` and `tags`; malformed
    ///   files are skipped with a `VFX/Warning`.
    void Scan(const std::string& contentDir);

    /// Returns all entries matching `query` via the tag-query grammar
    /// (phase 8.2.0). Empty / whitespace query returns every entry.
    ///
    /// Allocates a new vector each call — cache the result if the query
    /// has not changed since last frame.
    [[nodiscard]] std::vector<const VfxSnippetEntry*> Filter(
        const std::string& query) const;

    /// Returns every entry in the index (sorted by relative path).
    [[nodiscard]] const std::vector<VfxSnippetEntry>& Entries() const noexcept {
        return entries_;
    }

    /// Number of indexed entries.
    [[nodiscard]] uint32_t Count() const noexcept {
        return static_cast<uint32_t>(entries_.size());
    }

    /// Clears the index.
    void Clear() noexcept { entries_.clear(); }

    /// Adds an entry directly (used by tests that skip filesystem scanning).
    void AddEntry(VfxSnippetEntry entry);

private:
    std::vector<VfxSnippetEntry> entries_;
};

} // namespace uz
