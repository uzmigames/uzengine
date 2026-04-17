#pragma once

/// VFX Asset Browser Widget — scrollable tile grid with thumbnails + tag filter.
///
/// Phase 10.3.0. Renders a grid of asset tiles inside the editor overlay,
/// each with a thumbnail image (or fallback icon), display name, and tag
/// chips. A filter bar at the top (using the 10.1 `StringInput` widget)
/// live-filters tiles by name or tag query.
///
/// # Data source
///
/// The widget is data-agnostic: the caller provides a pluggable
/// `AssetDataSource` lambda that returns the current asset list. The
/// widget re-queries every frame (or on filter change) and rebuilds
/// the visible tile set.
///
/// # Selection + drag
///
/// Single-click selects a tile. `onSelectionChanged` fires with the
/// selected asset's path. Click+drag initiates a drag with the asset
/// path as payload; `onDragStart` fires so the caller can render a
/// ghost indicator.
///
/// @ref UE5 Content Browser tile view
/// @ref engine/vfx/editor/vfx_editor_overlay.h (host overlay)

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Asset entry (data source output)
// ─────────────────────────────────────────────────────────────────────────────

/// One asset visible in the browser. The data source lambda fills an array
/// of these each frame (or on-demand when the filter changes).
struct VfxBrowserAssetEntry {
    /// Asset display name (shown under the thumbnail).
    std::string displayName;

    /// Asset path (used for selection callback + drag payload).
    std::string assetPath;

    /// Optional thumbnail path (`.thumb.png`). Empty = fallback icon.
    std::string thumbnailPath;

    /// Tags for filter matching (lowercase, from the asset's metadata).
    std::vector<std::string> tags;
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

/// Scrollable asset tile grid with filter bar.
class VfxAssetBrowserWidget {
public:
    // ── Configuration ───────────────────────────────────────────────────

    /// Data source: called to populate the asset list. The widget calls
    /// this when the filter changes or on first frame.
    using AssetDataSource = std::function<std::vector<VfxBrowserAssetEntry>()>;

    /// Sets the data source callback.
    void SetDataSource(AssetDataSource source) { dataSource_ = std::move(source); }

    /// Tile dimensions in pixels.
    float tileWidth  = 96.0f;
    float tileHeight = 110.0f;
    float tileGap    = 6.0f;

    // ── Callbacks ───────────────────────────────────────────────────────

    /// Fires when the selected asset changes (single-click).
    std::function<void(const std::string& assetPath)> onSelectionChanged;

    /// Fires when the user starts dragging a tile.
    std::function<void(const std::string& assetPath)> onDragStart;

    // ── Lifecycle ───────────────────────────────────────────────────────

    /// Refreshes the asset list from the data source. Call once per frame
    /// or when the filter changes. Rebuilds the visible tile set.
    void Refresh();

    /// Returns the current filter string (for external binding).
    [[nodiscard]] const std::string& GetFilter() const noexcept { return filterText_; }

    /// Sets the filter text programmatically (e.g. from a tag click).
    void SetFilter(const std::string& filter);

    /// Returns a mutable reference to the filter buffer for use with
    /// `VfxEditorOverlay::StringInput`.
    [[nodiscard]] std::string& FilterBuffer() noexcept { return filterText_; }

    // ── State queries ───────────────────────────────────────────────────

    /// Returns the currently selected asset path (empty if none).
    [[nodiscard]] const std::string& SelectedAssetPath() const noexcept {
        return selectedPath_;
    }

    /// Returns the number of visible (post-filter) tiles.
    [[nodiscard]] uint32_t VisibleTileCount() const noexcept {
        return static_cast<uint32_t>(visibleEntries_.size());
    }

    /// Returns the total number of assets (pre-filter).
    [[nodiscard]] uint32_t TotalAssetCount() const noexcept {
        return static_cast<uint32_t>(allEntries_.size());
    }

    /// Returns the scroll offset in pixels.
    [[nodiscard]] float ScrollOffset() const noexcept { return scrollOffset_; }

    /// Sets the scroll offset (for external control).
    void SetScrollOffset(float offset) noexcept { scrollOffset_ = offset; }

    /// Selects an asset by path. No-op if path not found.
    void SelectAsset(const std::string& path);

    /// Clears the selection.
    void ClearSelection() noexcept { selectedPath_.clear(); }

private:
    /// Applies the filter to `allEntries_` and populates `visibleEntries_`.
    void ApplyFilter();

    /// Returns true when `entry` matches the current filter text.
    /// Matches against display name and tags (case-insensitive substring).
    [[nodiscard]] bool MatchesFilter(const VfxBrowserAssetEntry& entry) const;

    AssetDataSource dataSource_;
    std::vector<VfxBrowserAssetEntry> allEntries_;
    std::vector<const VfxBrowserAssetEntry*> visibleEntries_;
    std::string filterText_;
    std::string selectedPath_;
    float scrollOffset_ = 0.0f;
};

} // namespace uz
