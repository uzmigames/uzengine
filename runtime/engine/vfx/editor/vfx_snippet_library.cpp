#include "vfx_snippet_library.h"

#include "engine/logging/log.h"

#include <json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Scan
// ─────────────────────────────────────────────────────────────────────────────

void VfxSnippetIndex::Scan(const std::string& contentDir) {
    entries_.clear();

    std::error_code ec;
    if (!fs::exists(contentDir, ec) || ec) {
        UZ_LOG(VFX, Warning,
               "VfxSnippetIndex::Scan: directory does not exist: '{}'", contentDir);
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(contentDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;

        const auto ext = entry.path().extension().string();
        if (ext != ".uzvfxsys" && ext != ".uzfx") continue;

        // Build the relative path (forward slashes).
        const std::string absPath = entry.path().generic_string();
        const std::string contentDirNorm = fs::path(contentDir).generic_string();
        std::string relPath = absPath;
        if (absPath.size() > contentDirNorm.size() + 1 &&
            absPath.compare(0, contentDirNorm.size(), contentDirNorm) == 0) {
            relPath = absPath.substr(contentDirNorm.size());
            if (!relPath.empty() && relPath.front() == '/') {
                relPath = relPath.substr(1);
            }
        }

        // Read lightweight metadata from JSON.
        std::ifstream file(entry.path());
        if (!file) {
            UZ_LOG(VFX, Warning,
                   "VfxSnippetIndex: failed to open '{}', skipping", relPath);
            continue;
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        const std::string content = ss.str();
        if (content.empty()) continue;

        nlohmann::json root;
        try {
            root = nlohmann::json::parse(content);
        } catch (const nlohmann::json::parse_error&) {
            UZ_LOG(VFX, Warning,
                   "VfxSnippetIndex: malformed JSON in '{}', skipping", relPath);
            continue;
        }

        VfxSnippetEntry snippet;
        snippet.relativePath = relPath;
        snippet.format       = (ext == ".uzvfxsys") ? "uzvfxsys" : "uzfx";
        snippet.thumbnailPath = VfxThumbnailPathForAsset(relPath);

        // Display name: JSON "name" field, or filename stem.
        if (root.contains("name") && root["name"].is_string() &&
            !root["name"].get<std::string>().empty()) {
            snippet.displayName = root["name"].get<std::string>();
        } else {
            snippet.displayName = entry.path().stem().string();
        }

        // Tags (phase 8.2.0).
        if (root.contains("tags") && root["tags"].is_array()) {
            for (const auto& t : root["tags"]) {
                if (t.is_string()) {
                    snippet.tags.push_back(t.get<std::string>());
                }
            }
        }

        entries_.push_back(std::move(snippet));
    }

    // Deterministic order: sorted lexicographically by relative path.
    std::sort(entries_.begin(), entries_.end(),
              [](const VfxSnippetEntry& a, const VfxSnippetEntry& b) {
                  return a.relativePath < b.relativePath;
              });

    UZ_LOG(VFX, Info,
           "VfxSnippetIndex: scanned '{}' — {} assets indexed",
           contentDir, entries_.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter
// ─────────────────────────────────────────────────────────────────────────────

std::vector<const VfxSnippetEntry*> VfxSnippetIndex::Filter(
    const std::string& query) const {
    std::vector<const VfxSnippetEntry*> results;
    results.reserve(entries_.size());
    for (const auto& e : entries_) {
        if (VfxAssetTagMatches(query, e.tags)) {
            results.push_back(&e);
        }
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// AddEntry (tests)
// ─────────────────────────────────────────────────────────────────────────────

void VfxSnippetIndex::AddEntry(VfxSnippetEntry entry) {
    entries_.push_back(std::move(entry));
    // Re-sort to maintain the deterministic-order invariant.
    std::sort(entries_.begin(), entries_.end(),
              [](const VfxSnippetEntry& a, const VfxSnippetEntry& b) {
                  return a.relativePath < b.relativePath;
              });
}

} // namespace uz
