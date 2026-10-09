#pragma once

/// VFX Snippet Library Widget — tile browser for authored VFX assets.
///
/// Phase 9.4.0. Wraps the phase 10.3.0 `VfxAssetBrowserWidget` with a
/// data-source adapter that pulls from the phase 8.8.0 `VfxSnippetIndex`,
/// and adds a drag-drop callback suitable for wiring into the VfxSystem
/// editor (drops append a new emitter-ref pointing at the dragged path).
///
/// # Pipeline
///
/// ```
///  VfxSnippetIndex::Scan(contentDir)
///          │
///          ▼
///  VfxSnippetLibraryWidget::Refresh()
///     ├─ walks VfxSnippetIndex::Entries()
///     ├─ builds VfxBrowserAssetEntry[] (name + path + thumb + tags)
///     └─ passes through VfxAssetBrowserWidget data source
///          │
///          ▼
///  User drags a tile → onDropOnSystem(draggedPath, targetSystem)
/// ```
///
/// @ref engine/vfx/editor/vfx_snippet_library.h (index)
/// @ref engine/vfx/editor/vfx_asset_browser_widget.h (tile view)

#include "engine/vfx/editor/vfx_asset_browser_widget.h"
#include "engine/vfx/editor/vfx_snippet_library.h"

#include <cstdint>
#include <functional>
#include <string>

namespace uz {

struct VfxSystem;

/// Snippet library widget — tile browser over `VfxSnippetIndex`.
class VfxSnippetLibraryWidget {
public:
    VfxSnippetLibraryWidget();

    /// Rescans the content directory and rebuilds the tile list.
    ///
    /// # Arguments
    /// - `contentDir` — directory to scan (recursive). Passed through
    ///   to `VfxSnippetIndex::Scan`.
    void ScanAndRefresh(const std::string& contentDir);

    /// Returns the underlying snippet index (for direct queries).
    [[nodiscard]] const VfxSnippetIndex& Index() const noexcept { return index_; }

    /// Returns a mutable reference to the asset-browser widget for host
    /// rendering + mouse routing.
    [[nodiscard]] VfxAssetBrowserWidget& Browser() noexcept { return browser_; }

    /// Returns the number of snippets currently displayed (post-filter).
    [[nodiscard]] uint32_t VisibleCount() const noexcept {
        return browser_.VisibleTileCount();
    }

    /// Drop callback: fires when the user drops a dragged snippet onto
    /// a VfxSystem editor. The callback receives the target system
    /// and the dragged snippet path; typical implementation:
    /// ```cpp
    /// system.emitters.push_back({ "new_emitter", draggedPath, ... });
    /// ```
    std::function<void(const std::string& draggedPath, VfxSystem& targetSystem)>
        onDropOnSystem;

    /// Helper: appends a new emitter-ref to `system` pointing at
    /// `draggedPath`. Callers typically bind `onDropOnSystem` to this.
    static void AppendEmitterRef(const std::string& draggedPath,
                                  VfxSystem& system);

private:
    VfxSnippetIndex        index_;
    VfxAssetBrowserWidget  browser_;

    /// Rebuilds `browser_` data source entries from the index.
    std::vector<VfxBrowserAssetEntry> BuildEntries() const;
};

} // namespace uz
