/// UzEngine Scene Renderer — Asset loading (LoadMesh, LoadTexture, Queue*, DestroyAssets).
///
/// Mesh loading uses deferred uploads: `UploadMeshDeferred()` records copy
/// commands on the command list but does NOT execute/WaitIdle. Staging buffers
/// are kept alive in `m_deferredMeshUploads`. A single `FlushDeferredMeshUploads()`
/// call at the end of the sync pass executes all copies in one GPU submission.
///
/// Texture loading uses a two-phase async pipeline:
///   1. Submit phase: `LoadTexture()` / `QueueTextureLoad()` reserve a slot in
///      m_textures and submit the path to the AsyncTextureDecoder for background
///      CPU decode (stbi_load on worker threads).
///   2. Flush phase: `FlushPendingLoads()` polls completed decodes (non-blocking)
///      and uploads whatever is ready this frame. Remaining in-flight decodes are
///      picked up on subsequent frames, spreading upload cost across frames.

#include "scene_renderer.h"

#include "async_texture_decoder.h"
#include "mesh.h"
#include "gpu_mesh.h"
#include "texture_loader.h"
#include "upload_context.h"
#include "vfs.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"
#include "gi/mesh_sdf_baker.h"
#include "pipeline_library.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include <stb_image.h>

#include <string>

namespace uz {

/// Maximum textures to upload per frame. Spreads GPU upload cost across
/// multiple frames instead of one large stall. Remaining decoded textures
/// are kept in m_deferredDecoded for the next frame.
static constexpr uint32_t kMaxTextureUploadsPerFrame = 8;

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
            cpuMesh = MeshBuilder::CreateCube(0.5f);
        } else if (primitive == "sphere") {
            cpuMesh = MeshBuilder::CreateSphere(0.5f, 32, 32);
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
    m_meshMap[path] = index;
    m_deferredMeshUploads.push_back(std::move(deferred));  // move staging ownership

    // ── Queue SDF bake for GI system (deferred — runs after mesh upload flush) ──
    // Only standard-format meshes with valid geometry are eligible for SDF baking.
    // Read counts/stride from the header BEFORE moving the vectors (header is not moved).
    if (cpuMesh.header.vertexFormat == MeshVertexFormat::Standard
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
// Asset Loading — Textures (async decode + deferred GPU upload)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SceneRenderer::LoadTexture(const std::string& path, IVFS* vfs,
                                     IRHICommandList* cmdList) {
    // Check fully-loaded cache first
    auto it = m_textureMap.find(path);
    if (it != m_textureMap.end()) {
        return it->second;
    }

    // Check if already submitted for async decode
    if (m_asyncPendingSlots.find(path) != m_asyncPendingSlots.end()) {
        auto mapIt = m_textureMap.find(path);
        return (mapIt != m_textureMap.end()) ? mapIt->second : UINT32_MAX;
    }

    // Reserve a slot with an invalid placeholder. The texture will become
    // usable after FlushPendingLoads() uploads the decoded pixels.
    const auto slotIndex = static_cast<uint32_t>(m_textures.size());
    m_textures.emplace_back(); // placeholder (handle.IsValid() == false)
    m_textureMap[path] = slotIndex;

    // Try VFS first (pak mode): read bytes into memory, submit to async decoder.
    // Fall back to filesystem path for loose files (dev mode).
    std::string decodePath = path; // key used by async decoder
    std::vector<uint8_t> vfsData;

    if (vfs && vfs->Exists(path)) {
        vfsData = vfs->Read(path);
    }

    if (!vfsData.empty()) {
        // Pak mode: load synchronously from in-memory bytes.
        // AsyncTextureDecoder doesn't support in-memory submission yet,
        // so we use the sync path. This is fast enough for pak textures
        // since the data is already decompressed in memory.
        TextureLoadResult result = TextureLoader::LoadFromMemory(
            m_device, cmdList, vfsData.data(), vfsData.size(), path.c_str());
        if (result.handle.IsValid()) {
            m_textures[slotIndex] = result;
            UZ_LOG(Renderer, Info, "SceneRenderer: loaded texture (VFS) '{}' (id={}, {}x{}, srv={})",
                   path, slotIndex, result.width, result.height, result.srvIndex);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to load texture (VFS): '{}'", path);
        }
    } else {
        // Dev mode: resolve filesystem path
        const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                                 (path.size() > 1 && path[1] == ':'));
        const std::string resolvedPath = (vfs && !isAbsolute)
            ? vfs->ResolvePath(path)
            : path;

        if (resolvedPath != path) {
            m_textureMap[resolvedPath] = slotIndex;
        }

        if (m_asyncDecoder) {
            m_asyncDecoder->Submit(resolvedPath);
            m_asyncPendingSlots[path] = slotIndex;
            UZ_LOG(Renderer, Debug, "SceneRenderer: submitted async texture decode '{}' (slot={})",
                   resolvedPath, slotIndex);
        } else {
            TextureLoadResult result = TextureLoader::Load(m_device, cmdList, resolvedPath);
            if (result.handle.IsValid()) {
                m_textures[slotIndex] = result;
            } else {
                UZ_LOG(Renderer, Error, "SceneRenderer: failed to load texture: '{}'", path);
            }
        }
    }

    return slotIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// Deferred Load Queue — Lua API
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
    m_meshMap[path] = slotIndex;

    m_pendingMeshPaths.push_back(path);
    UZ_LOG(Renderer, Debug, "SceneRenderer: queued mesh load '{}' (slot={})", path, slotIndex);
    return slotIndex;
}

