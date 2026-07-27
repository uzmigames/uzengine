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
#include "cvar.h"
#include "gi/mesh_sdf_baker.h"
#include "pipeline_library.h"

#include <fstream>
#include <string>

// ── CVars ────────────────────────────────────────────────────────────────
/// Per-frame budget for integrating async-loaded meshes (GPU upload). File
/// reads and parsing happen unbudgeted on AsyncMeshLoader workers; this only
/// caps how many completed meshes get staged + copied per frame, bounding the
/// transient staging-memory spike. 0 = unlimited (integrate everything ready).
static auto* cv_LazyMeshesPerFrame = UZ_CVAR_INT(
    "r.LazyLoad.MeshesPerFrame", 32,
    "Max async-loaded meshes to GPU-upload per frame (0 = unlimited).",
    0, 1024);

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
// RequestMeshAsync — background read + parse, placeholder slot until ready
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SceneRenderer::RequestMeshAsync(const std::string& path, IVFS* vfs,
                                          IRHICommandList* cmdList) {
    if (path.empty()) return UINT32_MAX;

    // Cache hit: slot exists (resident mesh or in-flight placeholder).
    auto it = m_meshMap.find(path);
    if (it != m_meshMap.end()) {
        return it->second;
    }

    // Built-in primitives are cheap procedural CPU meshes — synchronous path.
    // Async loader unavailable (init failure / pre-init) — synchronous path.
    if ((path.size() > 8 && path.compare(0, 8, "builtin:") == 0) || !m_asyncMeshLoader) {
        return LoadMesh(path, vfs, cmdList);
    }

    // Reserve a placeholder slot so the path maps to a stable mesh ID from the
    // first request. The GPUMesh stays invalid (IsValid() == false) until
    // IntegrateAsyncMeshLoads() uploads the parsed data — callers must not
    // draw the slot before it becomes valid.
    const auto slot = static_cast<uint32_t>(m_meshes.size());
    m_meshes.emplace_back();
    m_meshBoundsMin.emplace_back();
    m_meshBoundsMax.emplace_back();
    m_meshMap[path] = slot;
    m_asyncMeshPendingSlots[path] = slot;

    // Prefer a resolved filesystem path: workers then use plain file I/O and
    // do not serialize on the shared VFS mutex. Fall back to a VFS read on
    // the worker when the file is not directly reachable (pak-only assets).
    const bool isAbsolute = (path[0] == '/' || path[0] == '\\' ||
                             (path.size() > 1 && path[1] == ':'));
    const std::string fsPath = (vfs && !isAbsolute) ? vfs->ResolvePath(path) : path;
    if (std::ifstream(fsPath, std::ios::binary).good()) {
        m_asyncMeshLoader->Submit(fsPath, path);
    } else if (vfs) {
        m_asyncMeshLoader->SubmitVFS(vfs, path, path);
    } else {
        m_asyncMeshLoader->Submit(path, path);
    }

    UZ_LOG(Renderer, Debug, "SceneRenderer: async mesh load requested '{}' (slot={})",
           path, slot);
    return slot;
}

