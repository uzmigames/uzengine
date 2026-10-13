/// UzEngine Scene Renderer — Shadow passes: CSM two-pass cache-aware rendering
/// + local light shadow depth.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "components.h"

#include "camera.h"
#include "gpu_mesh.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"

#include <cstring>

namespace uz {

static auto* cv_ShadowFreeze = UZ_CVAR_INT("r.Shadow.FreezeAfterFrame", -1,
    "Diagnostic: stop shadow atlas updates and hold matching cascade constants after this frame.", -1, 100000);

/// Per-cascade shadow constant buffer — mirrors the one in shadow_pass.cpp.
/// Contains only the light-space view-projection matrix, bound at root slot 3.
struct ShadowCB {
    Mat4  viewProj;
    float lightPosX = 0.f, lightPosY = 0.f, lightPosZ = 0.f; ///< Local tiles: the light the linear depth is measured from.
    float lightRadius = 0.f;                                 ///< Local tiles: distance / radius is what the tile stores.
};

void SceneRenderer::RegisterShadowPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    if (!m_showFlags.Get(ShowFlag::Shadows)) return;
    if (cv_ShadowFreeze->value >= 0 && static_cast<int>(ctx.frameIndex) > cv_ShadowFreeze->value) {
        if (static_cast<int>(ctx.frameIndex) == cv_ShadowFreeze->value + 1) {
            UZ_LOG(Renderer, Info, "ShadowFreeze active frame={} frozen_after={}", ctx.frameIndex, cv_ShadowFreeze->value);
        }
        return;
    }
    UZ_PROFILE_FUNCTION();

    const bool bindless = ctx.bindless;
    auto* pipelines = ctx.pipelines;
    const auto& cascades = ctx.shadowFrame.cascades;
    const uint32_t totalCascadeCount = ctx.totalCascadeCount;

