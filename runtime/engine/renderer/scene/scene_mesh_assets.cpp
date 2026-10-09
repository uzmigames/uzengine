/// UzEngine Scene Renderer — Mesh asset loading (LoadMesh, QueueMeshLoad,
/// RegisterProceduralMesh, QueueSdfBake, ServiceSdfBakes, DestroyAssets).
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
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>

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

// Mesh SDF bakes queued per frame when SDF GI is switched on after the meshes loaded (block B.3):
// each one re-reads its mesh from disk on the main thread.
static constexpr uint32_t kSdfRetriggerPerFrame = 2;

/// Reads a CPU mesh the way LoadMesh does: built-in primitives, then the VFS (pak mode), then the
/// file system (dev mode). Invalid when the path is unknown or the file does not parse.
static Mesh LoadCpuMeshByPath(const std::string& path, IVFS* vfs) {
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
        }
        return cpuMesh;
    }
    // Try VFS first (pak mode), then fall back to filesystem (dev mode).
    if (vfs && vfs->Exists(path)) {
        std::vector<uint8_t> data = vfs->Read(path);
        if (!data.empty()) {
            cpuMesh = uz::LoadMeshFromMemory(data, path);
        }
        return cpuMesh;
    }
    // Resolve to absolute path for filesystem access
    const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                             (path.size() > 1 && path[1] == ':'));
    const std::string resolvedPath = (vfs && !isAbsolute)
        ? vfs->ResolvePath(path)
        : path;
    return uz::LoadMesh(resolvedPath);
}

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

    Mesh cpuMesh = LoadCpuMeshByPath(path, vfs);
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

    // ── Queue the SDF bake (asynchronous, cached — block B.3) ──
    // Only standard-format meshes with valid geometry are eligible. When SDF GI is off the CPU
    // mesh is dropped here; switching it on later re-reads the meshes by path (ServiceSdfBakes).
    if (m_sdfGiEnabled
        && cpuMesh.header.vertexFormat == MeshVertexFormat::Standard
        && cpuMesh.GetVertexCount() > 0 && cpuMesh.GetIndexCount() >= 3)
    {
        QueueSdfBake(index, cpuMesh);
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
    if (m_sdfBakeQueue && m_sdfBakeQueue->InFlight() > 0) return false;
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
           m_budgetDeferredMeshCount, m_deferredMeshUploads.size(), m_sdfBakeQueue ? m_sdfBakeQueue->InFlight() : size_t{0}, m_asyncPendingSlots.size());
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
// QueueSdfBake — hand a loaded mesh to the asynchronous bake queue (block B.3)
// ─────────────────────────────────────────────────────────────────────────────

static MeshSdfBakeRequest MakeSdfBakeRequest(uint32_t meshIndex) {
    MeshSdfBakeRequest request;
    request.meshIndex = meshIndex;
    request.resolution = static_cast<uint32_t>(std::clamp(cv_MeshSdfResolution->value, 1.0f, 128.0f));
    request.twoSided = cv_MeshSdfTwoSided->value != 0.0f;
    request.vertexNormalOffset = static_cast<uint32_t>(offsetof(Vertex, normal));
    return request;
}

void SceneRenderer::QueueSdfBake(uint32_t meshIndex, Mesh& cpuMesh) {
    if (!m_sdfBakeQueue) return;
    MeshSdfBakeRequest request = MakeSdfBakeRequest(meshIndex);
    if (!MeshSdfBakeQueue::FillFromMesh(request, cpuMesh)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: mesh {} has no standard geometry for an SDF", meshIndex);
        return;
    }
    m_sdfBakeQueue->Submit(std::move(request));
}

// ───────────────────────────────────────
// ServiceSdfBakes — the asynchronous bakes, once per frame (block B.3)
// ───────────────────────────────────────

