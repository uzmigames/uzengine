/// StaticMeshComponent — renders a static mesh with a material.
///
/// Stores opaque resource indices into the engine's mesh and material tables.
/// In MVP these indices are set manually by game code. Phase 2 will wire
/// them to the asset system's handle types.

#pragma once

#include "primitive_component.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class MaterialInterface;

// ─────────────────────────────────────────────────────────────────────────────
// StaticMeshComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Renders a static (non-skinned) mesh with per-slot materials.
///
/// # Material System
/// @source UE5 MeshComponent.h:31 — OverrideMaterials
/// Each mesh can have multiple sections, each with its own material slot.
/// `overrideMaterials` is a sparse array of `MaterialInterface*` pointers —
/// null entries fall back to the mesh's default material, then to the legacy
/// path-based system for backward compatibility.
///
/// ```cpp
/// // New material system (preferred):
/// Material stoneBrick;
/// stoneBrick.albedoPath = "Stonebricks_BaseColor.jpg";
/// mesh.overrideMaterials.push_back(&stoneBrick);
///
/// // Legacy path system (still works, lowest priority):
/// mesh.albedoPath = "Stonebricks_BaseColor.jpg";
/// ```
///
/// # Priority Order (per slot)
/// 1. `overrideMaterials[slot]` if non-null
/// 2. Legacy component texture paths (albedoPath, normalPath, etc.)
///
/// `meshId` and `materialId` are opaque resource indices managed by the asset
/// system. Set them after construction to point to the desired mesh and
/// material. The indices are pushed into the render proxy via `OnProxyCreated()`.
///
/// # MVP Note
/// Resource handles are `uint32_t` indices until the asset system defines
/// typed handle wrappers. Default values (0, 0) are sentinel "null" resources.
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

    /// VFS-relative path to the emissive texture.
    std::string emissivePath;

    /// Emissive tint color (linear RGB). Default (0,0,0) = no emission.
    float emissiveColorR  = 0.0f;
    float emissiveColorG  = 0.0f;
    float emissiveColorB  = 0.0f;

    /// Emissive brightness multiplier. 0 = no emission. Values > 1.0 drive bloom.
    float emissiveIntensity = 0.0f;

    /// Set to `true` by `SceneRenderer` after all material textures have been uploaded.
    bool materialLoaded = false;

    // ── Material Slot System ─────────────────────────────────────────────

    /// Per-slot material overrides. Each entry corresponds to a mesh section.
    ///
    /// @source UE5 MeshComponent.h:31 — OverrideMaterials
    /// Sparse array: null entries use legacy path-based fallback (albedoPath, etc.).
    /// Non-owning pointers — the caller is responsible for Material/MaterialInstance
    /// lifetime (typically static or scene-lifetime objects).
    ///
    /// For single-material meshes, only slot 0 is used.
    std::vector<MaterialInterface*> overrideMaterials;

    /// Returns the material for a specific slot.
    ///
    /// @source UE5 MeshComponent.cpp:37-54 — GetMaterial(int32_t)
    /// Returns the override material if set, or nullptr (caller should use
    /// legacy path-based fallback).
    ///
    /// # Parameters
    /// - `slotIndex`: Zero-based material slot index.
    ///
    /// # Returns
    /// The `MaterialInterface*` for the slot, or nullptr if the slot index is
    /// out of range or the entry is null.
    [[nodiscard]] MaterialInterface* GetMaterial(int32_t slotIndex) const noexcept {
        if (slotIndex < 0) return nullptr;
        const auto idx = static_cast<size_t>(slotIndex);
        if (idx >= overrideMaterials.size()) return nullptr;
        return overrideMaterials[idx];
    }

    /// Returns the number of material slots.
    [[nodiscard]] int32_t GetNumMaterialSlots() const noexcept {
        return static_cast<int32_t>(overrideMaterials.size());
    }

    /// Sets a material for a specific slot, growing the array if necessary.
    ///
    /// # Parameters
    /// - `slotIndex`: Zero-based slot index. The array grows to accommodate.
    /// - `material`: Material to assign (may be null to clear the slot).
    void SetMaterial(int32_t slotIndex, MaterialInterface* material) {
        if (slotIndex < 0) return;
        const auto idx = static_cast<size_t>(slotIndex);
        if (idx >= overrideMaterials.size()) {
            overrideMaterials.resize(idx + 1, nullptr);
        }
        overrideMaterials[idx] = material;
    }

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