    // ── Shadow pass(es) — two-pass cache-aware rendering ───────────────
    // When shadow caching is active and the static shadow atlas is available:
    //   Pass 1 (conditional): Render static/stationary geometry into the
    //     static shadow atlas. Only executes when the cache is invalidated
    //     (light direction change, cascade parameter change, etc.).
    //   Pass 2 (every frame): Render only movable geometry into the dynamic
    //     shadow atlas. The pixel shader composites both atlases.
    //
    // When shadow caching is disabled or the static atlas is unavailable,
    // a single uncached pass renders all geometry into the dynamic atlas
    // (backward compatible behavior).
    //
    // EShadowDepthCacheMode — determines static vs dynamic shadow rendering
    // No directional light: no cascades to render (the lighting shader reads cascadeCount 0 and
    // the sun colour is zero). Local light shadows below do not depend on the sun.
    if (!m_meshes.empty() && ctx.lightData.hasDirectionalLight) {
        // Use the shadow cache status computed earlier (before SetupShadowFrame).
        const bool shadowCacheActive = ctx.shadowCacheEnabled;

        // Stable up vector for cache key construction (Z-up world up).
        const Vec3 lightUp = Vec3{0.f, 0.f, 1.f};

        // Query the cache manager for each cascade.
        // Track per-cascade modes for CSM scrolling support.
        // EShadowDepthCacheMode — determines static vs dynamic shadow rendering
        bool needStaticRerender = false;
        bool anyScrolling = false;
        ShadowCacheMode cascadeModes[kMaxShadowCascades]{};
        if (shadowCacheActive) {
            const uint32_t lightId = 0; // Primary directional light
            for (uint32_t i = 0; i < totalCascadeCount; ++i) {
                cascadeModes[i] = m_shadowCacheManager.UpdateCache(
                    lightId, i, cascades[i], ctx.sunDir, lightUp,
                    m_shadowAtlas.GetResolution(), ctx.frameIndex);
                if (cascadeModes[i] == ShadowCacheMode::StaticPrimitivesOnly
                    || cascadeModes[i] == ShadowCacheMode::Uncached) {
                    needStaticRerender = true;
                }
                if (cascadeModes[i] == ShadowCacheMode::CSMScrolling) {
                    anyScrolling = true;
                }
            }
        }

        // Common fields shared between shadow pass descriptors.
        auto fillCommonShadowDesc = [&](ShadowPassDesc& desc) {
            desc.drawList             = &ctx.drawList;
            desc.cbAllocator          = m_cbRing.get();
            desc.pipeline             = pipelines->GetShadowDepth(bindless);
            desc.meshes               = m_meshes.data();
            desc.meshCount            = static_cast<uint32_t>(m_meshes.size());
            desc.bindless             = bindless;
            for (uint32_t ci = 0; ci < totalCascadeCount; ++ci) {
                desc.cascades[ci] = cascades[ci];
            }
            desc.cascadeCount     = totalCascadeCount;
            desc.atlasResolution  = m_shadowAtlas.GetResolution();
            desc.farShadowLodBias = ctx.shadowFrame.config.farShadowLodBias;
        };

        if (shadowCacheActive) {
            // ── CSM Scrolling: GPU copy overlap + render edge strips ──
            // When UpdateCache returns CSMScrolling for any cascade, we need to
            // copy the overlapping region of the cached atlas to the correct
            // position (accounting for camera movement), then render only the
            // newly exposed edge strips.
            //
            // CopyCachedShadowMap — GPU copy of overlapping region
            // Scrolling data — UV regions + culling planes for edge strips
            if (anyScrolling) {
                const uint32_t lightId = 0;

                // Import the static shadow atlas for GPU copy operations
                auto staticAtlasRes = rgBuilder.ImportTrackedTexture(
                    "StaticShadowAtlas", m_staticShadowAtlas);

                // Register a render graph pass that copies the overlapping region
                // and renders edge-strip static primitives.
                // Capture by value since the lambda outlives this scope.
                ShadowPassDesc scrollingDesc{};
                scrollingDesc.shadowAtlas        = staticAtlasRes;
                scrollingDesc.shadowAtlasTexture = m_staticShadowAtlas;
                scrollingDesc.cacheMode          = ShadowCacheMode::CSMScrolling;
                fillCommonShadowDesc(scrollingDesc);

                // Store per-cascade scrolling data and modes in the desc for the pass.
                // The CSMScrolling pass uses CopyTexture for the overlap region and
                // renders only edge-strip primitives based on culling planes.
                auto* shadowCacheMgr = &m_shadowCacheManager;
                const auto staticAtlasTex = m_staticShadowAtlas;

                rgBuilder.AddPass("ShadowCSMScrolling", [scrollingDesc, shadowCacheMgr, lightId,
                                                          cascadeModes, staticAtlasTex](rg::RgPassBuilder& pb) {
                    pb.Write(scrollingDesc.shadowAtlas, RHIResourceState::DepthStencilWrite);

                    pb.SetExecutionCallback([scrollingDesc, shadowCacheMgr, lightId,
                                             cascadeModes, staticAtlasTex](
                                                IRHICommandList* cmd, const rg::RgPassResources& res) {
                        UZ_PROFILE_SCOPE("ShadowCSMScrolling::Execute");

                        const uint32_t resolution = scrollingDesc.atlasResolution;
                        const auto& commands = scrollingDesc.drawList->GetCommands();

                        for (uint32_t cascade = 0; cascade < scrollingDesc.cascadeCount; ++cascade) {
                            if (cascadeModes[cascade] != ShadowCacheMode::CSMScrolling) {
                                continue;
                            }

                            const auto& scrollData = shadowCacheMgr->GetScrollingData(lightId, cascade);
                            if (!scrollData.active) continue;

                            // GPU copy: the overlapping region from the cached atlas
                            // is already in the same texture (we reuse the allocation).
                            // draws a fullscreen quad with a copy shader; we use
                            // CopyTexture since our cached atlas is a separate texture
                            // from the per-frame atlas, and the overlap is the entire texture.
                            //
                            // For our architecture, the static atlas IS the cached atlas — so
                            // the overlap data is already in place. We just need to render
                            // the edge strips to fill in the non-overlapped regions.
                            //
                            // Render edge strips for scrolling — only non-overlapped regions

                            // Set viewport/scissor to the cascade tile
                            uint32_t tileX = 0, tileY = 0;
                            const uint32_t cols = GetAtlasColumns(scrollingDesc.cascadeCount);
                            tileX = (cascade % cols) * resolution;
                            tileY = (cascade / cols) * resolution;

                            RHIViewport vp{};
                            vp.x = static_cast<float>(tileX);
                            vp.y = static_cast<float>(tileY);
                            vp.width    = static_cast<float>(resolution);
                            vp.height   = static_cast<float>(resolution);
                            vp.maxDepth = 1.0f;
                            cmd->SetViewport(vp);

                            RHIScissorRect sc{};
                            sc.left   = static_cast<int32_t>(tileX);
                            sc.top    = static_cast<int32_t>(tileY);
                            sc.right  = static_cast<int32_t>(tileX + resolution);
                            sc.bottom = static_cast<int32_t>(tileY + resolution);
                            cmd->SetScissor(sc);

                            // Begin depth render pass (no clear — we want to keep overlap data)
                            RHIRenderPassDesc passDesc{};
                            passDesc.colorAttachmentCount = 0;
                            passDesc.hasDepth = true;
                            passDesc.depthAttachment.texture = staticAtlasTex;
                            passDesc.depthAttachment.clear   = false;
                            cmd->BeginRenderPass(passDesc);

                            cmd->SetPipeline(scrollingDesc.pipeline);

                            // Bind per-cascade shadow CB
                            ShadowCB shadowCb{};
                            shadowCb.viewProj = scrollingDesc.cascades[cascade].viewProj;
                            CBAllocation shadowAlloc = scrollingDesc.cbAllocator->Allocate(sizeof(ShadowCB));
                            if (shadowAlloc.cpuPtr) {
                                std::memcpy(shadowAlloc.cpuPtr, &shadowCb, sizeof(ShadowCB));
                                cmd->SetRootCBV(kSlotShadow, shadowAlloc.bufferHandle,
                                                shadowAlloc.offsetBytes, shadowAlloc.cpuPtr, sizeof(ShadowCB));
                            }

                            // Render only static primitives that:
                            // 1. Are NOT already in the cached map (new primitives), OR
                            // 2. Intersect the edge-strip culling planes (partially in the new region)
                            //
                            // Filter static primitives for edge-strip re-render
                            uint32_t extraSubjects = 0;
                            uint32_t lastBoundMeshId = UINT32_MAX;
                            for (uint32_t di = 0; di < commands.size(); ++di) {
                                const auto& drawCmd = commands[di];

                                // Translucent/additive materials (materialFlags
                                // bits[0:1] >= 2) and mesh decals (bit 4) are not occluders.
                                if ((drawCmd.materialFlags & 0x3u) >= 2u || (drawCmd.materialFlags & 0x10u) != 0u) continue;

                                // Only static/stationary primitives
                                if (drawCmd.mobility == EMobility::Movable) continue;

                                // Check if already rendered into the cached shadow map
                                bool isNewPrimitive = !shadowCacheMgr->IsStaticPrimitiveRendered(
                                    lightId, cascade, di);

                                if (!isNewPrimitive) {
                                    // Check if it intersects any edge-strip culling plane.
                                    // For simplicity, we use the draw command's position
                                    // (worldMatrix translation) as a point test against the planes.
                                    // A more precise test would use the mesh's AABB, but this
                                    // is sufficient for most cases.
                                    //
                                    // Point test against edge-strip culling planes
                                    const Vec3 objPos{
                                        drawCmd.worldMatrix.m._41,
                                        drawCmd.worldMatrix.m._42,
                                        drawCmd.worldMatrix.m._43
                                    };

                                    bool intersectsStrip = false;
                                    for (uint32_t pi = 0; pi < scrollData.cullingPlaneCount; ++pi) {
                                        const auto& plane = scrollData.cullingPlanes[pi];
                                        const Vec3 toObj = objPos - plane.point;
                                        // If the primitive is on the negative side of the plane,
                                        // it intersects the edge strip.
                                        if (toObj.Dot(plane.normal) <= 0.f) {
                                            intersectsStrip = true;
                                            break;
                                        }
                                    }

                                    if (!intersectsStrip) continue;
                                }

                                // Render this primitive
                                if (drawCmd.meshId >= scrollingDesc.meshCount) continue;
                                const GPUMesh& mesh = scrollingDesc.meshes[drawCmd.meshId];
                                if (!mesh.IsValid()) continue;

                                if (drawCmd.meshId != lastBoundMeshId) {
                                    if (scrollingDesc.bindless) {
                                        cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
                                    } else {
                                        cmd->SetVertexBuffer(0, mesh.vertexBuffer, 0);
                                    }
                                    cmd->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
                                    lastBoundMeshId = drawCmd.meshId;
                                }

                                PerObjectCB perObject{};
                                perObject.model = drawCmd.worldMatrix;
                                CBAllocation objAlloc = scrollingDesc.cbAllocator->Allocate(sizeof(PerObjectCB));
                                if (objAlloc.cpuPtr) {
                                    std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));
                                    cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle,
                                                    objAlloc.offsetBytes, objAlloc.cpuPtr, sizeof(PerObjectCB));
                                }

                                cmd->DrawIndexed(mesh.indexCount);
                                ++extraSubjects;

                                // Mark this primitive as rendered for future frames
                                shadowCacheMgr->MarkStaticPrimitiveRendered(
                                    lightId, cascade, di, static_cast<uint32_t>(commands.size()));
                            }

                            cmd->EndRenderPass();

                            UZ_LOG(Renderer, Trace,
                                   "CSMScrolling: cascade {} — {} extra static subjects rendered",
                                   cascade, extraSubjects);
                        }
                    });
                });
            }

            // ── Pass 1: Static geometry into cached atlas (conditional) ──
            // Only re-render when cache is fully invalidated (not scrolling)
            if (needStaticRerender) {
                auto staticAtlasRes = rgBuilder.ImportTrackedTexture(
                    "StaticShadowAtlas", m_staticShadowAtlas);

                ShadowPassDesc staticDesc{};
                staticDesc.shadowAtlas        = staticAtlasRes;
                staticDesc.shadowAtlasTexture = m_staticShadowAtlas;
                staticDesc.cacheMode          = ShadowCacheMode::StaticPrimitivesOnly;
                fillCommonShadowDesc(staticDesc);
                AddShadowPass(rgBuilder, staticDesc);
            }

            // ── Pass 2: Dynamic (movable) geometry into per-frame atlas ──
            ShadowPassDesc dynamicDesc{};
            dynamicDesc.shadowAtlas        = ctx.shadowAtlasRes;
            dynamicDesc.shadowAtlasTexture = m_shadowAtlas.GetTexture();
            dynamicDesc.cacheMode          = ShadowCacheMode::MovablePrimitivesOnly;
            fillCommonShadowDesc(dynamicDesc);
            AddShadowPass(rgBuilder, dynamicDesc);
        } else {
            // ── Single uncached pass — backward compatible ──────────────
            ShadowPassDesc shadowDesc{};
            shadowDesc.shadowAtlas        = ctx.shadowAtlasRes;
            shadowDesc.shadowAtlasTexture = m_shadowAtlas.GetTexture();
            shadowDesc.cacheMode          = ShadowCacheMode::Uncached;
            fillCommonShadowDesc(shadowDesc);
            AddShadowPass(rgBuilder, shadowDesc);
        }
    }

    // ── Local Light Shadow Passes (spot + point light shadow depth rendering) ──
    // For each local light that casts shadows, render the scene depth into
    // the corresponding tile(s) in the local shadow atlas. Uses the same
    // shadow depth pipeline as the CSM pass (depth-only, no color).
    // Spot lights get 1 tile each; point lights get 6 tiles (one per cubemap face).
    //
    // SetupWholeSceneProjection for spot lights + RenderShadowDepthMap
    const uint32_t totalLocalShadowEntries =
        ctx.lightData.spotShadowCount +
        ctx.lightData.pointShadowCount * kPointLightShadowFaces;
    if (m_localShadowAtlas.IsValid() && totalLocalShadowEntries > 0 && !m_meshes.empty()) {
        ctx.localShadowAtlasRes = rgBuilder.ImportTrackedTexture(
            "LocalShadowAtlas", m_localShadowAtlas.GetTexture());
        const auto localShadowAtlasRes = ctx.localShadowAtlasRes;

        const auto localAtlasTex = m_localShadowAtlas.GetTexture();
        // Linear-distance variant: the Camera-built light projections are reversed-Z while the
        // CSM convention (Less, clear 1.0, LESS comparison) is standard-Z; storing distance / radius
        // makes the tile independent of either (shadow_depth_linear_ps.hlsl, block A.1).
        const auto localShadowPipeline = pipelines->GetShadowDepthLinear(bindless);
        const auto* shadowCBData = reinterpret_cast<const ShadowDataCB*>(ctx.shadowAlloc.cpuPtr);

        // Build per-tile data for the lambda capture.
        // All local shadow entries (spot tiles + point light face tiles) are stored
        // sequentially in ShadowDataCB::localShadows[]. The atlas tile positions
        // are derived from the sequential tile index (same 4x4 grid layout).
        struct LocalTileData {
            uint32_t viewportX, viewportY, viewportSize;
            Mat4     viewProj;
            float    lightPosX, lightPosY, lightPosZ, radius;
        };
        std::vector<LocalTileData> tileData(totalLocalShadowEntries);
        for (uint32_t si = 0; si < totalLocalShadowEntries; ++si) {
            const uint32_t col = si % m_localShadowAtlas.GetColumns();
            const uint32_t row = si / m_localShadowAtlas.GetColumns();
            tileData[si].viewportX    = col * m_localShadowAtlas.GetTileSize();
            tileData[si].viewportY    = row * m_localShadowAtlas.GetTileSize();
            tileData[si].viewportSize = m_localShadowAtlas.GetTileSize();
            tileData[si].viewProj     = shadowCBData->localShadows[si].viewProj;
            tileData[si].lightPosX    = shadowCBData->localShadows[si].lightPosX;
            tileData[si].lightPosY    = shadowCBData->localShadows[si].lightPosY;
            tileData[si].lightPosZ    = shadowCBData->localShadows[si].lightPosZ;
            tileData[si].radius       = shadowCBData->localShadows[si].radius;
            tileData[si].lightPosX    = shadowCBData->localShadows[si].lightPosX;
            tileData[si].lightPosY    = shadowCBData->localShadows[si].lightPosY;
            tileData[si].lightPosZ    = shadowCBData->localShadows[si].lightPosZ;
            tileData[si].radius       = shadowCBData->localShadows[si].radius;
        }

        const DrawList* localDrawListPtr = &ctx.drawList;
        auto* localCbRing = m_cbRing.get();
        const GPUMesh* localMeshes = m_meshes.data();
        const auto localMeshCount = static_cast<uint32_t>(m_meshes.size());
        const bool localBindless = bindless;
        const auto localTileCount = totalLocalShadowEntries;

        rgBuilder.AddPass("LocalShadowDepth", [localShadowAtlasRes, localAtlasTex,
                                                localShadowPipeline, tileData, localTileCount,
                                                localDrawListPtr, localCbRing, localMeshes,
                                                localMeshCount, localBindless](rg::RgPassBuilder& pb) {
            pb.Write(localShadowAtlasRes, RHIResourceState::DepthStencilWrite);

            pb.SetExecutionCallback([localAtlasTex, localShadowPipeline, tileData, localTileCount,
                                     localDrawListPtr, localCbRing, localMeshes,
                                     localMeshCount, localBindless](
                                        IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("LocalShadowDepth::Execute");

                // Clear to 1.0 = distance / radius of "nothing closer than the range". Tiles store
                // linear distance and are tested with Less, like the CSM atlas. (Cleared to 0.0 until
                // 2026-10-05: with Less no caster could pass, and the LESS comparison then read
                // every spot pixel as shadowed.)
                cmd->ClearDepthStencil(localAtlasTex, 1.0f, 0);

                const auto& commands = localDrawListPtr->GetCommands();
                uint32_t totalDrawCalls = 0;

                // Render each local shadow tile (spot tiles + point light face tiles)
                for (uint32_t si = 0; si < localTileCount; ++si) {
                    const auto& tile = tileData[si];

                    // Viewport: tile region in the atlas
                    RHIViewport vp{};
                    vp.x        = static_cast<float>(tile.viewportX);
                    vp.y        = static_cast<float>(tile.viewportY);
                    vp.width    = static_cast<float>(tile.viewportSize);
                    vp.height   = static_cast<float>(tile.viewportSize);
                    vp.maxDepth = 1.0f;
                    cmd->SetViewport(vp);

                    RHIScissorRect sc{};
                    sc.left   = static_cast<int32_t>(tile.viewportX);
                    sc.top    = static_cast<int32_t>(tile.viewportY);
                    sc.right  = static_cast<int32_t>(tile.viewportX + tile.viewportSize);
                    sc.bottom = static_cast<int32_t>(tile.viewportY + tile.viewportSize);
                    cmd->SetScissor(sc);

                    // Depth-only render pass (0 color attachments)
                    RHIRenderPassDesc passDesc{};
                    passDesc.colorAttachmentCount = 0;
                    passDesc.hasDepth = true;
                    passDesc.depthAttachment.texture    = localAtlasTex;
                    passDesc.depthAttachment.clearDepth = 1.0f;
                    passDesc.depthAttachment.clear      = false; // Already cleared above
                    cmd->BeginRenderPass(passDesc);

                    cmd->SetPipeline(localShadowPipeline);

                    // Bind per-tile shadow CB (viewProj matrix) at slot 3
                    ShadowCB shadowCb{};
                    shadowCb.viewProj    = tile.viewProj;
                    shadowCb.lightPosX   = tile.lightPosX;
                    shadowCb.lightPosY   = tile.lightPosY;
                    shadowCb.lightPosZ   = tile.lightPosZ;
                    shadowCb.lightRadius = tile.radius;
                    CBAllocation shadowCbAlloc = localCbRing->Allocate(sizeof(ShadowCB));
                    if (shadowCbAlloc.cpuPtr == nullptr) {
                        cmd->EndRenderPass();
                        break;
                    }
                    std::memcpy(shadowCbAlloc.cpuPtr, &shadowCb, sizeof(ShadowCB));
                    cmd->SetRootCBV(kSlotShadow, shadowCbAlloc.bufferHandle,
                                    shadowCbAlloc.offsetBytes, shadowCbAlloc.cpuPtr, sizeof(ShadowCB));

                    // Draw all scene geometry into this tile
                    uint32_t lastBoundMeshId = UINT32_MAX;
                    for (const auto& drawCmd : commands) {
                        // Translucent/additive materials and mesh decals are not occluders.
                        if ((drawCmd.materialFlags & 0x3u) >= 2u || (drawCmd.materialFlags & 0x10u) != 0u) continue;
                        if (drawCmd.meshId >= localMeshCount) continue;
                        const GPUMesh& mesh = localMeshes[drawCmd.meshId];
                        if (!mesh.IsValid()) continue;

                        if (drawCmd.meshId != lastBoundMeshId) {
                            if (localBindless) {
                                cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
                            } else {
                                cmd->SetVertexBuffer(0, mesh.vertexBuffer, 0);
                            }
                            cmd->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
                            lastBoundMeshId = drawCmd.meshId;
                        }

                        // Per-object CB: world matrix
                        PerObjectCB perObject{};
                        perObject.model = drawCmd.worldMatrix;
                        CBAllocation objAlloc = localCbRing->Allocate(sizeof(PerObjectCB));
                        if (objAlloc.cpuPtr == nullptr) break;
                        std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));
                        cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle,
                                        objAlloc.offsetBytes, objAlloc.cpuPtr, sizeof(PerObjectCB));

                        cmd->DrawIndexed(mesh.indexCount);
                        ++totalDrawCalls;
                    }

                    cmd->EndRenderPass();
                }

                // First-frame diagnostic logging
                static bool loggedLocalShadowPass = false;
                if (!loggedLocalShadowPass) {
                    UZ_LOG(Renderer, Info,
                           "LocalShadowDepth: first execution — {} tiles, {} total draw calls",
                           localTileCount, totalDrawCalls);
                    loggedLocalShadowPass = true;
                }
            });
        });
    }
}

} // namespace uz
