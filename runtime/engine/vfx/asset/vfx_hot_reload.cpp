/// VFX Hot-Reload Implementation
///
/// Uses std::filesystem for directory scanning and timestamp polling.
///
/// The class is instantiated only in dev builds — see scene_renderer_init.cpp,
/// which guards the `make_unique<VFXHotReload>()` call with `#if !NDEBUG`. The
/// implementation itself is unconditionally compiled because `unique_ptr<T>`
/// in the renderer header forces the destructor to be visible to the linker
/// even in release builds where the unique_ptr is always null. Unreferenced
/// methods are stripped by DCE in `-Doptimize=ReleaseFast`.

#include "vfx_hot_reload.h"
#include "vfx_emitter_inheritance.h"

#include "log.h"
#include "profiler.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace uz {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Path normalization
// ---------------------------------------------------------------------------

/// Converts a raw path to forward slashes, lowercase on Windows.
std::string VFXHotReload::NormalizePath(const std::string& rawPath) {
    std::string result = fs::path(rawPath).generic_string();

#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif

    return result;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

VFXHotReload::~VFXHotReload() {
    if (initialized_) {
        Shutdown();
    }
}

/// Scans `contentDir` for .uzvfx files and records baseline timestamps.
void VFXHotReload::Initialize(const std::string& contentDir, VFXSystem* vfxSystem,
                               const ModuleRegistry* registry) {
    if (initialized_) {
        Shutdown();
    }

    vfxSystem_ = vfxSystem;
    registry_ = registry;
    contentDir_ = contentDir;

    ScanDirectory(contentDir);

    initialized_ = true;

    UZ_LOG(VFX, Info, "VFXHotReload: initialized, watching '{}' ({} files)",
           contentDir_, watchedFiles_.size());
}

void VFXHotReload::Shutdown() {
    watchedFiles_.clear();
    fileTimestamps_.clear();
    callbacks_.clear();
    inheritanceMap_.clear();
    vfxSystem_ = nullptr;
    registry_ = nullptr;
    contentDir_.clear();
    reloadCount_ = 0;
    initialized_ = false;

    UZ_LOG(VFX, Info, "VFXHotReload: shut down");
}

// ---------------------------------------------------------------------------
// Inheritance map (phase 8.3.0)
// ---------------------------------------------------------------------------

/// Parses the `"parent"` JSON field out of `normalizedPath` and updates
/// `inheritanceMap_` so ReloadGraph can propagate edits downstream.
void VFXHotReload::UpdateInheritanceLink(const std::string& normalizedPath) {
    // Clear any prior edge from this child: it may have been renamed or had
    // its parent field removed since the last scan.
    for (auto& kv : inheritanceMap_) {
        auto& deps = kv.second;
        deps.erase(std::remove(deps.begin(), deps.end(), normalizedPath),
                   deps.end());
    }

    std::ifstream f(normalizedPath);
    if (!f) return;
    std::ostringstream ss; ss << f.rdbuf();
    const std::string content = ss.str();
    if (content.empty()) return;

    try {
        auto root = nlohmann::json::parse(content);
        if (!root.is_object() || !root.contains("parent") ||
            !root["parent"].is_string()) {
            return;
        }
        const std::string parent = VfxNormalizeInheritancePath(
            root["parent"].get<std::string>());
        auto& deps = inheritanceMap_[parent];
        if (std::find(deps.begin(), deps.end(), normalizedPath) == deps.end()) {
            deps.push_back(normalizedPath);
        }
    } catch (const nlohmann::json::parse_error&) {
        // Non-JSON / malformed file — tolerate; a later reload will retry.
    }
}

void VFXHotReload::ReloadDescendants(const std::string& parentPath,
                                     std::unordered_set<std::string>& visited) {
    auto it = inheritanceMap_.find(parentPath);
    if (it == inheritanceMap_.end()) return;

    // Copy the list because ReloadGraph on a descendant might mutate the map.
    const std::vector<std::string> children = it->second;
    for (const auto& child : children) {
        if (!visited.insert(child).second) continue; // already reloaded
        UZ_LOG(VFX, Info,
               "VFXHotReload: cascading reload of descendant '{}'", child);
        ReloadGraph(child);
    }
}

// ---------------------------------------------------------------------------
// Directory scanning
// ---------------------------------------------------------------------------

/// Walks the directory tree and records timestamps for all .uzvfx files.
void VFXHotReload::ScanDirectory(const std::string& dir) {
    std::error_code ec;

    if (!fs::exists(dir, ec) || ec) {
        UZ_LOG(VFX, Warning, "VFXHotReload: directory does not exist: '{}'", dir);
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;

        const auto ext = entry.path().extension().string();
        if (ext != ".uzvfx" && ext != ".uzfx") continue;

        auto normalized = NormalizePath(entry.path().string());
        auto writeTime = entry.last_write_time(ec);
        if (ec) continue;

        // Store as integer ticks for stable comparison.
        auto ticks = writeTime.time_since_epoch().count();
        fileTimestamps_[normalized] = static_cast<uint64_t>(ticks);
        watchedFiles_.push_back(normalized);
        // Record parent → child edge for inheritance-aware reload (8.3.0).
        UpdateInheritanceLink(normalized);
    }
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

/// Checks each tracked file for timestamp changes and triggers reload.
void VFXHotReload::Poll() {
    UZ_PROFILE_FUNCTION();
    if (!initialized_) return;

    for (const auto& filePath : watchedFiles_) {
        std::error_code ec;
        auto writeTime = fs::last_write_time(filePath, ec);
        if (ec) continue;

        auto ticks = static_cast<uint64_t>(writeTime.time_since_epoch().count());
        auto it = fileTimestamps_.find(filePath);
        if (it == fileTimestamps_.end()) continue;

        if (it->second != ticks) {
            it->second = ticks;
            ReloadGraph(filePath);
        }
    }
}

// ---------------------------------------------------------------------------
// Graph reload
// ---------------------------------------------------------------------------

/// Reloads a VFX asset and invokes registered callbacks.
///
/// For .uzfx files: re-parses the JSON and logs the reload event.
/// The actual emitter re-instantiation happens in SceneRenderer::SyncVfxEmitters
/// via the reload callback, which marks affected VfxEmitterComponents as dirty
/// so they get re-created on the next frame.
///
/// For .uzvfx files: graph reload (module swap + shader recomposition).
void VFXHotReload::ReloadGraph(const std::string& path) {
    const auto ext = fs::path(path).extension().string();

    if (ext == ".uzfx") {
        UZ_LOG(VFX, Info, "VFXHotReload: .uzfx changed '{}' — emitters will re-instantiate next frame", path);
    } else {
        UZ_LOG(VFX, Info, "VFXHotReload: .uzvfx changed '{}' — graph reload", path);
    }

    ++reloadCount_;

    // Refresh the parent edge for this file before cascading: the edit may
    // have added, removed, or retargeted the `"parent"` JSON key.
    UpdateInheritanceLink(path);

    for (const auto& cb : callbacks_) {
        if (cb) {
            cb(path);
        }
    }

    // Cascade to descendants so parent edits propagate through the chain
    // (phase 8.3.0). Visited set guards against cycles.
    std::unordered_set<std::string> visited;
    visited.insert(path);
    ReloadDescendants(path, visited);
}

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

void VFXHotReload::OnReload(VFXReloadCallback callback) {
    callbacks_.push_back(std::move(callback));
}

} // namespace uz
