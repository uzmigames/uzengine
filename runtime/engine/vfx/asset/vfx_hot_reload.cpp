/// VFX Hot-Reload Implementation
///
/// Uses std::filesystem for directory scanning and timestamp polling.
/// Guarded by UZ_DEV_BUILD / !NDEBUG to strip from shipping builds.

#if defined(UZ_DEV_BUILD) || !defined(NDEBUG)

#include "vfx_hot_reload.h"

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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
    vfxSystem_ = nullptr;
    registry_ = nullptr;
    contentDir_.clear();
    reloadCount_ = 0;
    initialized_ = false;

    UZ_LOG(VFX, Info, "VFXHotReload: shut down");
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

    for (const auto& cb : callbacks_) {
        if (cb) {
            cb(path);
        }
    }
}

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

void VFXHotReload::OnReload(VFXReloadCallback callback) {
    callbacks_.push_back(std::move(callback));
}

} // namespace uz

#endif // UZ_DEV_BUILD || !NDEBUG
