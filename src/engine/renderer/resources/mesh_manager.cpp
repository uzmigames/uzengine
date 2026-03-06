#include "mesh_manager.h"
#include "gltf_importer.h"
#include "engine/core/diag.h"
#include <mutex>

namespace uz {

struct MeshManager::Impl {
    std::unordered_map<std::string, GpuMesh> cache;
    std::mutex mutex;
};

MeshManager::MeshManager() : impl_(new Impl()) {}
MeshManager::~MeshManager() {
    clear();
    delete impl_;
}

GpuMesh* MeshManager::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    auto it = impl_->cache.find(path);
    if (it != impl_->cache.end()) {
        return &it->second;
    }

    DIAG_FMT("MeshManager: loading '%s'...", path.c_str());
    ImportedModel model = import_gltf(path.c_str());
    if (model.meshes.empty()) {
        DIAG_FMT("MeshManager: failed to load '%s'", path.c_str());
        return nullptr;
    }

    // Create GPU mesh for the first mesh in the model.
    GpuMesh gpuMesh = create_gpu_mesh(model.meshes[0]);
    if (gpuMesh.vertex_buffer == RHI_NULL) {
        DIAG_FMT("MeshManager: failed to create GPU mesh for '%s'", path.c_str());
        return nullptr;
    }

    impl_->cache[path] = std::move(gpuMesh);
    return &impl_->cache[path];
}

void MeshManager::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto& [path, mesh] : impl_->cache) {
        destroy_gpu_mesh(mesh);
    }
    impl_->cache.clear();
}

} // namespace uz
