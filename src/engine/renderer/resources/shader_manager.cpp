// shader_manager.cpp -- ShaderManager implementation.
//
// On Linux (dev container): no DXC compilation. reload() always succeeds,
// marking the shader as valid and incrementing reload_count.
// On Windows: future integration with DXC for real HLSL -> DXIL compilation.
//
// Thread safety: all public methods are protected by a uz::platform::Mutex.
// This allows reload() to be called safely from a FileWatcher callback thread.

#include "engine/renderer/resources/shader_manager.h"
#include "engine/core/platform.h"

#include <algorithm>
#include <unordered_map>

namespace uz {

struct ShaderManager::Impl {
    platform::Mutex*                               mutex = nullptr;
    std::unordered_map<std::string, ShaderInfo>     shaders;       // keyed by name
    ShaderReloadCallback                           reload_callback;
};

// RAII lock guard for uz::platform::Mutex
namespace {

class MutexGuard {
public:
    explicit MutexGuard(platform::Mutex* m) noexcept : m_(m) {
        platform::mutex_lock(m_);
    }
    ~MutexGuard() {
        platform::mutex_unlock(m_);
    }
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;
private:
    platform::Mutex* m_;
};

} // anonymous namespace

ShaderManager::ShaderManager()
    : impl_(new Impl)
{
    impl_->mutex = platform::mutex_create();
}

ShaderManager::~ShaderManager() {
    platform::mutex_destroy(impl_->mutex);
    delete impl_;
}

void ShaderManager::register_shader(const std::string& name,
                                     const std::string& vs_path,
                                     const std::string& ps_path) {
    MutexGuard lock(impl_->mutex);

    ShaderInfo info;
    info.name     = name;
    info.vs_path  = vs_path;
    info.ps_path  = ps_path;
    info.is_valid = false;
    info.reload_count = 0;

    impl_->shaders[name] = std::move(info);
}

bool ShaderManager::reload(const std::string& path) {
    MutexGuard lock(impl_->mutex);

    bool any_affected = false;

    for (auto& [name, info] : impl_->shaders) {
        if (info.vs_path != path && info.ps_path != path) {
            continue;
        }

        any_affected = true;
        info.reload_count++;

        // On Linux: no DXC -- always succeed. Clear errors, mark valid.
        info.last_errors.clear();
        info.is_valid = true;

        // Invoke callback (still under lock -- callers should keep callbacks short)
        if (impl_->reload_callback) {
            impl_->reload_callback(path, true, info.last_errors);
        }
    }

    return any_affected;
}

std::vector<ShaderError> ShaderManager::get_errors(const std::string& path) const {
    MutexGuard lock(impl_->mutex);

    for (const auto& [name, info] : impl_->shaders) {
        if (info.vs_path == path || info.ps_path == path) {
            return info.last_errors;
        }
    }

    return {};
}

std::vector<std::string> ShaderManager::list() const {
    MutexGuard lock(impl_->mutex);

    std::vector<std::string> names;
    names.reserve(impl_->shaders.size());
    for (const auto& [name, info] : impl_->shaders) {
        names.push_back(name);
    }
    // Sort for deterministic output
    std::sort(names.begin(), names.end());
    return names;
}

const ShaderInfo* ShaderManager::get_info(const std::string& name) const {
    MutexGuard lock(impl_->mutex);

    auto it = impl_->shaders.find(name);
    if (it == impl_->shaders.end()) {
        return nullptr;
    }
    return &it->second;
}

void ShaderManager::set_reload_callback(ShaderReloadCallback cb) {
    MutexGuard lock(impl_->mutex);
    impl_->reload_callback = std::move(cb);
}

} // namespace uz