uint32_t SceneRenderer::QueueTextureLoad(const std::string& path) {
    if (path.empty()) return UINT32_MAX;

    // Return cached ID immediately — no need to queue.
    auto it = m_textureMap.find(path);
    if (it != m_textureMap.end()) {
        return it->second;
    }

    // Reserve a stable slot before the GPU upload occurs.
    const uint32_t slotIndex = static_cast<uint32_t>(m_textures.size());
    m_textures.emplace_back();        // placeholder (handle.IsValid() == false)
    m_textureMap[path] = slotIndex;

    // Submit to async decoder for background CPU decode.
    if (m_asyncDecoder) {
        m_asyncDecoder->Submit(path);
        m_asyncPendingSlots[path] = slotIndex;
        UZ_LOG(Renderer, Debug, "SceneRenderer: queued async texture load '{}' (slot={})", path, slotIndex);
    } else {
        // Fallback: queue for synchronous load in FlushPendingLoads.
        m_pendingTexturePaths.push_back(path);
        UZ_LOG(Renderer, Debug, "SceneRenderer: queued texture load '{}' (slot={})", path, slotIndex);
    }
    return slotIndex;
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
    m_meshMap[key] = index;

    // Queue the CPU mesh for GPU upload on the next FlushPendingLoads().
    m_pendingProceduralMeshes.emplace_back(index, std::move(mesh));

    UZ_LOG(Renderer, Debug, "SceneRenderer: registered procedural mesh '{}' (slot={})", key, index);
    return index;
}

