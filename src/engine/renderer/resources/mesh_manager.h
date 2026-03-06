#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "engine/renderer/resources/mesh.h"

namespace uz {

// ============================================================================
// MeshManager — loading, caching, and management of mesh assets.
// ============================================================================

class MeshManager {
public:
    MeshManager();
    ~MeshManager();

    // Non-copyable.
    MeshManager(const MeshManager&) = delete;
    MeshManager& operator=(const MeshManager&) = delete;

    /// Load a mesh from a .gltf or .glb file. Returns a pointer to the cached
    /// GPU mesh, or nullptr if loading fails.
    /// Note: A single GLTF can contain multiple meshes; this returns the first one.
    GpuMesh* load(const std::string& path);

    /// Unload all meshes.
    void clear();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uz
