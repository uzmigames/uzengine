/// StaticMeshComponent — renders a static mesh with a material.
///
/// Stores opaque resource indices into the engine's mesh and material tables.
/// In MVP these indices are set manually by game code. Phase 2 will wire
/// them to the asset system's handle types.

#pragma once

#include "primitive_component.h"

#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// StaticMeshComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Renders a static (non-skinned) mesh with a single material.
///
/// `meshId` and `materialId` are opaque resource indices managed by the asset
/// system. Set them after construction to point to the desired mesh and
/// material. The indices are pushed into the render proxy via `OnProxyCreated()`.
///
/// # MVP Note
/// Resource handles are `uint32_t` indices until the asset system defines
/// typed handle wrappers. Default values (0, 0) are sentinel "null" resources.
///
/// # Examples
/// ```cpp
/// auto& mesh = entity.AddComponent<StaticMeshComponent>();
/// mesh.meshId     = assetSystem.LoadMesh("cube.uzasset");
/// mesh.materialId = assetSystem.LoadMaterial("red.uzmat");
/// mesh.SetPosition({5.f, 0.f, 0.f});
/// ```
class StaticMeshComponent : public PrimitiveComponent {
public:
    /// Opaque index into the engine mesh resource table.
    ///
    /// 0 is a sentinel "null mesh" value. The proxy will be created but
    /// the renderer will skip draw calls for null meshes.
    uint32_t meshId = 0;

    /// Opaque index into the engine material resource table.
    ///
    /// 0 is a sentinel "null material" value. The renderer will use a
    /// default error material for null material indices.
    uint32_t materialId = 0;

    // ── Asset Paths (populated by scene loader, consumed by SceneRenderer) ──

    /// VFS-relative path to the mesh asset (e.g., "meshes/cube.gltf").
    ///
    /// Empty string means no mesh asset — `meshId` must be set manually.
    /// When non-empty, `SceneRenderer::SyncWorldToDrawList()` will lazy-load
    /// the mesh on first encounter and set `meshId` + `meshLoaded`.
    std::string meshPath;

    /// Set to `true` by `SceneRenderer` after the mesh has been uploaded to the GPU.
    bool meshLoaded = false;

    /// VFS-relative path to the albedo texture (e.g., "textures/brick_albedo.png").
    std::string albedoPath;

    /// VFS-relative path to the normal map texture.
    std::string normalPath;

    /// VFS-relative path to the roughness texture.
    std::string roughnessPath;

    /// VFS-relative path to the metallic texture.
    std::string metallicPath;

    /// VFS-relative path to the ambient occlusion texture.
    std::string aoPath;

    /// Set to `true` by `SceneRenderer` after all material textures have been uploaded.
    bool materialLoaded = false;

protected:
    /// Returns `RenderProxyType::StaticMesh`.
    [[nodiscard]] RenderProxyType GetProxyType() const override {
        return RenderProxyType::StaticMesh;
    }

    /// Writes `meshId` and `materialId` into the newly created proxy.
    ///
    /// # Parameters
    /// - `renderWorld`: The `RenderWorld` that owns the proxy.
    /// - `handle`: Freshly created proxy handle. Guaranteed valid.
    void OnProxyCreated(RenderWorld& renderWorld, RenderProxyHandle handle) override;
};

} // namespace uz
