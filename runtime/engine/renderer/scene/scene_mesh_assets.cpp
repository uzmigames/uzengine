/// UzEngine Scene Renderer — Mesh asset loading (LoadMesh, QueueMeshLoad,
/// RegisterProceduralMesh, FlushSdfBakes, DestroyAssets).
///
/// Mesh loading uses deferred uploads: `UploadMeshDeferred()` records copy
/// commands on the command list but does NOT execute/WaitIdle. Staging buffers
/// are kept alive in `m_deferredMeshUploads`. A single `FlushDeferredMeshUploads()`
/// call at the end of the sync pass executes all copies in one GPU submission.

#include "scene_renderer.h"

#include "mesh.h"
#include "gpu_mesh.h"
#include "upload_context.h"
#include "vfs.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"
#include "gi/mesh_sdf_baker.h"
#include "pipeline_library.h"
#include "vertex.h"
#include "cvar.h"

#include <fstream>
#include <string>
#include <algorithm>
#include <cstddef>

// Mesh distance-field baking parameters. Both take effect at bake time, so a
// scene reload is required after changing them. TwoSided produces an unsigned
// field: Sponza is split into open material partitions whose winding disagrees
// with its vertex normals, and a nearest-face sign cannot define an interior
// for such a surface — it classified free space as solid.
static auto* cv_MeshSdfTwoSided = UZ_CVAR_BOOL("r.GI.MeshSdf.TwoSided", false,
    "Unsigned mesh distance fields for open surfaces (requires scene reload)");
static auto* cv_MeshSdfResolution = UZ_CVAR_INT("r.GI.MeshSdf.Resolution", 32,
    "Mesh distance field voxels per axis (requires scene reload)", 1, 128);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Asset Loading — Meshes (deferred upload, flushed in batch)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SceneRenderer::LoadMesh(const std::string& path, IVFS* vfs,
                                  IRHICommandList* cmdList) {
    // Check cache
    auto it = m_meshMap.find(path);
    if (it != m_meshMap.end()) {
        return it->second;
    }

    // Built-in primitives: "builtin:cube", "builtin:sphere"
    Mesh cpuMesh{};
    if (path.size() > 8 && path.compare(0, 8, "builtin:") == 0) {
        const std::string primitive = path.substr(8);
        if (primitive == "cube") {
            cpuMesh = MeshBuilder::CreateCube(0.5f, 8);
        } else if (primitive == "sphere") {
            cpuMesh = MeshBuilder::CreateSphere(0.5f, 64, 64);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: unknown builtin mesh: '{}'", primitive);
            return UINT32_MAX;
        }
    } else {
        // Try VFS first (pak mode), then fall back to filesystem (dev mode).
        if (vfs && vfs->Exists(path)) {
            std::vector<uint8_t> data = vfs->Read(path);
            if (!data.empty()) {
                cpuMesh = uz::LoadMeshFromMemory(data, path);
            }
        } else {
            // Resolve to absolute path for filesystem access
            const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                                     (path.size() > 1 && path[1] == ':'));
            const std::string resolvedPath = (vfs && !isAbsolute)
                ? vfs->ResolvePath(path)
                : path;

            cpuMesh = uz::LoadMesh(resolvedPath);
        }
    }
    if (!cpuMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to load mesh: '{}'", path);
        return UINT32_MAX;
    }

    DeferredMeshUpload deferred = UploadMeshDeferred(m_device, cmdList, cpuMesh);
    if (!deferred.mesh.IsValid()) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to upload mesh: '{}'", path);
        return UINT32_MAX;
    }

    const auto index = static_cast<uint32_t>(m_meshes.size());
    m_meshes.push_back(deferred.mesh);  // copy handles (GPUMesh is trivially copyable)
    m_meshBoundsMin.push_back(cpuMesh.header.boundsMin);
    m_meshBoundsMax.push_back(cpuMesh.header.boundsMax);
    m_meshMap[path] = index;
    m_deferredMeshUploads.push_back(std::move(deferred));  // move staging ownership

    // ── Queue SDF bake for GI system (deferred — runs after mesh upload flush) ──
    // Only standard-format meshes with valid geometry are eligible for SDF baking.
    // Skip entirely when SDF GI is not enabled — FlushSdfBakes() does a hard
    // device WaitIdle per batch, which freezes the main thread for hundreds of
    // milliseconds when loading scenes with many meshes (e.g. Sponza). The
    // SDF bake can be re-triggered later if SDF GI is toggled on.
    // Read counts/stride from the header BEFORE moving the vectors (header is not moved).
    if (m_sdfGiEnabled
        && cpuMesh.header.vertexFormat == MeshVertexFormat::Standard
        && cpuMesh.GetVertexCount() > 0 && cpuMesh.GetIndexCount() >= 3)
    {
        PendingSdfBake sdfBake;
        sdfBake.meshIndex    = index;
        sdfBake.vertexCount  = cpuMesh.GetVertexCount();
        sdfBake.vertexStride = cpuMesh.header.vertexStride;
        sdfBake.indexCount   = cpuMesh.GetIndexCount();
        sdfBake.vertexData   = std::move(cpuMesh.vertexData);
        sdfBake.indexData    = std::move(cpuMesh.indexData);
        m_pendingSdfBakes.push_back(std::move(sdfBake));
    }

    UZ_LOG(Renderer, Info, "SceneRenderer: loaded mesh '{}' (id={}, {} vertices, {} indices)",
           path, index, m_meshes[index].vertexCount, m_meshes[index].indexCount);
    return index;
}