void SceneRenderer::ServiceSdfBakes(IRHICommandList* cmdList) {
    if (!m_sdfBakeQueue) return;
    const auto serviceStart = std::chrono::steady_clock::now();
    uint32_t requeued = 0;

    // SDF GI switched on since meshes loaded without fields: re-read them by path, a few per
    // frame, and queue their bakes. Procedural meshes have no source and keep no field.
    // The first call only records the state: meshes loaded with SDF GI on were queued by LoadMesh.
    if (!m_sdfServiceStarted) {
        m_sdfServiceStarted = true;
        m_sdfGiEnabledLast = m_sdfGiEnabled;
    }
    if (m_sdfGiEnabled && !m_sdfGiEnabledLast) {
        m_sdfRetriggerPending = true;
        m_sdfRetriggerCursor = 0;
    }
    m_sdfGiEnabledLast = m_sdfGiEnabled;
    if (m_sdfRetriggerPending && m_sdfGiEnabled) {
        while (m_sdfRetriggerCursor < m_meshes.size() && requeued < kSdfRetriggerPerFrame) {
            const uint32_t index = m_sdfRetriggerCursor++;
            if (m_meshes[index].sdf.valid || !m_meshes[index].IsValid() || m_sdfBakeQueue->IsPending(index)) continue;
            std::string path;
            for (const auto& [meshPath, meshIndex] : m_meshMap) {
                if (meshIndex == index) { path = meshPath; break; }
            }
            if (path.empty()) continue;
            MeshSdfBakeRequest request = MakeSdfBakeRequest(index);
            if (path.compare(0, 8, "builtin:") == 0) {
                Mesh cpuMesh = LoadCpuMeshByPath(path, m_lastVfs);   // procedural, cheap
                if (!MeshSdfBakeQueue::FillFromMesh(request, cpuMesh)) continue;
            } else if (m_lastVfs && m_lastVfs->Exists(path) && !std::filesystem::exists(m_lastVfs->ResolvePath(path))) {
                request.sourceBytes = m_lastVfs->Read(path);          // pak: the bytes, parsed on the worker
                request.sourcePath = path;
                if (request.sourceBytes.empty()) continue;
            } else {
                // A real file: the worker reads and parses it (a 15 MB read cost the main thread ~25 ms).
                const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                                         (path.size() > 1 && path[1] == ':'));
                request.sourcePath = (m_lastVfs && !isAbsolute) ? m_lastVfs->ResolvePath(path) : path;
            }
            m_sdfBakeQueue->Submit(std::move(request));
            ++requeued;
        }
        if (m_sdfRetriggerCursor >= m_meshes.size()) {
            m_sdfRetriggerPending = false;
            UZ_LOG(Renderer, Info, "SceneRenderer: SDF GI enabled after load — missing mesh SDFs queued");
        }
    }

    const auto requeueEnd = std::chrono::steady_clock::now();
    const RHIPipelineHandle bakePipeline = m_pipelines ? m_pipelines->GetMeshSdfBake() : RHIPipelineHandle{};
    // A profiler scope around the GPU work of this frame (uploads, slab dispatches, readbacks).
    if (m_gpuProfileEnabled) m_device->GpuProfilerBeginTimestamp(cmdList, "MeshSdfBake");
    const MeshSdfBakeQueue::ServiceStats stats = m_sdfBakeQueue->Service(cmdList, bakePipeline, m_meshes, m_bakeFenceValue + 1, m_preloading);
    if (m_gpuProfileEnabled) m_device->GpuProfilerEndTimestamp(cmdList, "MeshSdfBake");
    // Main-thread cost of this frame's service (mesh re-reads, resource creation, uploads): a hitch
    // here is visible in the frame time, so it is named when it exceeds 10 ms.
    const double serviceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - serviceStart).count();
    if (serviceMs > 10.0) {
        const double requeueMs = std::chrono::duration<double, std::milli>(requeueEnd - serviceStart).count();
        UZ_LOG(Renderer, Info, "SceneRenderer: SDF bake service took {:.1f} ms this frame ({:.1f} ms queuing {} re-reads; {:.1f} ms completing {} [fence {:.1f}, body {:.1f}, erase {:.1f}]; {:.1f} ms starting {}; {:.1f} ms slabs {}; {} in flight)",
               serviceMs, requeueMs, requeued, stats.completeMs, stats.completed, stats.fenceMs, stats.completeBodyMs, stats.eraseMs, stats.startMs, stats.started, stats.slabMs, stats.slabs, m_sdfBakeQueue->InFlight());
    }
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
