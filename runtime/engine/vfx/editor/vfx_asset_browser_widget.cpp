#include "vfx_asset_browser_widget.h"

#include <algorithm>
#include <cctype>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Refresh — re-query data source + apply filter
// ─────────────────────────────────────────────────────────────────────────────

void VfxAssetBrowserWidget::Refresh() {
    if (dataSource_) {
        allEntries_ = dataSource_();
    }
    ApplyFilter();
}

void VfxAssetBrowserWidget::SetFilter(const std::string& filter) {
    filterText_ = filter;
    ApplyFilter();
}

void VfxAssetBrowserWidget::SelectAsset(const std::string& path) {
    for (const auto& entry : allEntries_) {
        if (entry.assetPath == path) {
            selectedPath_ = path;
            if (onSelectionChanged) {
                onSelectionChanged(path);
            }
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter
// ─────────────────────────────────────────────────────────────────────────────

void VfxAssetBrowserWidget::ApplyFilter() {
    visibleEntries_.clear();
    for (const auto& entry : allEntries_) {
        if (MatchesFilter(entry)) {
            visibleEntries_.push_back(&entry);
        }
    }
}

bool VfxAssetBrowserWidget::MatchesFilter(const VfxBrowserAssetEntry& entry) const {
    if (filterText_.empty()) return true;

    // Lowercase the filter for case-insensitive matching.
    std::string lowerFilter;
    lowerFilter.reserve(filterText_.size());
    for (char c : filterText_) {
        lowerFilter += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Match against display name (case-insensitive substring).
    {
        std::string lowerName;
        lowerName.reserve(entry.displayName.size());
        for (char c : entry.displayName) {
            lowerName += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lowerName.find(lowerFilter) != std::string::npos) {
            return true;
        }
    }

    // Match against tags (case-insensitive substring on each tag).
    for (const auto& tag : entry.tags) {
        std::string lowerTag;
        lowerTag.reserve(tag.size());
        for (char c : tag) {
            lowerTag += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lowerTag.find(lowerFilter) != std::string::npos) {
            return true;
        }
    }

    return false;
}

} // namespace uz
