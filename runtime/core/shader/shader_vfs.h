/// UzEngine Shader Virtual File System
///
/// Maps virtual shader paths (e.g., "/engine/pbr.hlsl") to real filesystem
/// paths. All shader loading goes through ShaderVFS — no raw filesystem
/// access in shader code.
///
/// # Design
/// - Mount points map virtual prefixes to real directories
/// - Resolution tries mounts in reverse order (last mount wins)
/// - Thread-safe: all methods are const or guarded
///
/// # Usage
/// ```cpp
/// ShaderVFS vfs;
/// vfs.Mount("/engine", "shaders/engine");
/// vfs.Mount("/game", "shaders/game");
///
/// std::string realPath;
/// if (vfs.Resolve("/engine/pbr.hlsl", realPath)) {
///     // realPath == "shaders/engine/pbr.hlsl"
/// }
/// ```

#pragma once

#include <string>
#include <vector>
#include <optional>

namespace uz {

/// A single virtual-to-real directory mapping.
struct ShaderMount {
    std::string virtualPrefix;  ///< Virtual path prefix (e.g., "/engine")
    std::string realDirectory;  ///< Real filesystem directory (e.g., "shaders/engine")
};

/// Virtual file system for shader source files.
///
/// Provides path resolution from virtual shader paths to real filesystem paths.
/// Mount points are searched in reverse order so that later mounts override
/// earlier ones (useful for mod/override systems).
class ShaderVFS {
public:
    ShaderVFS() = default;

    /// Adds a virtual-to-real directory mapping.
    ///
    /// # Arguments
    /// * `virtualPrefix` — Virtual path prefix (e.g., "/engine"). Must start with '/'.
    /// * `realDirectory` — Real filesystem directory. Forward slashes, no trailing slash.
    ///
    /// # Behavior
    /// Later mounts take priority over earlier ones for the same prefix.
    void Mount(const std::string& virtualPrefix, const std::string& realDirectory);

    /// Removes all mounts matching the given virtual prefix.
    void Unmount(const std::string& virtualPrefix);

    /// Resolves a virtual path to a real filesystem path.
    ///
    /// Searches mounts in reverse order (last mount wins).
    /// Returns the resolved real path, or std::nullopt if no mount matches.
    [[nodiscard]] std::optional<std::string> Resolve(const std::string& virtualPath) const;

    /// Returns true if a file exists at the resolved virtual path.
    [[nodiscard]] bool Exists(const std::string& virtualPath) const;

    /// Reads the entire file content at the given virtual path.
    ///
    /// Returns the file content, or std::nullopt if the path cannot be resolved
    /// or the file cannot be read.
    [[nodiscard]] std::optional<std::string> ReadFile(const std::string& virtualPath) const;

    /// Returns the list of all active mounts (in mount order).
    [[nodiscard]] const std::vector<ShaderMount>& GetMounts() const { return m_mounts; }

    /// Returns the number of active mounts.
    [[nodiscard]] size_t GetMountCount() const { return m_mounts.size(); }

    /// Removes all mounts.
    void Clear();

private:
    std::vector<ShaderMount> m_mounts;
};

} // namespace uz