// ─────────────────────────────────────────────────────────────────────────────
// Deferred Load Queue — Meshes (Lua API)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SceneRenderer::QueueMeshLoad(const std::string& path) {
    if (path.empty()) return UINT32_MAX;

    // Return cached ID immediately — no need to queue.
    auto it = m_meshMap.find(path);
    if (it != m_meshMap.end()) {
        return it->second;
    }

    // Reserve a stable slot before the GPU upload occurs.
    // The slot will be filled by FlushPendingLoads() on the next RenderFrame().
    // Insert a placeholder GPUMesh so the vector slot exists.
    const uint32_t slotIndex = static_cast<uint32_t>(m_meshes.size());
    m_meshes.emplace_back();          // placeholder (IsValid() == false)
    m_meshBoundsMin.emplace_back();   // placeholder bounds (zero) — filled by FlushPendingLoads
    m_meshBoundsMax.emplace_back();
    m_meshMap[path] = slotIndex;

    m_pendingMeshPaths.push_back(path);
    UZ_LOG(Renderer, Debug, "SceneRenderer: queued mesh load '{}' (slot={})", path, slotIndex);
    return slotIndex;
}

const GPUMesh* SceneRenderer::GetGPUMesh(uint32_t meshId) const {
    if (meshId >= m_meshes.size()) return nullptr;
    const GPUMesh& m = m_meshes[meshId];
    if (!m.IsValid()) return nullptr;
    return &m;
}

void SceneRenderer::QueueBufferDestroy(RHIBufferHandle buffer) {
    if (buffer.IsValid()) {
        m_deferredDestroyBuffers.push_back(buffer);
    }
}

uint32_t SceneRenderer::RegisterProceduralMesh(Mesh&& mesh, const std::string& key) {
    if (key.empty() || !mesh.IsValid()) return UINT32_MAX;

    // Return cached ID if this procedural mesh was already registered.
    auto it = m_meshMap.find(key);
    if (it != m_meshMap.end()) {
        return it->second;
    }

    // Reserve a stable slot with a placeholder GPUMesh.
    const auto index = static_cast<uint32_t>(m_meshes.size());
    m_meshes.emplace_back();  // placeholder (IsValid() == false)
    m_meshBoundsMin.push_back(mesh.header.boundsMin);
    m_meshBoundsMax.push_back(mesh.header.boundsMax);
    m_meshMap[key] = index;

    // Queue the CPU mesh for GPU upload on the next FlushPendingLoads().
    m_pendingProceduralMeshes.emplace_back(index, std::move(mesh));

    UZ_LOG(Renderer, Debug, "SceneRenderer: registered procedural mesh '{}' (slot={})", key, index);
    return index;
}

// ─────────────────────────────────────────────────────────────────────────────
// IsSceneFullyLoaded
// ─────────────────────────────────────────────────────────────────────────────

bool SceneRenderer::IsSceneFullyLoaded() const {
    // Budget-deferred meshes, batched mesh uploads, pending SDF bakes and
    // in-flight async slots were all invisible to this check: it reported
    // "loaded" while Sponza's ceiling was still streaming (task 05 evidence:
    // screenshot at 14:29:40, last mesh at 14:29:41).
    if (m_budgetDeferredMeshCount > 0) return false;
    if (!m_deferredMeshUploads.empty()) return false;
    if (!m_pendingSdfBakes.empty()) return false;
    if (!m_asyncPendingSlots.empty()) return false;
    if (!m_pendingMeshPaths.empty()) return false;
    if (!m_pendingProceduralMeshes.empty()) return false;
    if (!m_pendingTexturePaths.empty()) return false;
    if (m_asyncDecoder && m_asyncDecoder->PendingCount() > 0) return false;
    if (!m_deferredDecoded.empty()) return false;
    if (m_textureBatchCount > 0) return false;
    if (!m_pendingCopyUploads.empty()) return false;
    if (m_copyFenceValue > 0) return false;
    return true;
}

