#include "engine/renderer/resources/material_instance.h"
#include "engine/core/diag.h"

#include <mutex>
#include <unordered_map>

namespace uz {

// ============================================================================
// MaterialInstanceManager::Impl
// ============================================================================

struct MaterialInstanceManager::Impl {
    std::unordered_map<uint32_t, MaterialInstance*> instances;
    std::unordered_map<std::string, uint32_t>      name_index;
    uint32_t next_id = 1;
    mutable std::mutex mutex;
};

MaterialInstanceManager::MaterialInstanceManager()
    : impl_(new Impl()) {}

MaterialInstanceManager::~MaterialInstanceManager() {
    clear();
    delete impl_;
}

MaterialInstance* MaterialInstanceManager::create(const std::string& name,
                                                   Material* parent) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    // Check for duplicate name.
    auto name_it = impl_->name_index.find(name);
    if (name_it != impl_->name_index.end()) {
        DIAG_FMT("MaterialInstanceManager: instance '%s' already exists (id=%u)",
                 name.c_str(), name_it->second);
        return impl_->instances[name_it->second];
    }

    auto* inst   = new MaterialInstance();
    inst->id     = impl_->next_id++;
    inst->name   = name;
    inst->parent = parent;

    impl_->instances[inst->id] = inst;
    impl_->name_index[name]    = inst->id;

    DIAG_FMT("MaterialInstanceManager: created '%s' (id=%u, parent='%s')",
             name.c_str(), inst->id,
             parent ? parent->name.c_str() : "<null>");
    return inst;
}

MaterialInstance* MaterialInstanceManager::get(uint32_t id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->instances.find(id);
    return (it != impl_->instances.end()) ? it->second : nullptr;
}

MaterialInstance* MaterialInstanceManager::find(const std::string& name) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->name_index.find(name);
    if (it == impl_->name_index.end()) return nullptr;
    auto inst_it = impl_->instances.find(it->second);
    return (inst_it != impl_->instances.end()) ? inst_it->second : nullptr;
}

void MaterialInstanceManager::destroy(uint32_t id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->instances.find(id);
    if (it == impl_->instances.end()) return;

    impl_->name_index.erase(it->second->name);
    delete it->second;
    impl_->instances.erase(it);
}

const std::unordered_map<uint32_t, MaterialInstance*>&
MaterialInstanceManager::all() const {
    return impl_->instances;
}

void MaterialInstanceManager::set_texture_override(MaterialInstance* inst,
                                                     const std::string& key,
                                                     const std::string& asset_path) {
    if (!inst) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);

    inst->texture_overrides[key] = asset_path;

    // Queue async texture load via the unified TextureManager.
    // texture_get_or_queue() returns a placeholder handle immediately; the real
    // data replaces it in-place via texture_tick() (zero rebinding needed).
    RhiTexture handle = texture_get_or_queue(asset_path);
    inst->texture_handle_overrides[key] = handle;

    DIAG_FMT("MaterialInstanceManager: texture override '%s' = '%s' (handle=%llu)",
             key.c_str(), asset_path.c_str(),
             static_cast<unsigned long long>(handle));
}

void MaterialInstanceManager::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto& [id, inst] : impl_->instances) {
        delete inst;
    }
    impl_->instances.clear();
    impl_->name_index.clear();
}

uint32_t MaterialInstanceManager::count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<uint32_t>(impl_->instances.size());
}

} // namespace uz
