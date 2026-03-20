/// UzEngine Material Manager — Asset Lifecycle for .uzmat Materials
///
/// Manages the lifecycle of material assets: loading .uzmat files, resolving
/// texture references relative to the material directory, caching material
/// instances by path, extracting parameter defaults, and supporting hot-reload
/// when .uzmat files change on disk.
///
/// Materials are referenced by a stable uint32_t ID. The same file path always
/// returns the same ID (cached). IDs are NOT reused after UnloadMaterial().
///
/// Material resource collection pattern — caches compiled material graphs.
///
/// # Thread Safety
/// NOT thread-safe. Call from the game thread only, consistent with
/// MaterialGraph's thread-safety contract.

#pragma once

#include "material_graph.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Sentinel value returned when a material fails to load.
inline constexpr uint32_t kInvalidMaterialId = UINT32_MAX;

// ─────────────────────────────────────────────────────────────────────────────
// MaterialManager
// ─────────────────────────────────────────────────────────────────────────────

/// Manages the lifecycle of material assets.
///
/// Loads .uzmat files, resolves texture references, caches material instances,
/// and supports hot-reload. Materials are referenced by path and shared across
/// all objects using the same material.
///
/// Central material resource collection — owns, compiles, and caches materials.
class MaterialManager {
public:
    MaterialManager() = default;
    ~MaterialManager() = default;

    // Non-copyable, movable.
    MaterialManager(const MaterialManager&) = delete;
    MaterialManager& operator=(const MaterialManager&) = delete;
    MaterialManager(MaterialManager&&) noexcept = default;
    MaterialManager& operator=(MaterialManager&&) noexcept = default;

    /// Initialize the manager. Call once at engine startup.
    void Init();

    /// Shutdown and release all materials. Call once at engine shutdown.
    void Shutdown();

    /// Loads a material from a .uzmat file path.
    ///
    /// Returns a stable ID. Same path returns same ID (cached).
    /// If the file doesn't exist or fails to parse, returns kInvalidMaterialId.
    ///
    /// @param path  Absolute or relative path to the .uzmat file.
    /// @return      Material ID, or kInvalidMaterialId on failure.
    [[nodiscard]] uint32_t LoadMaterial(const std::string& path);

    /// Returns the loaded MaterialGraph for a material ID. Null if invalid.
    [[nodiscard]] const MaterialGraph* GetGraph(uint32_t id) const;

    /// Returns the content hash for a material ID. Zero if invalid.
    [[nodiscard]] uint64_t GetContentHash(uint32_t id) const;

    /// Returns the resolved texture paths for a material.
    ///
    /// These are absolute paths ready for SceneRenderer::LoadTexture().
    /// Returns an empty vector if the ID is invalid.
    [[nodiscard]] const std::vector<std::string>& GetTexturePaths(uint32_t id) const;

    /// A parameter default extracted from ScalarParameter/VectorParameter nodes.
    struct ParamDefault {
        std::string name;                ///< Parameter name from the "paramName" string param
        float values[4] = {};            ///< Default value components
        uint32_t componentCount = 1;     ///< 1 = scalar, 3 = vec3 (VectorParameter)
    };

    /// Returns the material parameter defaults for a material ID.
    ///
    /// Extracted from ScalarParameter and VectorParameter expression nodes.
    /// Returns an empty vector if the ID is invalid.
    [[nodiscard]] const std::vector<ParamDefault>& GetParamDefaults(uint32_t id) const;

    /// Returns the original file path for a material ID. Empty if invalid.
    [[nodiscard]] const std::string& GetPath(uint32_t id) const;

    /// Checks all loaded materials for file changes on disk.
    ///
    /// Compares stored modification timestamps against current file state.
    /// Re-parses any changed files and updates the cached data.
    /// Call once per frame from the game thread.
    ///
    /// @return  IDs of materials that were successfully reloaded.
    [[nodiscard]] std::vector<uint32_t> PollHotReload();

    /// Returns the number of currently loaded materials (excluding unloaded slots).
    [[nodiscard]] uint32_t GetMaterialCount() const;

    /// Removes a material from the cache. The ID becomes invalid.
    ///
    /// After this call, GetGraph/GetContentHash/etc. for this ID return
    /// defaults. The ID is never reused.
    void UnloadMaterial(uint32_t id);

    /// Removes all materials from the cache.
    void UnloadAll();

private:
    /// Internal storage for a loaded material and its derived data.
    struct LoadedMaterial {
        std::string path;                       ///< Original .uzmat file path
        std::string directory;                  ///< Directory containing the .uzmat file
        MaterialGraph graph;                    ///< Parsed material graph
        uint64_t contentHash = 0;               ///< Graph content hash for PSO cache keying
        std::vector<std::string> texturePaths;  ///< Resolved absolute texture paths
        std::vector<ParamDefault> paramDefaults;///< Parameter defaults from graph
        uint64_t lastModifiedTime = 0;          ///< File modification timestamp for hot-reload
    };

    /// Path-to-ID lookup for deduplication.
    std::unordered_map<std::string, uint32_t> m_pathToId;

    /// Slot array of loaded materials. Null entries are unloaded slots.
    std::vector<std::unique_ptr<LoadedMaterial>> m_materials;

    /// Monotonically increasing ID counter. IDs are never reused.
    uint32_t m_nextId = 0;

    /// Empty fallbacks returned by const-ref getters for invalid IDs.
    static const std::vector<std::string> s_emptyTexturePaths;
    static const std::vector<ParamDefault> s_emptyParamDefaults;
    static const std::string s_emptyString;

    /// Loads and parses a .uzmat file, resolves texture paths, extracts params.
    [[nodiscard]] std::unique_ptr<LoadedMaterial> ParseMaterial(const std::string& path);

    /// Resolves a texture path relative to the material's directory.
    ///
    /// Absolute paths (starting with '/' or containing ':') are returned as-is.
    /// Relative paths are joined with materialDir.
    [[nodiscard]] std::string ResolveTexturePath(const std::string& materialDir,
                                                  const std::string& texturePath) const;

    /// Extracts parameter defaults from ScalarParameter and VectorParameter nodes.
    void ExtractParamDefaults(const MaterialGraph& graph,
                              std::vector<ParamDefault>& out) const;

    /// Extracts and resolves texture paths from TextureSample and TextureParameter nodes.
    void ExtractTexturePaths(const MaterialGraph& graph,
                             const std::string& materialDir,
                             std::vector<std::string>& out) const;

    /// Gets file modification time as a uint64_t (nanoseconds since epoch).
    /// Returns 0 if the file does not exist or cannot be queried.
    [[nodiscard]] uint64_t GetFileModTime(const std::string& path) const;
};

} // namespace uz