std::string SceneRenderer::GetTexturePath(uint32_t textureId) const {
    for (const auto& [path, id] : m_textureMap) {
        if (id == textureId) return path;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// FlushPendingLoads — batch GPU upload of async-decoded textures
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::FlushPendingLoads(IRHICommandList* cmdList, IVFS* vfs) {
    UZ_PROFILE_FUNCTION();

    // ── Flush pending mesh uploads (deferred — batched into single submission) ──
    for (const std::string& path : m_pendingMeshPaths) {
        const uint32_t slotIndex = m_meshMap.at(path);
        Mesh cpuMesh = uz::LoadMesh(path);
        if (!cpuMesh.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to load queued mesh '{}'", path);
            continue;
        }
        DeferredMeshUpload deferred = UploadMeshDeferred(m_device, cmdList, cpuMesh);
        if (!deferred.mesh.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to upload queued mesh '{}'", path);
            continue;
        }

        m_meshes[slotIndex] = deferred.mesh;  // copy handles into slot
        m_deferredMeshUploads.push_back(std::move(deferred));  // move staging ownership

        // Queue SDF bake for the Lua-path loaded mesh (same logic as LoadMesh).
        if (cpuMesh.header.vertexFormat == MeshVertexFormat::Standard
            && cpuMesh.GetVertexCount() > 0 && cpuMesh.GetIndexCount() >= 3)
        {
            PendingSdfBake sdfBake;
            sdfBake.meshIndex    = slotIndex;
            sdfBake.vertexCount  = cpuMesh.GetVertexCount();
            sdfBake.vertexStride = cpuMesh.header.vertexStride;
            sdfBake.indexCount   = cpuMesh.GetIndexCount();
            sdfBake.vertexData   = std::move(cpuMesh.vertexData);
            sdfBake.indexData    = std::move(cpuMesh.indexData);
            m_pendingSdfBakes.push_back(std::move(sdfBake));
        }

        UZ_LOG(Renderer, Info,
            "SceneRenderer: flushed mesh '{}' (id={}, {} verts, {} indices)",
            path, slotIndex, m_meshes[slotIndex].vertexCount, m_meshes[slotIndex].indexCount);
    }
    m_pendingMeshPaths.clear();

    // ── Flush procedural meshes (CPU-generated, queued by RegisterProceduralMesh) ──
    for (auto& [meshIndex, cpuMesh] : m_pendingProceduralMeshes) {
        DeferredMeshUpload deferred = UploadMeshDeferred(m_device, cmdList, cpuMesh);
        if (!deferred.mesh.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to upload procedural mesh index {}", meshIndex);
            continue;
        }
        m_meshes[meshIndex] = deferred.mesh;
        m_deferredMeshUploads.push_back(std::move(deferred));

        UZ_LOG(Renderer, Info,
            "SceneRenderer: flushed procedural mesh (id={}, {} verts, {} indices)",
            meshIndex, m_meshes[meshIndex].vertexCount, m_meshes[meshIndex].indexCount);
    }
    m_pendingProceduralMeshes.clear();

    // ── Batch-flush deferred mesh uploads in a single GPU submission ──────
    if (!m_deferredMeshUploads.empty()) {
        FlushDeferredMeshUploads(m_device, cmdList, m_deferredMeshUploads);
        m_deferredMeshUploads.clear();
    }

    // ── Batch SDF bake for queued meshes ─────────────────────────────────
    if (!m_pendingSdfBakes.empty()) {
        FlushSdfBakes(cmdList);
    }

    // ── Flush pending synchronous texture uploads (fallback path) ───────
    // Only used if m_asyncDecoder was not available when QueueTextureLoad
    // was called.
    for (const std::string& path : m_pendingTexturePaths) {
        const uint32_t slotIndex = m_textureMap.at(path);
        TextureLoadResult result = TextureLoader::Load(m_device, cmdList, path);
        if (!result.handle.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to load queued texture '{}'", path);
            continue;
        }
        m_textures[slotIndex] = result;
        UZ_LOG(Renderer, Info,
            "SceneRenderer: flushed texture '{}' (id={}, {}x{}, srv={})",
            path, slotIndex, result.width, result.height, result.srvIndex);
    }
    m_pendingTexturePaths.clear();

    // ── Poll async decoder and batch-upload completed textures ──────────
    // Upload up to kMaxUploadsPerFrame textures per frame to avoid stalling
    // the render thread with a large WaitIdle. Remaining decoded textures
    // are kept in m_deferredDecoded for the next frame. This spreads the
    // GPU upload cost across multiple frames instead of one large stall.
    if (!m_asyncDecoder) return;

    // Poll completed decodes (non-blocking). Whatever is ready gets uploaded
    // this frame; remaining in-flight decodes are picked up next frame.
    {
        auto newlyDecoded = m_asyncDecoder->PollCompleted();
        for (auto& d : newlyDecoded) {
            m_deferredDecoded.push_back(std::move(d));
        }

        // Non-blocking: upload whatever is ready this frame. Remaining
        // in-flight decodes will be picked up on the next FlushPendingLoads()
        // call, spreading GPU upload cost across frames instead of stalling.
    }

    if (m_deferredDecoded.empty()) {
        // No new textures — but check if we have a pending batch to flush
        // (all decodes complete, nothing more coming)
        if (m_textureBatchCount > 0 && m_asyncDecoder->PendingCount() == 0) {
            cmdList->End();
            cmdList->Execute();
            m_device->WaitIdle();
            m_textureBatchUpload->ReleaseStaging();
            m_textureBatchUpload.reset();
            cmdList->Begin();
            UZ_LOG(Renderer, Info,
                   "SceneRenderer: final texture batch flush ({} textures, 1 WaitIdle)",
                   m_textureBatchCount);
            m_textureBatchCount = 0;
        }
        return;
    }

    // Ensure persistent upload context exists
    if (!m_textureBatchUpload) {
        m_textureBatchUpload = std::make_unique<UploadContext>(m_device);
    }

    const uint32_t toProcess = std::min(
        static_cast<uint32_t>(m_deferredDecoded.size()),
        kMaxTextureUploadsPerFrame);

    UZ_LOG(Renderer, Debug, "SceneRenderer: uploading {}/{} decoded textures this frame",
           toProcess, m_deferredDecoded.size());

    uint32_t uploadedCount = 0;
    uint32_t processed = 0;

    for (uint32_t i = 0; i < toProcess; ++i) {
        auto& d = m_deferredDecoded[i];

        auto pendingIt = m_asyncPendingSlots.find(d.path);
        if (pendingIt == m_asyncPendingSlots.end()) {
            if (d.pixels) {
                stbi_image_free(d.pixels);
                d.pixels = nullptr;
            }
            for (auto* mip : d.mipData) {
                if (mip) std::free(mip);
            }
            d.mipData.clear();
            UZ_LOG(Renderer, Warning,
                   "SceneRenderer: async decode completed for unknown path '{}' — discarding",
                   d.path);
            ++processed;
            continue;
        }

        const uint32_t slotIndex = pendingIt->second;

        if (!d.pixels) {
            UZ_LOG(Renderer, Error,
                   "SceneRenderer: async decode failed for '{}' (slot={})",
                   d.path, slotIndex);
            m_asyncPendingSlots.erase(pendingIt);
            ++processed;
            continue;
        }

        TextureLoadResult result;
        if (!d.mipData.empty() && d.mipCount > 1) {
            UZ_LOG(Renderer, Debug,
                   "SceneRenderer: using pre-computed mips for '{}' ({} mips)",
                   d.path, d.mipCount);
            result = TextureLoader::UploadPixelsWithMipsDeferred(
                m_device, cmdList, d.pixels, d.width, d.height,
                d.path.c_str(), m_textureBatchUpload.get(),
                d.mipData.data(), d.mipWidths.data(), d.mipHeights.data(),
                d.mipCount);
        } else {
            result = TextureLoader::UploadPixelsDeferred(
                m_device, cmdList, d.pixels, d.width, d.height,
                d.path.c_str(), m_textureBatchUpload.get());
        }

        stbi_image_free(d.pixels);
        d.pixels = nullptr;

        for (auto* mip : d.mipData) {
            if (mip) std::free(mip);
        }
        d.mipData.clear();

        if (result.handle.IsValid()) {
            m_textures[slotIndex] = result;
            ++uploadedCount;
            UZ_LOG(Renderer, Info,
                   "SceneRenderer: async texture uploaded '{}' (id={}, {}x{}, srv={})",
                   d.path, slotIndex, result.width, result.height, result.srvIndex);
        } else {
            UZ_LOG(Renderer, Error,
                   "SceneRenderer: GPU upload failed for async-decoded '{}' (slot={})",
                   d.path, slotIndex);
        }

        m_asyncPendingSlots.erase(pendingIt);
        ++processed;
    }

    // Remove processed entries from the front of the deferred queue.
    if (processed > 0) {
        m_deferredDecoded.erase(m_deferredDecoded.begin(),
                                m_deferredDecoded.begin() + processed);
    }

    m_textureBatchCount += uploadedCount;

    // Flush conditions:
    // 1. Accumulated >=4 textures in the batch, OR
    // 2. All async decodes are complete (no more textures coming)
    static constexpr uint32_t kFlushThreshold = 4;
    const bool allDecodesComplete = m_asyncDecoder->PendingCount() == 0
                                     && m_deferredDecoded.empty();
    const bool batchFull = m_textureBatchCount >= kFlushThreshold;

    if (m_textureBatchCount > 0 && (batchFull || allDecodesComplete)) {
        cmdList->End();
        cmdList->Execute();
        m_device->WaitIdle();
        m_textureBatchUpload->ReleaseStaging();
        m_textureBatchUpload.reset();
        cmdList->Begin();

        UZ_LOG(Renderer, Info,
               "SceneRenderer: texture batch flush ({} textures, 1 WaitIdle)",
               m_textureBatchCount);
        m_textureBatchCount = 0;
    }
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
    return true;
}

bool SceneRenderer::IsSceneFullyLoadedDebug() const {
    UZ_LOG(Renderer, Debug,
           "IsSceneFullyLoaded: meshPaths={}, procMeshes={}, texPaths={}, asyncPending={}, deferred={}, batch={}",
           m_pendingMeshPaths.size(), m_pendingProceduralMeshes.size(),
           m_pendingTexturePaths.size(),
           m_asyncDecoder ? m_asyncDecoder->PendingCount() : 0,
           m_deferredDecoded.size(), m_textureBatchCount);
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
    UZ_LOG(Renderer, Info, "SceneRenderer: SDF batch bake complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

// CreateShadowAtlas() moved to ShadowAtlas::Init() in shadows/shadow_atlas.cpp

void SceneRenderer::DestroyAssets() {
    // Release any in-flight texture batch upload before destroying textures
    if (m_textureBatchUpload) {
        m_textureBatchUpload->ReleaseStaging();
        m_textureBatchUpload.reset();
    }
    m_textureBatchCount = 0;

    for (auto& mesh : m_meshes) {
        DestroyGPUMesh(m_device, mesh);
    }
    m_meshes.clear();
    m_meshMap.clear();

    for (auto& tex : m_textures) {
        if (tex.handle.IsValid()) {
            m_device->DestroyTexture(tex.handle);
        }
    }
    m_textures.clear();
    m_textureMap.clear();
}

} // namespace uz
