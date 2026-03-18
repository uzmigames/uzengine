/// UzEngine Scene Renderer — ECS World synchronization (SyncWorldToDrawList).

#include "scene_renderer.h"

#include "components.h"

#include "camera.h"
#include "gpu_mesh.h"
#include "texture_loader.h"
#include "lod_selector.h"
#include "rhi.h"
#include "log.h"
#include "world.h"
#include "material_interface.h"

#include <cmath>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SyncWorldToDrawList
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::SyncWorldToDrawList(World* world, IVFS* vfs,
                                         IRHICommandList* cmdList,
                                         DrawList& outDrawList) {
    if (!world) return;

    uint32_t drawIndex = 0;

    // Use mutable registry view — we need to write meshLoaded/materialLoaded flags
    // back into the components after lazy-loading assets.
    auto meshView = world->GetRegistry().view<StaticMeshComponent, SceneComponent>();

    for (auto [enttEntity, meshComp, sceneComp] : meshView.each()) {

        // ── Lazy-load mesh from path ────────────────────────────────────────
        if (!meshComp.meshLoaded && !meshComp.meshPath.empty()) {
            const uint32_t loaded = LoadMesh(meshComp.meshPath, vfs, cmdList);
            if (loaded != UINT32_MAX) {
                meshComp.meshId     = loaded;
                meshComp.meshLoaded = true;
            } else {
                // Mark loaded to avoid retrying every frame
                meshComp.meshLoaded = true;
            }
        }

        // ── Resolve material for slot 0 ────────────────────────────────────
        // @source UE5 MeshComponent.cpp:37-54 — GetMaterial(slotIndex)
        // All texture paths and parameters come from the MaterialInterface.
        // If no material is assigned, the mesh renders with default fallback textures.
        MaterialInterface* const mat = meshComp.GetMaterial(0);

        // Empty fallback for meshes with no material assigned.
        static const std::string s_emptyPath;

        const std::string& effAlbedoPath    = mat ? mat->GetAlbedoPath()    : s_emptyPath;
        const std::string& effNormalPath     = mat ? mat->GetNormalPath()     : s_emptyPath;
        const std::string& effRoughnessPath  = mat ? mat->GetRoughnessPath()  : s_emptyPath;
        const std::string& effMetallicPath   = mat ? mat->GetMetallicPath()   : s_emptyPath;
        const std::string& effAoPath         = mat ? mat->GetAoPath()         : s_emptyPath;
        const std::string& effEmissivePath   = mat ? mat->GetEmissivePath()   : s_emptyPath;

        // ── Lazy-load material textures from paths ──────────────────────────
        if (!meshComp.materialLoaded && !effAlbedoPath.empty()) {
            auto tryLoad = [&](const std::string& path) -> uint32_t {
                if (path.empty()) return UINT32_MAX;
                return LoadTexture(path, vfs, cmdList);
            };
            tryLoad(effAlbedoPath);
            tryLoad(effNormalPath);
            tryLoad(effRoughnessPath);
            tryLoad(effMetallicPath);
            tryLoad(effAoPath);
            tryLoad(effEmissivePath);
            meshComp.materialLoaded = true;
            if (mat) {
                mat->SetGpuResourcesLoaded(true);
            }
        }

        uint32_t meshId = meshComp.meshId;

        // ── LOD Selection ──────────────────────────────────────────────────
        // @source UE5 StaticMeshSceneProxy.cpp:2661-2663 — GetLOD()
        // @source UE5 SceneManagement.cpp:1034-1055 — ComputeStaticMeshLOD
        //
        // When multiple LOD levels are defined, compute the object's screen
        // size from its bounding sphere and camera distance, then select the
        // appropriate LOD level. The LOD's meshIndex overrides meshId so the
        // draw call uses the correct vertex/index buffers.
        if (meshComp.lodGroup.numLevels > 1 && m_camera) {
            const Mat4& worldMatrix = sceneComp.GetWorldMatrix();
            const Vec3 objectPosition = worldMatrix.getTranslation();
            const Vec3& cameraPosition = m_camera->GetPosition();

            const float dx = objectPosition.x - cameraPosition.x;
            const float dy = objectPosition.y - cameraPosition.y;
            const float dz = objectPosition.z - cameraPosition.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            // Use component-specified bounding sphere radius, or default to
            // a unit sphere (50 cm radius) if none was set.
            const float sphereRadius = meshComp.boundingSphereRadius > 0.0f
                                       ? meshComp.boundingSphereRadius
                                       : 50.0f;

            // @source UE5 SceneManagement.cpp:977 — ScreenMultiple from projection matrix
            const Mat4& projMatrix = m_camera->GetProjection();
            const float projM00 = projMatrix.m._11;
            const float projM11 = projMatrix.m._22;

            const float screenSize = ComputeScreenSize(sphereRadius, distance,
                                                        projM00, projM11);

            const uint32_t selectedLOD = SelectLOD(meshComp.lodGroup, screenSize,
                                                    m_lodBias);

            // Update per-object LOD selection state (tracks transitions).
            meshComp.lodSelection.Update(selectedLOD);

            // Use the LOD level's mesh index for rendering.
            meshId = meshComp.lodGroup.levels[selectedLOD].meshIndex;
        }

        // Validate mesh index is within loaded meshes
        if (m_meshes.empty()) continue;
        if (meshId >= static_cast<uint32_t>(m_meshes.size())) continue;
        if (!m_meshes[meshId].IsValid()) continue;

        DrawCommand cmd{};
        cmd.sortKey      = SortKey::Opaque(0, 0, 0.f, drawIndex);
        cmd.worldMatrix  = sceneComp.GetWorldMatrix();
        cmd.normalMatrix = cmd.worldMatrix.NormalMatrix();
        cmd.meshId       = meshId;

        // Map ECS mobility to renderer-side EMobility for shadow cache filtering.
        // Stationary is treated as Static for shadow caching (UE5 parity).
        switch (sceneComp.mobility) {
            case SceneComponent::Mobility::Static:
                cmd.mobility = EMobility::Static;
                break;
            case SceneComponent::Mobility::Stationary:
                cmd.mobility = EMobility::Stationary;
                break;
            case SceneComponent::Mobility::Movable:
            default:
                cmd.mobility = EMobility::Movable;
                break;
        }

        // Map materialId to texture SRV indices if textures are loaded
        if (!m_textures.empty() && meshComp.materialId < static_cast<uint32_t>(m_textures.size())) {
            const auto& tex = m_textures[meshComp.materialId];
            cmd.albedoSrvIndex = tex.srvIndex;
            cmd.albedoTexture  = tex.handle;
        }

        // Bind textures from effective paths (material system or legacy component paths).
        auto bindTexFromPath = [&](const std::string& path, uint32_t& srvIndex, RHITextureHandle& handle) {
            if (path.empty()) return;
            auto texIt = m_textureMap.find(path);
            if (texIt != m_textureMap.end() && texIt->second < static_cast<uint32_t>(m_textures.size())) {
                const auto& tex = m_textures[texIt->second];
                srvIndex = tex.srvIndex;
                handle   = tex.handle;
            }
        };
        bindTexFromPath(effAlbedoPath,    cmd.albedoSrvIndex,    cmd.albedoTexture);
        bindTexFromPath(effNormalPath,    cmd.normalSrvIndex,    cmd.normalTexture);
        bindTexFromPath(effRoughnessPath, cmd.roughnessSrvIndex, cmd.roughnessTexture);
        bindTexFromPath(effMetallicPath,  cmd.metallicSrvIndex,  cmd.metallicTexture);
        bindTexFromPath(effAoPath,        cmd.aoSrvIndex,        cmd.aoTexture);
        bindTexFromPath(effEmissivePath,  cmd.emissiveSrvIndex,  cmd.emissiveTexture);

        // ── Material parameters ─────────────────────────────────────────────
        // Read from MaterialInterface if available, otherwise use legacy
        // component fields for backward compatibility.
        if (mat) {
            mat->GetBaseColorTint(cmd.baseColorTintR, cmd.baseColorTintG, cmd.baseColorTintB);
            cmd.roughnessMultiplier = mat->GetRoughnessMultiplier();
            cmd.metallicMultiplier  = mat->GetMetallicMultiplier();
            cmd.normalIntensity     = mat->GetNormalIntensity();
            cmd.aoIntensity         = mat->GetAoIntensity();
            cmd.opacity             = mat->GetOpacity();
            cmd.uvScaleX            = mat->GetUvScaleX();
            cmd.uvScaleY            = mat->GetUvScaleY();
            cmd.uvOffsetX           = mat->GetUvOffsetX();
            cmd.uvOffsetY           = mat->GetUvOffsetY();

            mat->GetEmissiveColor(cmd.emissiveColorR, cmd.emissiveColorG, cmd.emissiveColorB);
            cmd.emissiveIntensity = mat->GetEmissiveIntensity();
        } else {
            // No material assigned — use neutral defaults (no emission).
            cmd.emissiveColorR    = 0.0f;
            cmd.emissiveColorG    = 0.0f;
            cmd.emissiveColorB    = 0.0f;
            cmd.emissiveIntensity = 0.0f;
        }

        outDrawList.Add(cmd);
        drawIndex++;
    }
}

} // namespace uz
