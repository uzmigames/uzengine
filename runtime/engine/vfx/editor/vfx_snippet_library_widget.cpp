#include "vfx_snippet_library_widget.h"

#include "engine/vfx/system/vfx_system.h"

namespace uz {

VfxSnippetLibraryWidget::VfxSnippetLibraryWidget() {
    browser_.SetDataSource([this]() { return BuildEntries(); });
}

void VfxSnippetLibraryWidget::ScanAndRefresh(const std::string& contentDir) {
    index_.Scan(contentDir);
    browser_.Refresh();
}

std::vector<VfxBrowserAssetEntry> VfxSnippetLibraryWidget::BuildEntries() const {
    std::vector<VfxBrowserAssetEntry> out;
    const auto& entries = index_.Entries();
    out.reserve(entries.size());
    for (const auto& e : entries) {
        VfxBrowserAssetEntry entry;
        entry.displayName   = e.displayName.empty() ? e.relativePath : e.displayName;
        entry.assetPath     = e.relativePath;
        entry.tags          = e.tags;
        // Derive thumbnail path by stripping the extension and appending ".thumb.png".
        const auto dot = e.relativePath.rfind('.');
        entry.thumbnailPath = (dot != std::string::npos)
            ? e.relativePath.substr(0, dot) + ".thumb.png"
            : e.relativePath + ".thumb.png";
        out.push_back(std::move(entry));
    }
    return out;
}

void VfxSnippetLibraryWidget::AppendEmitterRef(const std::string& draggedPath,
                                                 VfxSystem& system) {
    if (draggedPath.empty()) return;

    VfxSystemEmitterRef ref;
    // Derive a display name from the filename (stem).
    auto lastSlash = draggedPath.find_last_of("/\\");
    std::string stem = (lastSlash != std::string::npos)
        ? draggedPath.substr(lastSlash + 1)
        : draggedPath;
    const auto dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);

    ref.name        = stem;
    ref.emitterPath = draggedPath;
    // Generate a simple unique id: "e<N>" where N = current emitter count.
    ref.id = "e" + std::to_string(system.emitters.size());
    ref.enabled    = true;
    ref.localSpace = true;

    system.emitters.push_back(std::move(ref));
}

} // namespace uz