// ─────────────────────────────────────────────────────────────────────────────
// IntegrateAsyncMeshLoads — GPU-upload completed CPU meshes (budgeted)
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::IntegrateAsyncMeshLoads(IRHICommandList* cmdList) {
    if (!m_asyncMeshLoader) return;

    UZ_PROFILE_FUNCTION();

    // Collect newly completed CPU meshes (non-blocking).
    {
        auto results = m_asyncMeshLoader->PollCompleted();
        for (auto& r : results) {
            m_asyncMeshReady.push_back(std::move(r));
        }
    }
    if (m_asyncMeshReady.empty()) return;

    const int budgetMax = cv_LazyMeshesPerFrame ? cv_LazyMeshesPerFrame->value : 0;
    const auto toProcess = (budgetMax > 0)
        ? std::min(m_asyncMeshReady.size(), static_cast<size_t>(budgetMax))
        : m_asyncMeshReady.size();

    for (size_t i = 0; i < toProcess; ++i) {
        auto& ready = m_asyncMeshReady[i];

        auto pendingIt = m_asyncMeshPendingSlots.find(ready.path);
        if (pendingIt == m_asyncMeshPendingSlots.end()) {
            UZ_LOG(Renderer, Warning,
                   "SceneRenderer: async mesh completed for unknown path '{}' — discarding",
                   ready.path);
            continue;
        }
        const uint32_t slot = pendingIt->second;

        if (!ready.mesh.IsValid()) {
            // Read/parse failed on the worker (already logged there). The slot
            // stays an invalid placeholder — entities never claim it.
            UZ_LOG(Renderer, Error,
                   "SceneRenderer: async mesh load failed for '{}' (slot={})",
                   ready.path, slot);
            m_asyncMeshPendingSlots.erase(pendingIt);
            continue;
        }

        DeferredMeshUpload deferred = UploadMeshDeferred(m_device, cmdList, ready.mesh);
        if (!deferred.mesh.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "SceneRenderer: GPU upload failed for async mesh '{}' (slot={})",
                   ready.path, slot);
            m_asyncMeshPendingSlots.erase(pendingIt);
            continue;
        }

        m_meshes[slot]        = deferred.mesh;
        m_meshBoundsMin[slot] = ready.mesh.header.boundsMin;
        m_meshBoundsMax[slot] = ready.mesh.header.boundsMax;
        m_deferredMeshUploads.push_back(std::move(deferred));

        // Queue SDF bake (same eligibility as the synchronous LoadMesh path).
        if (m_sdfGiEnabled
            && ready.mesh.header.vertexFormat == MeshVertexFormat::Standard
            && ready.mesh.GetVertexCount() > 0 && ready.mesh.GetIndexCount() >= 3)
        {
            PendingSdfBake sdfBake;
            sdfBake.meshIndex    = slot;
            sdfBake.vertexCount  = ready.mesh.GetVertexCount();
            sdfBake.vertexStride = ready.mesh.header.vertexStride;
            sdfBake.indexCount   = ready.mesh.GetIndexCount();
            sdfBake.vertexData   = std::move(ready.mesh.vertexData);
            sdfBake.indexData    = std::move(ready.mesh.indexData);
            m_pendingSdfBakes.push_back(std::move(sdfBake));
        }

        m_asyncMeshPendingSlots.erase(pendingIt);
        UZ_LOG(Renderer, Info,
               "SceneRenderer: async mesh integrated '{}' (id={}, {} verts, {} indices)",
               ready.path, slot, m_meshes[slot].vertexCount, m_meshes[slot].indexCount);
    }

    m_asyncMeshReady.erase(m_asyncMeshReady.begin(),
                           m_asyncMeshReady.begin() + static_cast<ptrdiff_t>(toProcess));
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
    if (!m_pendingMeshPaths.empty()) return false;
    if (!m_pendingProceduralMeshes.empty()) return false;
    if (!m_pendingTexturePaths.empty()) return false;
    if (m_asyncDecoder && m_asyncDecoder->PendingCount() > 0) return false;
    if (!m_deferredDecoded.empty()) return false;
    if (m_textureBatchCount > 0) return false;
    if (!m_pendingCopyUploads.empty()) return false;
    if (m_copyFenceValue > 0) return false;
    if (m_asyncMeshLoader && m_asyncMeshLoader->PendingCount() > 0) return false;
    if (!m_asyncMeshReady.empty()) return false;
    if (!m_asyncMeshPendingSlots.empty()) return false;
    if (!m_deferredMeshUploads.empty()) return false;
    return true;
}

bool SceneRenderer::IsSceneFullyLoadedDebug() const {
    UZ_LOG(Renderer, Debug,
           "IsSceneFullyLoaded: meshPaths={}, procMeshes={}, texPaths={}, asyncPending={}, "
           "deferred={}, batch={}, copyUploads={}, copyFence={}, "
           "asyncMeshPending={}, asyncMeshReady={}, asyncMeshSlots={}",
           m_pendingMeshPaths.size(), m_pendingProceduralMeshes.size(),
           m_pendingTexturePaths.size(),
           m_asyncDecoder ? m_asyncDecoder->PendingCount() : 0,
           m_deferredDecoded.size(), m_textureBatchCount,
           m_pendingCopyUploads.size(), m_copyFenceValue,
           m_asyncMeshLoader ? m_asyncMeshLoader->PendingCount() : 0,
           m_asyncMeshReady.size(), m_asyncMeshPendingSlots.size());
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

    UZ_LOG(Renderer, Info, "SceneRenderer: batch SDF bake — {} meshes",
           m_pendingSdfBakes.size());

    for (auto& bake : m_pendingSdfBakes) {
        if (bake.meshIndex >= static_cast<uint32_t>(m_meshes.size())) continue;

        const auto* positions = reinterpret_cast<const float*>(bake.vertexData.data());
        const auto* indices   = reinterpret_cast<const uint32_t*>(bake.indexData.data());

        MeshSDF sdf = MeshSdfBaker::BakeMeshGPU(
            m_device, cmdList, bakePipeline,
            positions, bake.vertexCount, bake.vertexStride,
            indices, bake.indexCount, 32);

        if (sdf.valid) {
            // Defer destruction of the temporary triangle upload buffer until the
            // GPU finishes the command list that references it.
            if (sdf.pendingTriBuffer.IsValid()) {
                m_deferredDestroyBuffers.push_back(sdf.pendingTriBuffer);
                sdf.pendingTriBuffer = RHIBufferHandle{};
            }
            m_meshes[bake.meshIndex].sdf = sdf;
            UZ_LOG(Renderer, Info,
                   "SceneRenderer: GPU-baked mesh SDF (meshIdx={}, res={}, SRV={})",
                   bake.meshIndex, 32, sdf.srvIndex);
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

    // The GDF composite only re-runs for dirty clipmaps, and the dirty flag
    // tracks camera motion — a composite that already ran this session would
    // never see the SDFs we just baked. Force a full recomposite.
    m_globalDistanceField.MarkAllDirty();

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
