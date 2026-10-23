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
///
/// Represents one mount point in the shader VFS. The virtual prefix is stripped
/// and replaced with the real directory when resolving paths.
///
/// # Examples
/// ```cpp
/// ShaderMount mount{"/engine", "shaders/engine"};
/// // "/engine/pbr.hlsl" resolves to "shaders/engine/pbr.hlsl"
/// ```
struct ShaderMount {
    std::string virtualPrefix;  ///< Virtual path prefix (e.g., "/engine")
    std::string realDirectory;  ///< Real filesystem directory (e.g., "shaders/engine")
};

/// Virtual file system for shader source files.
///
/// Provides path resolution from virtual shader paths to real filesystem paths.
/// Mount points are searched in reverse order so that later mounts override
/// earlier ones (useful for mod/override systems).
///
/// # Thread Safety
/// NOT thread-safe. All mount/resolve operations must happen on the same thread
/// or be externally synchronized.
///
/// # Lifetime
/// Must outlive any `ShaderSourceManager` or `ShaderDependencyGraph` that
/// references it.
///
/// # Examples
/// ```cpp
/// ShaderVFS vfs;
/// vfs.Mount("/engine", "shaders/engine");
/// vfs.Mount("/game", "shaders/game");
///
/// auto resolved = vfs.Resolve("/engine/common.hlsli");
/// // resolved == "shaders/engine/common.hlsli"
///
/// auto source = vfs.ReadFile("/engine/pbr.hlsl");
/// if (source) { /* use *source */ }
/// ```
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
    /// Duplicate prefixes are allowed — each creates a separate mount entry.
    ///
    /// # Examples
    /// ```cpp
    /// vfs.Mount("/engine", "shaders/engine");
    /// vfs.Mount("/engine", "shaders/engine_override"); // overrides for same prefix
    /// ```
    void Mount(const std::string& virtualPrefix, const std::string& realDirectory);

    /// Removes all mounts matching the given virtual prefix.
    ///
    /// # Arguments
    /// * `virtualPrefix` — The prefix to unmount. All mounts with this exact prefix
    ///   are removed.
    void Unmount(const std::string& virtualPrefix);

    /// Resolves a virtual path to a real filesystem path.
    ///
    /// Searches mounts in reverse order (last mount wins). The virtual prefix is
    /// stripped and replaced with the mount's real directory.
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual path to resolve (e.g., "/engine/pbr.hlsl").
    ///
    /// # Returns
    /// The resolved real filesystem path, or `std::nullopt` if no mount matches
    /// the path's prefix, or if the path contains a `..` segment or a NUL byte
    /// (it could otherwise climb out of the mount's real directory).
    ///
    /// # Examples
    /// ```cpp
    /// vfs.Mount("/engine", "shaders/engine");
    /// auto path = vfs.Resolve("/engine/pbr.hlsl");
    /// // path == "shaders/engine/pbr.hlsl"
    ///
    /// auto none = vfs.Resolve("/unknown/foo.hlsl");
    /// // none == std::nullopt
    /// ```
    [[nodiscard]] std::optional<std::string> Resolve(const std::string& virtualPath) const;

    /// Returns true if a file exists at the resolved virtual path.
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual path to check.
    ///
    /// # Returns
    /// `true` if the path resolves to a mount AND the resulting file exists on disk.
    [[nodiscard]] bool Exists(const std::string& virtualPath) const;

    /// Reads the entire file content at the given virtual path.
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual path to read.
    ///
    /// # Returns
    /// The file content as a string, or `std::nullopt` if the path cannot be
    /// resolved or the file cannot be read from disk.
    [[nodiscard]] std::optional<std::string> ReadFile(const std::string& virtualPath) const;

    /// Returns the list of all active mounts (in mount order).
    ///
    /// # Returns
    /// Const reference to the internal mount vector. First-mounted entries appear first.
    [[nodiscard]] const std::vector<ShaderMount>& GetMounts() const { return m_mounts; }

    /// Returns the number of active mounts.
    [[nodiscard]] size_t GetMountCount() const { return m_mounts.size(); }

    /// Removes all mounts, resetting the VFS to its initial empty state.
    void Clear();

private:
    std::vector<ShaderMount> m_mounts;
};

} // namespace uz
