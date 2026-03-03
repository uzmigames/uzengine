// asset_registry.cpp -- Asset discovery and import tracking implementation.
//
// Uses C++17 <filesystem> for recursive directory walking on all platforms.
// Sidecar .import.json files are written next to each source asset using
// nlohmann::json for serialization.

#include "engine/editor/asset_registry.h"
#include "engine/core/file_watcher.h"
#include "engine/core/platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <ctime>
#include <chrono>

namespace fs = std::filesystem;

namespace uz {

// ===========================================================================
// AssetType helpers
// ===========================================================================

const char* asset_type_to_string(AssetType type) noexcept {
    switch (type) {
        case AssetType::Mesh:     return "Mesh";
        case AssetType::Texture:  return "Texture";
        case AssetType::Audio:    return "Audio";
        case AssetType::Scene:    return "Scene";
        case AssetType::Material: return "Material";
        case AssetType::Shader:   return "Shader";
        case AssetType::Unknown:  return "Unknown";
    }
    return "Unknown";
}

AssetType asset_type_from_string(const char* str) noexcept {
    if (!str) return AssetType::Unknown;
    if (std::strcmp(str, "Mesh")     == 0) return AssetType::Mesh;
    if (std::strcmp(str, "Texture")  == 0) return AssetType::Texture;
    if (std::strcmp(str, "Audio")    == 0) return AssetType::Audio;
    if (std::strcmp(str, "Scene")    == 0) return AssetType::Scene;
    if (std::strcmp(str, "Material") == 0) return AssetType::Material;
    if (std::strcmp(str, "Shader")   == 0) return AssetType::Shader;
    return AssetType::Unknown;
}

AssetType classify_asset(const std::string& path) noexcept {
    // Find the last '.' to extract extension.
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return AssetType::Unknown;

    // Extract extension and convert to lowercase for case-insensitive matching.
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Mesh
    if (ext == ".glb" || ext == ".gltf") return AssetType::Mesh;

    // Texture
    if (ext == ".png" || ext == ".jpg" || ext == ".ktx2") return AssetType::Texture;

    // Audio
    if (ext == ".wav" || ext == ".ogg") return AssetType::Audio;

    // Scene
    if (ext == ".uzscene") return AssetType::Scene;

    // Material
    if (ext == ".uzmat") return AssetType::Material;

    // Shader
    if (ext == ".hlsl" || ext == ".glsl") return AssetType::Shader;

    return AssetType::Unknown;
}

// ===========================================================================
// ImportStatus helpers
// ===========================================================================

const char* import_status_to_string(ImportStatus status) noexcept {
    switch (status) {
        case ImportStatus::Fresh:       return "Fresh";
        case ImportStatus::Stale:       return "Stale";
        case ImportStatus::Error:       return "Error";
        case ImportStatus::NotImported: return "NotImported";
    }
    return "NotImported";
}

// ===========================================================================
// AssetRegistry
// ===========================================================================

AssetRegistry::AssetRegistry() = default;
AssetRegistry::~AssetRegistry() = default;

uint32_t AssetRegistry::scan(const std::string& project_root) {
    entries_.clear();
    project_root_ = project_root;

    // Normalize trailing separator.
    if (!project_root_.empty() && project_root_.back() != '/' && project_root_.back() != '\\') {
        project_root_ += '/';
    }

    scan_directory(project_root_, "");

    return static_cast<uint32_t>(entries_.size());
}

const std::vector<AssetEntry>& AssetRegistry::entries() const noexcept {
    return entries_;
}

AssetEntry* AssetRegistry::get(const std::string& path) noexcept {
    for (auto& entry : entries_) {
        if (entry.path == path) return &entry;
    }
    return nullptr;
}

const AssetEntry* AssetRegistry::get(const std::string& path) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.path == path) return &entry;
    }
    return nullptr;
}

bool AssetRegistry::import_asset(const std::string& path) {
    AssetEntry* entry = get(path);
    if (!entry) return false;

    // Build absolute path.
    const std::string abs_path = project_root_ + entry->path;

    // Check that the source file still exists.
    if (!platform::path_exists(abs_path.c_str())) {
        entry->status = ImportStatus::Error;
        entry->error_msg = "source file not found: " + abs_path;
        return false;
    }

    // Compute current hash.
    const uint64_t current_hash = compute_hash(abs_path);

    // If already fresh and hash matches, skip.
    if (entry->status == ImportStatus::Fresh && entry->hash == current_hash) {
        return true;
    }

    // Build the .import.json sidecar content.
    const std::string sidecar_path = import_json_path(entry->path);

    // Ensure parent directory exists.
    const auto parent = fs::path(sidecar_path).parent_path();
    if (!parent.empty()) {
        platform::mkdir_recursive(parent.string().c_str());
    }

    // Current timestamp as ISO 8601.
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char time_buf[64] = {};
    struct tm tm_buf {};
#if defined(UZ_PLATFORM_WINDOWS)
    gmtime_s(&tm_buf, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_buf);
#endif
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);

    nlohmann::json import_json = {
        {"source",      entry->path},
        {"type",        asset_type_to_string(entry->type)},
        {"hash",        current_hash},
        {"imported_at", std::string(time_buf)}
    };

    const std::string json_str = import_json.dump(2);
    if (!platform::file_write_all(sidecar_path.c_str(), json_str.data(), json_str.size())) {
        entry->status = ImportStatus::Error;
        entry->error_msg = "failed to write .import.json: " + sidecar_path;
        return false;
    }

    // Update entry.
    entry->hash = current_hash;
    entry->status = ImportStatus::Fresh;
    entry->error_msg.clear();
    return true;
}

