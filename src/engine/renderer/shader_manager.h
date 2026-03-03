#pragma once

// shader_manager.h -- In-memory shader file cache with hot-reload support.
//
// On Linux (dev container): no DXC compilation. ShaderManager tracks files
// and simulates compilation state -- reload always succeeds with is_valid=true.
// On Windows: integrates with DXC for real HLSL -> DXIL compilation (future).
//
// Thread safety: all public methods are protected by an internal mutex, so
// reload() can safely be called from a FileWatcher callback thread.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

struct ShaderError {
    std::string file;
    int         line   = 0;
    int         column = 0;
    std::string message;
};

struct ShaderInfo {
    std::string              name;
    std::string              vs_path;
    std::string              ps_path;
    std::vector<ShaderError> last_errors;
    bool                     is_valid     = false;
    uint64_t                 reload_count = 0;
};

using ShaderReloadCallback =
    std::function<void(const std::string& path,
                       bool success,
                       const std::vector<ShaderError>& errors)>;

class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();

    // Non-copyable
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    /// Register a shader by name with its VS and PS source paths.
    /// If the name already exists it is overwritten.
    void register_shader(const std::string& name,
                         const std::string& vs_path,
                         const std::string& ps_path);

    /// Trigger reload for a specific file path (called from FileWatcher callback).
    /// Returns true if any registered shader references the given path.
    bool reload(const std::string& path);

    /// Get errors for a specific file path.
    /// Returns an empty vector if the path is not referenced or has no errors.
    [[nodiscard]] std::vector<ShaderError> get_errors(const std::string& path) const;

    /// List all registered shader names.
    [[nodiscard]] std::vector<std::string> list() const;

    /// Get info for a specific shader by name. Returns nullptr if not found.
    [[nodiscard]] const ShaderInfo* get_info(const std::string& name) const;

    /// Set callback invoked after every reload attempt.
    void set_reload_callback(ShaderReloadCallback cb);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uz
