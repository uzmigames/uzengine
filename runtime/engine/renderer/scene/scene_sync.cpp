/// UzEngine Scene Renderer — ECS World synchronization (SyncWorldToDrawList).

#include "scene_renderer.h"

#include "components.h"

#include "camera.h"
#include "camera_manager.h"
#include "gpu_mesh.h"
#include "texture_loader.h"
#include "lod_selector.h"
#include "rhi.h"
#include "log.h"
#include "world.h"
#include "material_interface.h"
#include "material_pipeline.h"
#include "engine/material/material_manager.h"
#include "engine/material/material_graph.h"
#include "vfs.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

// Diagnostic counter: mesh entities found by SyncWorldToDrawList.
// Read by runtime.cpp to report in uz_frame_stats.
std::atomic<uint32_t> g_lastSyncMeshEntityCount{0};



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

    // Count entities and update global diagnostic counter.
    uint32_t meshEntityCount = 0;
    for ([[maybe_unused]] auto e : meshView) meshEntityCount++;
    g_lastSyncMeshEntityCount.store(meshEntityCount, std::memory_order_relaxed);

    for (auto [enttEntity, meshComp, sceneComp] : meshView.each()) {

        // ── Lazy-load mesh from path (LOD0) ────────────────────────────────
        if (!meshComp.meshLoaded && !meshComp.meshPath.empty()) {
            const uint32_t loaded = LoadMesh(meshComp.meshPath, vfs, cmdList);
            if (loaded != UINT32_MAX) {
                meshComp.meshId     = loaded;
                meshComp.meshLoaded = true;
                // LOD0 always uses the base meshId in the LOD group.
                meshComp.lodGroup.levels[0].meshIndex = loaded;

                // If the loaded mesh has in-file LODs (UZMH v2), populate
                // the LODGroup so screen-size selection is activated.
                if (loaded < static_cast<uint32_t>(m_meshes.size())) {
                    const auto& gpu = m_meshes[loaded];
                    UZ_LOG(Renderer, Info, "Mesh '{}' loaded: meshId={}, gpu.lodCount={}, gpu.indexCount={}",
                           meshComp.meshPath, loaded, gpu.lodCount, gpu.indexCount);

                    // Auto-compute bounding sphere radius from mesh AABB so LOD
                    // screen-size thresholds use the correct object size.
                    // Use max extent from origin (pivot-based sphere).
                    if (meshComp.boundingSphereRadius <= 0.0f) {
                        const Vec3& bMin = gpu.boundsMin;
                        const Vec3& bMax = gpu.boundsMax;
                        // Farthest AABB corner from pivot (origin of the mesh).
                        const float rx = std::max(std::abs(bMin.x), std::abs(bMax.x));
                        const float ry = std::max(std::abs(bMin.y), std::abs(bMax.y));
                        const float rz = std::max(std::abs(bMin.z), std::abs(bMax.z));
                        const float autoRadius = std::sqrt(rx * rx + ry * ry + rz * rz);
                        if (autoRadius > 0.0f) {
                            meshComp.boundingSphereRadius = autoRadius;
                            UZ_LOG(Renderer, Info, "Mesh '{}': auto-computed boundingSphereRadius={:.1f}cm",
                                   meshComp.meshPath, autoRadius);
                        }
                    }

                    if (gpu.lodCount > 1 && meshComp.lodGroup.numLevels <= 1) {
                        meshComp.lodGroup.numLevels = gpu.lodCount;
                        // All in-file LODs share the same meshIndex — the draw
                        // command uses lodIndexByteOffset/Count to select the LOD.
                        for (uint32_t li = 1; li < gpu.lodCount && li < kMaxLODs; ++li) {
                            meshComp.lodGroup.levels[li].meshIndex = loaded;
                            // Auto screen-size thresholds (industry-standard defaults)
                            constexpr float kAutoThresholds[] = {1.0f, 0.5f, 0.25f, 0.12f, 0.06f, 0.03f, 0.015f, 0.0f};
                            meshComp.lodGroup.levels[li].screenSizeThreshold =
                                (li < 8) ? kAutoThresholds[li] : 0.0f;
                        }
                        UZ_LOG(Renderer, Info, "Mesh '{}': LOD group set up — {} levels, thresholds [2.0, 0.5, 0.25, 0.12]",
                               meshComp.meshPath, gpu.lodCount);
                    }
                }
            } else {
                // Mark loaded to avoid retrying every frame
                meshComp.meshLoaded = true;
            }
        }

        // ── Lazy-load LOD meshes (LOD1..LOD7) ──────────────────────────────
        // Each non-empty lodMeshPaths entry is loaded on first encounter.
        // Successfully loaded meshes update the LODGroup's meshIndex for
        // that level so the LOD selector uses the correct geometry.
        for (uint32_t lodIdx = 0; lodIdx < kMaxLODs - 1; ++lodIdx) {
            if (!meshComp.lodMeshLoaded[lodIdx] && !meshComp.lodMeshPaths[lodIdx].empty()) {
                const uint32_t loaded = LoadMesh(meshComp.lodMeshPaths[lodIdx], vfs, cmdList);
                meshComp.lodMeshLoaded[lodIdx] = true; // Mark to avoid retrying
                if (loaded != UINT32_MAX) {
                    // lodIdx 0 in lodMeshPaths = LOD level 1 in lodGroup
                    const uint32_t lodLevel = lodIdx + 1;
                    if (lodLevel < meshComp.lodGroup.numLevels) {
                        meshComp.lodGroup.levels[lodLevel].meshIndex = loaded;
                    }
                }
            }
        }

        // ── Resolve material for slot 0 ────────────────────────────────────
        // GetMaterial(slotIndex) — resolve material for slot 0
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
        const std::string& effOrmPath        = mat ? mat->GetOrmPath()        : s_emptyPath;

        // ── Lazy-load material textures from paths ──────────────────────────
        if (!meshComp.materialLoaded && !effAlbedoPath.empty()) {
            // sRGB resolved per slot via MaterialInterface::IsTextureSlotSRGB().
            // Works for any shading model — PBR, Hair, Cloth, SSS, etc.
            auto tryLoad = [&](const std::string& slotName, const std::string& path) -> uint32_t {
                if (path.empty()) return UINT32_MAX;
                const bool isSRGB = mat ? mat->IsTextureSlotSRGB(slotName) : MaterialInterface::DefaultSlotIsSRGB(slotName);
                return LoadTexture(path, vfs, cmdList, isSRGB);
            };
            tryLoad("Albedo",    effAlbedoPath);
            tryLoad("Normal",     effNormalPath);
            tryLoad("Roughness",  effRoughnessPath);
            tryLoad("Metallic",   effMetallicPath);
            tryLoad("Ao",         effAoPath);
            tryLoad("Emissive",   effEmissivePath);
            tryLoad("ORM",        effOrmPath);
            meshComp.materialLoaded = true;
            if (mat) {
                mat->SetGpuResourcesLoaded(true);
            }
        }

        uint32_t meshId = meshComp.meshId;

        // ── LOD Selection ──────────────────────────────────────────────────
        // LOD selection — forcedLOD override or screen-size based automatic.
        //
        // When multiple LOD levels are defined, compute the object's screen
        // size from its bounding sphere and camera distance, then select the
        // appropriate LOD level. The LOD's meshIndex overrides meshId so the
        // draw call uses the correct vertex/index buffers.
        if (meshComp.lodGroup.numLevels > 1 && m_cameraManager) {
            uint32_t selectedLOD = 0;

            if (meshComp.forcedLOD >= 0) {
                // Forced LOD — bypass screen-size computation.
                selectedLOD = static_cast<uint32_t>(
                    std::min(meshComp.forcedLOD,
                             static_cast<int32_t>(meshComp.lodGroup.numLevels - 1)));
            } else {
                // Automatic LOD — compute from screen-space projected size.
                const Mat4& worldMatrix = sceneComp.GetWorldMatrix();
                const Vec3 objectPosition = worldMatrix.getTranslation();
                const Vec3& cameraPosition = m_cameraManager->GetActiveCamera().GetPosition();

                const float dx = objectPosition.x - cameraPosition.x;
                const float dy = objectPosition.y - cameraPosition.y;
                const float dz = objectPosition.z - cameraPosition.z;
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

                // Use component-specified bounding sphere radius, scaled by the
                // entity's uniform scale. The auto-computed radius is in model
                // space — multiply by max axis scale for world-space LOD selection.
                const float modelRadius = meshComp.boundingSphereRadius > 0.0f
                                          ? meshComp.boundingSphereRadius
                                          : 50.0f;
                const Vec3 scaleVec = sceneComp.GetScale();
                const float uniformScale = std::max({scaleVec.x, scaleVec.y, scaleVec.z});
                const float sphereRadius = modelRadius * uniformScale;

                // ScreenMultiple from projection matrix — computes screen size
                const Mat4& projMatrix = m_cameraManager->GetActiveCamera().GetProjection();
                const float projM00 = projMatrix.m._11;
                const float projM11 = projMatrix.m._22;

                const float screenSize = ComputeScreenSize(sphereRadius, distance,
                                                            projM00, projM11);

                selectedLOD = SelectLOD(meshComp.lodGroup, screenSize, m_lodBias);
            }

            // Update per-object LOD selection state (tracks transitions).
            meshComp.lodSelection.Update(selectedLOD);

            // For multi-LOD meshes stored in a single file (UZMH v2),
            // all LODs share the same meshId but use different index ranges.
            // Check if the selected LOD has a different meshIndex (separate file)
            // or if we should use the in-file LOD index offset.
            const uint32_t lodMeshIdx = meshComp.lodGroup.levels[selectedLOD].meshIndex;
            if (lodMeshIdx != meshComp.lodGroup.levels[0].meshIndex && lodMeshIdx != UINT32_MAX) {
                // Separate LOD mesh file — use that mesh ID
                meshId = lodMeshIdx;
            }
            // else: same mesh file with multi-LOD — set index offset below
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
        cmd.entityId     = static_cast<uint32_t>(enttEntity);

        // Multi-LOD index override: when the GPUMesh has in-file LODs (UZMH v2),
        // use the LOD's index offset/count instead of the full mesh indexCount.
        const auto& gpuMesh = m_meshes[meshId];
        if (gpuMesh.lodCount > 1 && meshComp.lodSelection.currentLOD > 0) {
            const uint32_t lod = meshComp.lodSelection.currentLOD;
            if (lod <= 3 && gpuMesh.lodInfo[lod - 1].indexCount > 0) {
                cmd.lodIndexByteOffset = gpuMesh.lodInfo[lod - 1].indexOffset;
                cmd.lodIndexCount = gpuMesh.lodInfo[lod - 1].indexCount;
            }
        }

        // Map ECS mobility to renderer-side EMobility for shadow cache filtering.
        // Stationary is treated as Static for shadow caching (standard).
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

        // ── Material Graph path (.uzmat) ───────────────────────────────────
        // When a mesh has a materialGraphPath set (from scene_loader), resolve
        // the graph via MaterialManager, compile the PSO via MaterialPipeline,
        // and set it on the draw command. The GBuffer pass will use this PSO
        // instead of the default one, calling ComputeMaterialOutputs() in the
        // shader to evaluate the graph.
        if (!meshComp.materialGraphPath.empty() && m_materialManager && m_materialPipeline) {
            // Lazy-load the graph on first encounter
            if (meshComp.materialGraphId == UINT32_MAX) {
                // Resolve VFS-relative path to absolute filesystem path
                const std::string absPath = vfs ? vfs->ResolvePath(meshComp.materialGraphPath)
                                                : meshComp.materialGraphPath;
                meshComp.materialGraphId = m_materialManager->LoadMaterial(absPath);
                if (meshComp.materialGraphId == UINT32_MAX) {
                    UZ_LOG(Renderer, Warning, "Failed to load material graph: '{}'",
                           meshComp.materialGraphPath);
                    meshComp.materialGraphPath.clear(); // Don't retry
                }
            }

            if (meshComp.materialGraphId != UINT32_MAX) {
                const auto* graph = m_materialManager->GetGraph(meshComp.materialGraphId);
                if (graph) {
                    auto pso = m_materialPipeline->GetOrCreatePSO(*graph);
                    if (pso.IsValid()) {
                        cmd.materialGraphPipeline = pso;

                        // Resolve texture SRV indices for MaterialTextureCB.
                        // Use the compile result's textureSlots (ordered by translator
                        // allocation = topological sort order) to match slot indices
                        // correctly. The defaultPath in each slot is the VFS-relative
                        // texture path from the TextureSample node.
                        const uint64_t matHash = graph->ComputeContentHash();
                        const auto* compResult = m_materialPipeline->GetCompileResult(matHash);
                        if (compResult && !compResult->textureSlots.empty()) {
                            // Ensure all material graph textures are loaded
                            for (size_t si = 0; si < compResult->textureSlots.size(); ++si) {
                                const auto& texPath = compResult->textureSlots[si].defaultPath;
                                if (!texPath.empty() && m_textureMap.find(texPath) == m_textureMap.end()) {
                                    QueueTextureLoad(texPath);
                                }
                            }
                            cmd.materialTexCount = static_cast<uint32_t>(
                                std::min(compResult->textureSlots.size(), size_t(16)));
                            for (uint32_t ti = 0; ti < cmd.materialTexCount; ++ti) {
                                const auto& texPath = compResult->textureSlots[ti].defaultPath;
                                auto texIt = m_textureMap.find(texPath);
                                if (texIt != m_textureMap.end() &&
                                    texIt->second < static_cast<uint32_t>(m_textures.size())) {
                                    cmd.materialTexIndices[ti] = m_textures[texIt->second].srvIndex;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Legacy materialId → texture mapping: only use when the component
        // explicitly set a materialId AND textures exist. Default materialId=0
        // would incorrectly bind the first loaded texture (e.g., HDRI) as albedo
        // for meshes that have no texture assigned.
        if (meshComp.materialLoaded && meshComp.materialId > 0
            && meshComp.materialId < static_cast<uint32_t>(m_textures.size())) {
            const auto& tex = m_textures[meshComp.materialId];
            cmd.albedoSrvIndex = tex.srvIndex;
            cmd.albedoTexture  = tex.handle;
        }

        // ── GPU-baked materials: use pre-set SRV indices directly ──────────
        // When a material has GPU-baked textures (e.g., from MaterialBakerGpu),
        // the SRV indices are already set on the material — use them directly
        // instead of path-based lookup. This avoids the file-based texture
        // loading path entirely.
        if (mat && mat->IsGpuResourcesLoaded()) {
            const uint32_t albSrv  = mat->GetAlbedoSrvIndex();
            const uint32_t nrmSrv  = mat->GetNormalSrvIndex();
            const uint32_t rghSrv  = mat->GetRoughnessSrvIndex();
            const uint32_t metSrv  = mat->GetMetallicSrvIndex();
            const uint32_t aoSrv   = mat->GetAoSrvIndex();
            const uint32_t emiSrv  = mat->GetEmissiveSrvIndex();
            if (albSrv > 0) cmd.albedoSrvIndex    = albSrv;
            if (nrmSrv > 0) cmd.normalSrvIndex    = nrmSrv;
            if (rghSrv > 0) cmd.roughnessSrvIndex = rghSrv;
            if (metSrv > 0) cmd.metallicSrvIndex  = metSrv;
            if (aoSrv  > 0) cmd.aoSrvIndex        = aoSrv;
            if (emiSrv > 0) cmd.emissiveSrvIndex   = emiSrv;
        }

        // Bind textures from effective paths (material system or legacy component paths).
        // Skipped for slots already set by GPU-baked materials above.
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
        bindTexFromPath(effEmissivePath,  cmd.emissiveSrvIndex,  cmd.emissiveTexture);

        // Blend mode — bits[0:1] of materialFlags.
        // 0=Opaque, 1=Masked, 2=Translucent, 3=Additive.
        // GBuffer pass skips draws where bits[0:1] >= 2 (Translucent/Additive).
        // Forward translucent pass only draws where bits[0:1] >= 2.
        if (mat) {
            const uint32_t bm = static_cast<uint32_t>(mat->GetBlendMode()) & 0x3u;
            cmd.materialFlags |= bm;
        }

        // ORM packed mode: single texture replaces separate roughness/metallic/AO.
        // Bind ORM texture to the aoIndex slot; shader unpacks R=AO, G=Roughness, B=Metallic.
        const bool isORM = mat && mat->IsORM();
        if (isORM && !effOrmPath.empty()) {
            bindTexFromPath(effOrmPath, cmd.aoSrvIndex, cmd.aoTexture);
            cmd.materialFlags |= 0x4u; // bit 2 = ORM packed
        } else {
            bindTexFromPath(effRoughnessPath, cmd.roughnessSrvIndex, cmd.roughnessTexture);
            bindTexFromPath(effMetallicPath,  cmd.metallicSrvIndex,  cmd.metallicTexture);
            bindTexFromPath(effAoPath,        cmd.aoSrvIndex,        cmd.aoTexture);
        }

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
            cmd.alphaClipThreshold  = mat->GetAlphaClipThreshold();
            cmd.uvScaleX            = mat->GetUvScaleX();
            cmd.uvScaleY            = mat->GetUvScaleY();
            cmd.uvOffsetX           = mat->GetUvOffsetX();
            cmd.uvOffsetY           = mat->GetUvOffsetY();

            mat->GetEmissiveColor(cmd.emissiveColorR, cmd.emissiveColorG, cmd.emissiveColorB);
            cmd.emissiveIntensity = mat->GetEmissiveIntensity();

            // Pack specular and envMultiplier into materialFlags unused bits.
            // bits[8:15]  = specular * 255 (range 0.0-1.0, 8-bit precision)
            // bits[16:23] = envMultiplier * 127.5 (range 0.0-2.0, 128 = 1.0)
            // @ref Lagarde 2014 "Moving Frostbite to Physically Based Rendering" — F0 = 0.08 * specular
            const uint32_t specU8 = std::min(255u, static_cast<uint32_t>(mat->GetSpecular() * 255.0f));
            const uint32_t envU8  = std::min(255u, static_cast<uint32_t>(mat->GetEnvMultiplier() * 127.5f));
            cmd.materialFlags |= (specU8 << 8) | (envU8 << 16);

            // Shading model from material — packed into materialFlags bits[24:27].
            cmd.shadingModel = mat->GetShadingModel();
            const uint32_t smId = static_cast<uint32_t>(mat->GetShadingModel()) & 0xFu;
            cmd.materialFlags |= (smId << 24);

            // bits[28:31] — context-dependent (4-bit, 16 levels):
            //   smID==4 (ClearCoat):      clearCoatRoughness * 15
            //   smID==9 (ThinTranslucent): transmission * 15
            //   other:                    clearCoatRoughness * 15 (default)
            if (mat->GetShadingModel() == EShadingModel::ThinTranslucent) {
                const uint32_t transU4 = std::min(15u, static_cast<uint32_t>(mat->GetScalarParam("Transmission", 0.0f) * 15.0f));
                cmd.materialFlags |= (transU4 << 28);
            } else {
                const uint32_t ccRoughU4 = std::min(15u, static_cast<uint32_t>(mat->GetClearCoatRoughness() * 15.0f));
                cmd.materialFlags |= (ccRoughU4 << 28);
            }

            // Pack twoSided into materialFlags bit 3 (PSO-level only, not read in shader).
            if (mat->IsTwoSided()) {
                cmd.materialFlags |= 0x8u; // bit 3
            }


            // Opacity texture SRV index — used for alpha mask sampling in the shader.
            cmd.opacitySrvIndex = mat->GetOpacitySrvIndex();

            // Subsurface scattering parameters (only read for SSS shading models).
            mat->GetSubsurfaceColor(cmd.subsurfaceColorR, cmd.subsurfaceColorG, cmd.subsurfaceColorB);
            cmd.subsurfaceRadius = mat->GetSubsurfaceRadius();

            // Clear coat parameters (only meaningful for ClearCoat shading model).
            cmd.clearCoat          = mat->GetClearCoat();
            cmd.clearCoatRoughness = mat->GetClearCoatRoughness();
        } else {
            // No material assigned — use neutral defaults (no emission).
            cmd.emissiveColorR    = 0.0f;
            cmd.emissiveColorG    = 0.0f;
            cmd.emissiveColorB    = 0.0f;
            cmd.emissiveIntensity = 0.0f;

            // Default specular=0.5 (F0=0.04) and envMultiplier=1.0 for unassigned materials.
            constexpr uint32_t kDefaultSpecU8 = 128; // 0.5 * 255 ≈ 128
            constexpr uint32_t kDefaultEnvU8  = 128; // 1.0 * 127.5 ≈ 128
            cmd.materialFlags |= (kDefaultSpecU8 << 8) | (kDefaultEnvU8 << 16);
        }

        outDrawList.Add(cmd);
        drawIndex++;
    }

    // ── Preload decal textures from DecalComponent materials ─────────────
    // DecalComponent references a MaterialInterface* whose texture paths need
    // to be loaded into m_textureMap so the decal gather can resolve → SRV.
    {
        auto decalView = world->GetRegistry().view<DecalComponent>();
        for (auto [enttEntity, decal] : decalView.each()) {
            if (!decal.enabled || !decal.material) continue;
            const auto* mat = decal.material;
            auto tryLoadDecalTex = [&](const std::string& path, bool srgb) {
                if (path.empty()) return;
                if (m_textureMap.find(path) != m_textureMap.end()) return;
                LoadTexture(path, vfs, cmdList, srgb);
            };
            tryLoadDecalTex(mat->GetAlbedoPath(),    true);
            tryLoadDecalTex(mat->GetNormalPath(),     false);
            tryLoadDecalTex(mat->GetRoughnessPath(),  false);
            tryLoadDecalTex(mat->GetMetallicPath(),   false);
            tryLoadDecalTex(mat->GetTexturePath("Opacity"), false);
        }
    }

    // ── Flush batched mesh uploads ───────────────────────────────────────
    // All meshes loaded during this sync pass were deferred — submit all
    // copy commands in a single GPU submission instead of one per mesh.
    if (!m_deferredMeshUploads.empty()) {
        FlushDeferredMeshUploads(m_device, cmdList, m_deferredMeshUploads);
        UZ_LOG(Renderer, Info, "SyncWorldToDrawList: flushed {} deferred mesh uploads",
               m_deferredMeshUploads.size());
        m_deferredMeshUploads.clear();
    }

    // ── Batch SDF bake for queued meshes ─────────────────────────────────
    if (!m_pendingSdfBakes.empty()) {
        FlushSdfBakes(cmdList);
    }
}

} // namespace uz
