/// UzEngine Material Manager — Implementation
///
/// Handles .uzmat loading, path caching, texture path resolution,
/// parameter default extraction, and file-change-based hot-reload.

#include "material_manager.h"

#include "../logging/log.h"

#include <algorithm>
#include <filesystem>

UZ_DEFINE_LOG_CATEGORY(MaterialManager, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Static empty fallbacks for const-ref getters on invalid IDs
// ─────────────────────────────────────────────────────────────────────────────

const std::vector<std::string> MaterialManager::s_emptyTexturePaths;
const std::vector<MaterialManager::ParamDefault> MaterialManager::s_emptyParamDefaults;
const std::string MaterialManager::s_emptyString;

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void MaterialManager::Init() {
    UZ_LOG(MaterialManager, Info, "MaterialManager initialized");
}

void MaterialManager::Shutdown() {
    const uint32_t count = GetMaterialCount();
    UnloadAll();
    UZ_LOG(MaterialManager, Info, "MaterialManager shut down ({} materials released)", count);
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMaterial
// ─────────────────────────────────────────────────────────────────────────────

uint32_t MaterialManager::LoadMaterial(const std::string& path) {
    if (path.empty()) {
        UZ_LOG(MaterialManager, Error, "LoadMaterial called with empty path");
        return kInvalidMaterialId;
    }

    // Normalize the path to a canonical form for consistent cache keying.
    // std::filesystem::weakly_canonical handles "..", ".", and separator normalization
    // without requiring the file to exist at the time of normalization (only the
    // directory prefix must exist). We fall back to the raw path if normalization fails.
    std::string normalizedPath;
    try {
        normalizedPath = std::filesystem::weakly_canonical(path).string();
    } catch (const std::filesystem::filesystem_error&) {
        normalizedPath = path;
    }

    // Check cache first — same normalized path returns the same ID.
    auto it = m_pathToId.find(normalizedPath);
    if (it != m_pathToId.end()) {
        const uint32_t existingId = it->second;
        // Verify the slot is still live (not unloaded).
        if (existingId < m_materials.size() && m_materials[existingId]) {
            return existingId;
        }
        // Slot was unloaded — remove stale cache entry and re-load below.
        m_pathToId.erase(it);
    }

    // Parse the material from disk.
    auto loaded = ParseMaterial(normalizedPath);
    if (!loaded) {
        return kInvalidMaterialId;
    }

    // Assign a new ID (monotonically increasing, never reused).
    const uint32_t id = m_nextId++;

    // Grow the slot array if needed.
    if (id >= m_materials.size()) {
        m_materials.resize(static_cast<size_t>(id) + 1);
    }

    m_materials[id] = std::move(loaded);
    m_pathToId[normalizedPath] = id;

    UZ_LOG(MaterialManager, Info, "Loaded material [{}]: '{}' (hash={:#018x}, {} textures, {} params)",
           id, normalizedPath, m_materials[id]->contentHash,
           m_materials[id]->texturePaths.size(), m_materials[id]->paramDefaults.size());

    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
// Getters
// ─────────────────────────────────────────────────────────────────────────────

const MaterialGraph* MaterialManager::GetGraph(uint32_t id) const {
    if (id >= m_materials.size() || !m_materials[id]) {
        return nullptr;
    }
    return &m_materials[id]->graph;
}

uint64_t MaterialManager::GetContentHash(uint32_t id) const {
    if (id >= m_materials.size() || !m_materials[id]) {
        return 0;
    }
    return m_materials[id]->contentHash;
}

const std::vector<std::string>& MaterialManager::GetTexturePaths(uint32_t id) const {
    if (id >= m_materials.size() || !m_materials[id]) {
        return s_emptyTexturePaths;
    }
    return m_materials[id]->texturePaths;
}

const std::vector<MaterialManager::ParamDefault>& MaterialManager::GetParamDefaults(uint32_t id) const {
    if (id >= m_materials.size() || !m_materials[id]) {
        return s_emptyParamDefaults;
    }
    return m_materials[id]->paramDefaults;
}

const std::string& MaterialManager::GetPath(uint32_t id) const {
    if (id >= m_materials.size() || !m_materials[id]) {
        return s_emptyString;
    }
    return m_materials[id]->path;
}

uint32_t MaterialManager::GetMaterialCount() const {
    uint32_t count = 0;
    for (const auto& slot : m_materials) {
        if (slot) {
            ++count;
        }
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Unload
// ─────────────────────────────────────────────────────────────────────────────

void MaterialManager::UnloadMaterial(uint32_t id) {
    if (id >= m_materials.size() || !m_materials[id]) {
        UZ_LOG(MaterialManager, Warning, "UnloadMaterial: invalid ID {}", id);
        return;
    }

    // Remove the path-to-ID cache entry.
    const std::string& matPath = m_materials[id]->path;
    auto it = m_pathToId.find(matPath);
    if (it != m_pathToId.end() && it->second == id) {
        m_pathToId.erase(it);
    }

    UZ_LOG(MaterialManager, Info, "Unloaded material [{}]: '{}'", id, matPath);
    m_materials[id].reset();
}

void MaterialManager::UnloadAll() {
    m_pathToId.clear();
    m_materials.clear();
    // m_nextId is NOT reset — IDs are never reused for safety.
    UZ_LOG(MaterialManager, Info, "All materials unloaded");
}

// ─────────────────────────────────────────────────────────────────────────────
// Hot-Reload
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint32_t> MaterialManager::PollHotReload() {
    std::vector<uint32_t> reloadedIds;

    for (uint32_t id = 0; id < static_cast<uint32_t>(m_materials.size()); ++id) {
        auto& slot = m_materials[id];
        if (!slot) {
            continue;
        }

        const uint64_t currentModTime = GetFileModTime(slot->path);

        // Skip if the file hasn't changed or if we can't read the timestamp.
        if (currentModTime == 0 || currentModTime == slot->lastModifiedTime) {
            continue;
        }

        // File has changed on disk — re-parse it.
        auto reloaded = ParseMaterial(slot->path);
        if (!reloaded) {
            UZ_LOG(MaterialManager, Warning,
                   "Hot-reload failed for material [{}]: '{}' — keeping previous version",
                   id, slot->path);
            // Update the mod time even on failure so we don't spam re-parse attempts
            // every frame. The user will fix the file and save again, which will
            // produce a new mod time and trigger another reload attempt.
            slot->lastModifiedTime = currentModTime;
            continue;
        }

        // Preserve the original path and directory (already normalized).
        reloaded->path = slot->path;
        reloaded->directory = slot->directory;

        const bool hashChanged = (reloaded->contentHash != slot->contentHash);

        slot = std::move(reloaded);
        reloadedIds.push_back(id);

        if (hashChanged) {
            UZ_LOG(MaterialManager, Info,
                   "Hot-reloaded material [{}]: '{}' (content hash changed → {:#018x})",
                   id, slot->path, slot->contentHash);
        } else {
            UZ_LOG(MaterialManager, Debug,
                   "Hot-reloaded material [{}]: '{}' (file touched, content unchanged)",
                   id, slot->path);
        }
    }

    return reloadedIds;
}

// ─────────────────────────────────────────────────────────────────────────────
// ParseMaterial
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<MaterialManager::LoadedMaterial>
MaterialManager::ParseMaterial(const std::string& path) {
    auto mat = std::make_unique<LoadedMaterial>();
    mat->path = path;

    // Extract the directory containing the .uzmat file for relative texture resolution.
    const auto lastSep = path.find_last_of("/\\");
    mat->directory = (lastSep != std::string::npos) ? path.substr(0, lastSep) : ".";

    // Load and parse the material graph from disk.
    std::string parseError;
    if (!mat->graph.LoadFromFile(path, &parseError)) {
        UZ_LOG(MaterialManager, Error, "Failed to load material '{}': {}", path, parseError);
        return nullptr;
    }

    // Validate the graph structure before proceeding.
    std::string validationError;
    if (!mat->graph.Validate(validationError)) {
        UZ_LOG(MaterialManager, Error, "Material graph validation failed for '{}': {}",
               path, validationError);
        return nullptr;
    }

    // Compute content hash for PSO cache keying.
    mat->contentHash = mat->graph.ComputeContentHash();

    // Extract texture paths from TextureSample and TextureParameter nodes.
    ExtractTexturePaths(mat->graph, mat->directory, mat->texturePaths);

    // Extract parameter defaults from ScalarParameter and VectorParameter nodes.
    ExtractParamDefaults(mat->graph, mat->paramDefaults);

    // Record file modification time for hot-reload polling.
    mat->lastModifiedTime = GetFileModTime(path);

    return mat;
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveTexturePath
// ─────────────────────────────────────────────────────────────────────────────

std::string MaterialManager::ResolveTexturePath(const std::string& materialDir,
                                                 const std::string& texturePath) const {
    if (texturePath.empty()) {
        return {};
    }

    // Absolute path detection:
    // - Starts with '/' (Unix absolute)
    // - Contains ':' at position 1 (Windows drive letter, e.g., "C:\...")
    const bool isAbsolute = (!texturePath.empty() && texturePath[0] == '/') ||
                            (texturePath.size() >= 2 && texturePath[1] == ':');

    if (isAbsolute) {
        return texturePath;
    }

    // Relative path — join with the material directory and canonicalize
    // to resolve any ".." or "." segments.
    const std::string joined = materialDir + "/" + texturePath;
    try {
        return std::filesystem::weakly_canonical(joined).string();
    } catch (const std::filesystem::filesystem_error&) {
        // If canonicalization fails (e.g., parent directory doesn't exist),
        // return the raw joined path. The texture loader will report the
        // error when it tries to open the file.
        return joined;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ExtractTexturePaths
// ─────────────────────────────────────────────────────────────────────────────

void MaterialManager::ExtractTexturePaths(const MaterialGraph& graph,
                                           const std::string& materialDir,
                                           std::vector<std::string>& out) const {
    out.clear();

    for (const auto& expr : graph.GetExpressions()) {
        // TextureSample and TextureParameter both store a "texturePath" string param.
        if (expr.type != MatExprType::TextureSample &&
            expr.type != MatExprType::TextureParameter) {
            continue;
        }

        auto it = expr.stringParams.find("texturePath");
        if (it == expr.stringParams.end() || it->second.empty()) {
            continue;
        }

        std::string resolved = ResolveTexturePath(materialDir, it->second);
        if (resolved.empty()) {
            continue;
        }

        // Avoid duplicates — the same texture may be referenced by multiple nodes.
        auto dupIt = std::find(out.begin(), out.end(), resolved);
        if (dupIt == out.end()) {
            out.push_back(std::move(resolved));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ExtractParamDefaults
// ─────────────────────────────────────────────────────────────────────────────

void MaterialManager::ExtractParamDefaults(const MaterialGraph& graph,
                                            std::vector<ParamDefault>& out) const {
    out.clear();

    for (const auto& expr : graph.GetExpressions()) {
        if (expr.type == MatExprType::ScalarParameter) {
            // ScalarParameter stores: stringParams["paramName"], scalarParams["default"]
            ParamDefault param;

            auto nameIt = expr.stringParams.find("paramName");
            param.name = (nameIt != expr.stringParams.end()) ? nameIt->second : "Unnamed";

            auto defaultIt = expr.scalarParams.find("default");
            param.values[0] = (defaultIt != expr.scalarParams.end()) ? defaultIt->second : 0.f;
            param.values[1] = 0.f;
            param.values[2] = 0.f;
            param.values[3] = 0.f;
            param.componentCount = 1;

            out.push_back(std::move(param));

        } else if (expr.type == MatExprType::VectorParameter) {
            // VectorParameter stores: stringParams["paramName"],
            // scalarParams["defaultR"], scalarParams["defaultG"], scalarParams["defaultB"]
            ParamDefault param;

            auto nameIt = expr.stringParams.find("paramName");
            param.name = (nameIt != expr.stringParams.end()) ? nameIt->second : "Unnamed";

            auto rIt = expr.scalarParams.find("defaultR");
            auto gIt = expr.scalarParams.find("defaultG");
            auto bIt = expr.scalarParams.find("defaultB");
            param.values[0] = (rIt != expr.scalarParams.end()) ? rIt->second : 0.f;
            param.values[1] = (gIt != expr.scalarParams.end()) ? gIt->second : 0.f;
            param.values[2] = (bIt != expr.scalarParams.end()) ? bIt->second : 0.f;
            param.values[3] = 0.f;
            param.componentCount = 3;

            out.push_back(std::move(param));
        }
    }

    // Sort by name for deterministic ordering across loads.
    std::sort(out.begin(), out.end(),
              [](const ParamDefault& a, const ParamDefault& b) {
                  return a.name < b.name;
              });
}

// ─────────────────────────────────────────────────────────────────────────────
// GetFileModTime
// ─────────────────────────────────────────────────────────────────────────────

uint64_t MaterialManager::GetFileModTime(const std::string& path) const {
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }

    // Convert file_time_type to a uint64_t (nanoseconds since epoch).
    // This gives us a monotonically comparable timestamp for change detection.
    const auto duration = ftime.time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

} // namespace uz