bool SceneRenderer::IsSceneFullyLoadedDebug() const {
    UZ_LOG(Renderer, Debug,
           "IsSceneFullyLoaded (readiness extras): budgetDeferredMeshes={}, meshUploads={}, sdfBakes={}, asyncSlots={}",
           m_budgetDeferredMeshCount, m_deferredMeshUploads.size(), m_pendingSdfBakes.size(), m_asyncPendingSlots.size());
    UZ_LOG(Renderer, Debug,
           "IsSceneFullyLoaded: meshPaths={}, procMeshes={}, texPaths={}, asyncPending={}, "
           "deferred={}, batch={}, copyUploads={}, copyFence={}",
           m_pendingMeshPaths.size(), m_pendingProceduralMeshes.size(),
           m_pendingTexturePaths.size(),
           m_asyncDecoder ? m_asyncDecoder->PendingCount() : 0,
           m_deferredDecoded.size(), m_textureBatchCount,
           m_pendingCopyUploads.size(), m_copyFenceValue);
    return IsSceneFullyLoaded();
}

// ─────────────────────────────────────────────────────────────────────────────
// FlushSdfBakes — batch GPU SDF bake for all pending meshes
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::FlushSdfBakes(IRHICommandList* cmdList) {
    if (m_pendingSdfBakes.empty()) return;

    auto bakePipeline = m_pipelines->GetMeshSdfBake();
    if (!bakePipeline.IsValid()) {
        UZ_LOG(Renderer, Warning,
               "SceneRenderer: SDF bake pipeline not available — skipping {} pending bakes",
               m_pendingSdfBakes.size());
        m_pendingSdfBakes.clear();
        return;
    }

    const auto resolution = static_cast<uint32_t>(std::clamp(cv_MeshSdfResolution->value, 1.0f, 128.0f));

    UZ_LOG(Renderer, Info, "SceneRenderer: batch SDF bake — {} meshes",
           m_pendingSdfBakes.size());

    for (auto& bake : m_pendingSdfBakes) {
        if (bake.meshIndex >= static_cast<uint32_t>(m_meshes.size())) continue;

        const auto* positions = reinterpret_cast<const float*>(bake.vertexData.data());
        const auto* indices   = reinterpret_cast<const uint32_t*>(bake.indexData.data());

        MeshSDF sdf = MeshSdfBaker::BakeMeshGPU(
            m_device, cmdList, bakePipeline,
            positions, bake.vertexCount, bake.vertexStride,
            indices, bake.indexCount, resolution, static_cast<uint32_t>(offsetof(Vertex, normal)),
            cv_MeshSdfTwoSided->value);

        if (sdf.valid) {
            // The triangle + BVH buffer the bake read is kept with the SDF: the radiance cache's
            // probe side classifier (followup-01) asks it for the nearest face of each probe.
            // It is released with the SDF texture (DestroyGPUMesh).
            sdf.triangleBuffer = sdf.pendingTriBuffer;
            sdf.pendingTriBuffer = RHIBufferHandle{};
            m_meshes[bake.meshIndex].sdf = sdf;
            UZ_LOG(Renderer, Info,
                   "SceneRenderer: GPU-baked mesh SDF (meshIdx={}, res={}, SRV={})",
                   bake.meshIndex, sdf.resolution, sdf.srvIndex);
        } else {
            UZ_LOG(Renderer, Warning,
                   "SceneRenderer: SDF bake failed for meshIdx={} ({} verts, {} indices)",
                   bake.meshIndex, bake.vertexCount, bake.indexCount);
        }
    }

    // All SDF dispatches are on the command list — flush once and wait for GPU.
    cmdList->End();
    cmdList->Execute();
    m_device->WaitIdle();
    cmdList->Begin();

    m_pendingSdfBakes.clear();
    UZ_LOG(Renderer, Info, "SceneRenderer: SDF batch bake complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyAssets — release all mesh and texture GPU resources
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::DestroyAssets() {
    // Release any in-flight texture batch upload before destroying textures
    if (m_textureBatchUpload) {
        m_textureBatchUpload->ReleaseStaging();
        m_textureBatchUpload.reset();
    }
    m_textureBatchCount = 0;

    // Destroy textures that were in-flight on the copy queue.
    // WaitIdle was already called in Shutdown() before DestroyAssets(), so
    // these textures are safe to destroy.
    for (auto& pending : m_pendingCopyUploads) {
        if (pending.result.handle.IsValid()) {
            m_device->DestroyTexture(pending.result.handle);
        }
    }
    m_pendingCopyUploads.clear();
    m_copyFenceValue = 0;

    for (auto& mesh : m_meshes) {
        DestroyGPUMesh(m_device, mesh);
    }
    m_meshes.clear();
    m_meshBoundsMin.clear();
    m_meshBoundsMax.clear();
    m_meshMap.clear();

    for (auto& tex : m_textures) {
        // Skip fallback textures — they are destroyed separately in Shutdown()
        // and may be shared across multiple slots.
        if (tex.handle.IsValid()
            && tex.handle != m_fallbackWhite
            && tex.handle != m_fallbackBlack
            && tex.handle != m_fallbackNormal) {
            m_device->DestroyTexture(tex.handle);
        }
    }
    m_textures.clear();
    m_textureMap.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// TrackExternalTexture — register externally-created texture for Shutdown cleanup
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::TrackExternalTexture(const TextureLoadResult& tex) {
    if (!tex.handle.IsValid()) return;
    m_textures.push_back(tex);
}

} // namespace uz