std::string AssetRegistry::read_import_json(const std::string& path) const {
    const std::string sidecar_path = import_json_path(path);

    size_t size = 0;
    char* data = platform::file_read_all(sidecar_path.c_str(), &size);
    if (!data) return {};

    std::string result(data, size);
    std::free(data);
    return result;
}

void AssetRegistry::watch(FileWatcher& watcher) {
    // Stop any existing watch first — FileWatcher only supports one directory.
    watcher.stop();

    if (project_root_.empty()) return;

    watcher.watch(project_root_.c_str(), [this](const FileChange& change) {
        // Ignore .import.json sidecar file changes to avoid infinite rescan loops.
        if (change.path.find(".import.json") != std::string::npos) return;

        // Re-scan the project root on any file change.
        scan(project_root_);
    });
}

const std::string& AssetRegistry::project_root() const noexcept {
    return project_root_;
}

void AssetRegistry::clear() noexcept {
    entries_.clear();
    project_root_.clear();
}

// ===========================================================================
// Private helpers
// ===========================================================================

void AssetRegistry::scan_directory(const std::string& dir,
                                     const std::string& relative_prefix) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;

        const auto& p = entry.path();
        const std::string filename = p.filename().string();

        // Skip hidden files/directories (start with '.').
        if (!filename.empty() && filename[0] == '.') continue;

        // Skip .import.json sidecar files from listing.
        if (filename.size() > 12 &&
            filename.substr(filename.size() - 12) == ".import.json") {
            continue;
        }

        if (entry.is_directory(ec)) {
            const std::string sub_prefix = relative_prefix.empty()
                ? filename
                : relative_prefix + "/" + filename;
            scan_directory(p.string(), sub_prefix);
        } else if (entry.is_regular_file(ec)) {
            const AssetType type = classify_asset(filename);
            if (type == AssetType::Unknown) continue;

            const std::string rel_path = relative_prefix.empty()
                ? filename
                : relative_prefix + "/" + filename;

            AssetEntry asset;
            asset.path = rel_path;
            asset.type = type;
            asset.hash = compute_hash(p.string());

            // Check for existing .import.json to determine initial status.
            refresh_entry_status(asset);

            entries_.push_back(std::move(asset));
        }
    }
}

uint64_t AssetRegistry::compute_hash(const std::string& abs_path) const noexcept {
    // Lightweight hash: combine file size with last-write-time.
    // This is not cryptographic — just for change detection.
    std::error_code ec;
    const auto fsize = fs::file_size(abs_path, ec);
    if (ec) return 0;

    const auto ftime = fs::last_write_time(abs_path, ec);
    if (ec) return 0;

    const auto duration = ftime.time_since_epoch();
    const auto mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

    // Mix size and mtime into a single 64-bit hash.
    // FNV-1a inspired mixing.
    uint64_t h = 14695981039346656037ULL; // FNV offset basis
    h ^= fsize;
    h *= 1099511628211ULL; // FNV prime
    h ^= static_cast<uint64_t>(mtime_ns);
    h *= 1099511628211ULL;
    return h;
}

std::string AssetRegistry::import_json_path(const std::string& rel_path) const {
    return project_root_ + rel_path + ".import.json";
}

void AssetRegistry::refresh_entry_status(AssetEntry& entry) {
    const std::string sidecar = import_json_path(entry.path);

    size_t size = 0;
    char* data = platform::file_read_all(sidecar.c_str(), &size);
    if (!data) {
        entry.status = ImportStatus::NotImported;
        return;
    }

    // Parse JSON.
    try {
        auto j = nlohmann::json::parse(std::string(data, size));
        std::free(data);

        // Check for error field.
        if (j.contains("error") && j["error"].is_string()) {
            entry.status = ImportStatus::Error;
            entry.error_msg = j["error"].get<std::string>();
            return;
        }

        // Compare hash.
        if (j.contains("hash") && j["hash"].is_number_unsigned()) {
            const uint64_t stored_hash = j["hash"].get<uint64_t>();
            if (stored_hash == entry.hash) {
                entry.status = ImportStatus::Fresh;
            } else {
                entry.status = ImportStatus::Stale;
            }
        } else {
            entry.status = ImportStatus::Stale;
        }
    } catch (...) {
        std::free(data);
        entry.status = ImportStatus::NotImported;
    }
}

} // namespace uz
