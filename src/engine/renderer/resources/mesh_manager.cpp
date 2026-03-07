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

    // Populate default material slots from imported glTF materials.
    for (const auto& imat : model.materials) {
        DefaultMaterialSlot slot;
        slot.name = imat.name;
        slot.albedo[0] = imat.albedo[0];
        slot.albedo[1] = imat.albedo[1];
        slot.albedo[2] = imat.albedo[2];
        slot.metallic  = imat.metallic;
        slot.roughness = imat.roughness;
        slot.emission[0] = imat.emission[0];
        slot.emission[1] = imat.emission[1];
        slot.emission[2] = imat.emission[2];
        slot.emission_energy = imat.emission_energy;

        // Resolve texture paths relative to source file directory.
        auto resolve = [&](const std::string& rel) -> std::string {
            if (rel.empty()) return {};
            if (rel.size() >= 2 && rel[1] == ':') return rel;
            return model.base_dir + rel;
        };
        slot.albedo_texture             = resolve(imat.albedo_texture);
        slot.normal_texture             = resolve(imat.normal_texture);
        slot.metallic_roughness_texture = resolve(imat.metallic_roughness_texture);
        slot.ao_texture                 = resolve(imat.ao_texture);
        slot.emission_texture           = resolve(imat.emission_texture);

        gpuMesh.default_materials.push_back(std::move(slot));
    }

    DIAG_FMT("MeshManager: loaded '%s' (%u verts, %zu submeshes, %zu material slots)",
             path.c_str(), gpuMesh.vertex_count,
             gpuMesh.submeshes.size(), gpuMesh.default_materials.size());

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
