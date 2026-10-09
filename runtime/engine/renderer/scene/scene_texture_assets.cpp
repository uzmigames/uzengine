/// UzEngine Scene Renderer — Texture asset loading (LoadTexture,
/// QueueTextureLoad, GetTexturePath, PreloadMaterialTextures, FlushPendingLoads).
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
#include "material_interface.h"
#include "mesh.h"
#include "gpu_mesh.h"
#include "texture_loader.h"
#include "upload_context.h"
#include "upload_ring.h"
#include "vfs.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

#include <stb_image.h>

#include <string>

#include "cvar.h"

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_MaxTexUploads = UZ_CVAR_INT("r.Scene.MaxTextureUploadsPerFrame", 8, "Max async texture uploads per frame", 1, 32);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// HasPendingTextureLoads — texture-only readiness query for pre-load drain
// ─────────────────────────────────────────────────────────────────────────────

bool SceneRenderer::HasPendingTextureLoads() const {
    if (!m_asyncPendingSlots.empty()) return true;
    if (!m_pendingCopyUploads.empty()) return true;
    if (m_asyncDecoder && m_asyncDecoder->PendingCount() > 0) return true;
    if (!m_deferredDecoded.empty()) return true;
    return false;
}

void SceneRenderer::DrainPendingTextureLoads(IRHICommandList* cmdList, IVFS* vfs) {
    if (!cmdList || !m_device) return;

    constexpr uint32_t kMaxIterations = 500;
    uint32_t iter = 0;

    while (HasPendingTextureLoads() && iter < kMaxIterations) {
        // Poll async decoder for completed results
        if (m_asyncDecoder) {
            auto results = m_asyncDecoder->PollCompleted();
            for (auto& r : results) {
                m_deferredDecoded.push_back(std::move(r));
            }
        }

        // Flush: finalize copy queue uploads + upload new decoded textures
        FlushPendingLoads(cmdList, vfs);

        // If we uploaded anything on the copy queue, submit + wait
        if (m_copyCmdList && m_copyFenceValue > 0) {
            m_device->WaitIdle();
        }

        // Yield briefly if nothing was available to process
        if (m_deferredDecoded.empty() && m_asyncPendingSlots.empty()
            && (!m_asyncDecoder || m_asyncDecoder->PendingCount() == 0)) {
            break;
        }

        // Small sleep to avoid busy-spinning while waiting for decoder workers
        if (m_asyncDecoder && m_asyncDecoder->PendingCount() > 0
            && m_deferredDecoded.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        ++iter;
    }

    UZ_LOG(Renderer, Info, "DrainPendingTextureLoads: completed in {} iterations", iter);
}

/// Maximum textures to upload per frame. Limits the number of staging buffer
/// creations + GPU copies per frame to avoid main-thread stalls during the
/// End/Execute/WaitIdle flush. Each 2K BC7 texture upload takes ~2-3ms of
/// command recording, so 4/frame keeps the stall under 12ms.
static constexpr uint32_t kMaxTextureUploadsPerFrame = 8;

// ─────────────────────────────────────────────────────────────────────────────
// Asset Loading — Textures (async decode + deferred GPU upload)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SceneRenderer::LoadTexture(const std::string& path, IVFS* vfs,
                                     IRHICommandList* cmdList, bool isSRGB) {
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

    // Reserve a slot with a fallback placeholder so draw commands have a valid
    // SRV from the first frame. The full-resolution texture replaces this once
    // async loading + copy queue upload completes. Using the existing fallback
    // textures (1x1 white or 1x1 flat normal) avoids creating per-slot resources.
    const auto slotIndex = static_cast<uint32_t>(m_textures.size());
    {
        TextureLoadResult placeholder{};
        // Normal maps use the flat-normal fallback (128,128,255); everything
        // else uses white (identity for albedo/roughness/metallic/AO).
        const bool isNormalMap = (path.find("Normal") != std::string::npos
                               || path.find("normal") != std::string::npos
                               || path.find("_N.") != std::string::npos
                               || path.find("_n.") != std::string::npos);
        placeholder.srvIndex = isNormalMap ? m_fallbackNormalSrvIndex
                                           : m_fallbackWhiteSrvIndex;
        placeholder.handle = isNormalMap ? m_fallbackNormal : m_fallbackWhite;
        placeholder.width = 1;
        placeholder.height = 1;
        placeholder.mipLevels = 1;
        placeholder.loadState = TextureLoadState::Placeholder;
        m_textures.push_back(placeholder);
    }
    m_textureMap[path] = slotIndex;

    // ── Resolve texture path and submit to async worker ─────────────────
    // Strategy: move ALL file I/O to worker threads. The main thread only
    // enqueues a request and returns immediately.
    //
    // Priority:
    //   1. Cooked .uztexture companion on filesystem (BC7, zero CPU decode)
    //   2. Raw source file on filesystem (PNG/JPG, worker decodes via stbi)
    //   3. VFS path (worker reads from thread-safe VFS on background thread)
    //   4. Sync fallback (no async decoder — rare, only if decoder init failed)

    const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                             (path.size() > 1 && path[1] == ':'));

    // Build cooked companion path: "foo.jpg" → "foo.uztexture"
    std::string cookedPath;
    {
        auto dot = path.rfind('.');
        if (dot != std::string::npos) {
            cookedPath = path.substr(0, dot) + ".uztexture";
        }
    }

    // Try filesystem paths first — worker thread does I/O, main thread is free
    std::string fsPath;
    if (!cookedPath.empty()) {
        std::string cookedFs = (vfs && !isAbsolute) ? vfs->ResolvePath(cookedPath) : cookedPath;
        if (std::ifstream(cookedFs, std::ios::binary).good()) {
            fsPath = cookedFs;
        }
    }
    if (fsPath.empty()) {
        std::string rawFs = (vfs && !isAbsolute) ? vfs->ResolvePath(path) : path;
        if (std::ifstream(rawFs, std::ios::binary).good()) {
            fsPath = rawFs;
        }
    }

    if (!fsPath.empty() && m_asyncDecoder) {
        // Fast path: submit filesystem path → worker does ALL I/O in parallel
        m_asyncDecoder->Submit(fsPath, isSRGB, path);
        m_asyncPendingSlots[path] = slotIndex;
    } else if (vfs && m_asyncDecoder) {
        // VFS path: worker reads from thread-safe VFS on background thread.
        // Prefer cooked .uztexture if it exists in VFS, otherwise use raw path.
        // The VFS read path may differ from the key path (cooked vs raw),
        // so we store a separate read path in the request but keep the
        // original path as the result key for pending slot lookup.
        std::string vfsReadPath = path;
        if (!cookedPath.empty() && vfs->Exists(cookedPath)) {
            vfsReadPath = cookedPath;
        }
        m_asyncDecoder->SubmitVFS(vfs, vfsReadPath, isSRGB, path);
        m_asyncPendingSlots[path] = slotIndex;
    } else if (vfs && vfs->Exists(path)) {
        // Sync fallback: no async decoder available — read and upload on main thread.
        std::vector<uint8_t> vfsData = vfs->Read(path);
        if (!vfsData.empty()) {
            TextureLoadResult result = TextureLoader::LoadFromMemory(
                m_device, cmdList, vfsData.data(), vfsData.size(), path.c_str(), isSRGB);
            if (result.handle.IsValid()) {
                m_textures[slotIndex] = result;
            } else {
                UZ_LOG(Renderer, Error, "SceneRenderer: failed to load texture (VFS sync): '{}'", path);
            }
        }
    } else if (m_asyncDecoder) {
        // Last resort: submit path as-is to worker (filesystem path, no VFS)
        std::string rawPath = (vfs && !isAbsolute) ? vfs->ResolvePath(path) : path;
        m_asyncDecoder->Submit(rawPath, isSRGB, path);
        m_asyncPendingSlots[path] = slotIndex;
    }

    return slotIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// PreloadMaterialTextures — submit all texture paths to async decoder immediately
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::PreloadMaterialTextures(MaterialInterface* mat, IVFS* vfs,
                                             IRHICommandList* cmdList) {
    if (!mat) return;

    // Iterate all known texture slot names and preload any that have a path.
    // Uses the generic IsTextureSlotSRGB() so this works for any shading model.
    static const char* const kSlotNames[] = {
        "Albedo", "Normal", "Roughness", "Metallic", "Ao", "Emissive", "ORM",
        // Future shading model slots (no-op if path is empty):
        "ScatterColor", "SubsurfaceColor", "FuzzColor", "ClothColor",
        "Thickness", "Opacity", "DetailAlbedo", "DetailNormal",
        "ClearCoatNormal", "Tangent", "Heightmap"
    };

    for (const char* slot : kSlotNames) {
        const std::string& path = mat->GetTexturePath(slot);
        if (!path.empty()) {
            (void)LoadTexture(path, vfs, cmdList, mat->IsTextureSlotSRGB(slot));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Deferred Load Queue — Textures (Lua API)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SceneRenderer::QueueTextureLoad(const std::string& path, bool isSRGB) {
    if (path.empty()) return UINT32_MAX;

    // Return cached ID immediately — no need to queue.
    auto it = m_textureMap.find(path);
    if (it != m_textureMap.end()) {
        return it->second;
    }

    // Reserve a stable slot with a fallback placeholder (same logic as LoadTexture).
    const uint32_t slotIndex = static_cast<uint32_t>(m_textures.size());
    {
        TextureLoadResult placeholder{};
        const bool isNormalMap = (path.find("Normal") != std::string::npos
                               || path.find("normal") != std::string::npos
                               || path.find("_N.") != std::string::npos
                               || path.find("_n.") != std::string::npos);
        placeholder.srvIndex = isNormalMap ? m_fallbackNormalSrvIndex
                                           : m_fallbackWhiteSrvIndex;
        placeholder.handle = isNormalMap ? m_fallbackNormal : m_fallbackWhite;
        placeholder.width = 1;
        placeholder.height = 1;
        placeholder.mipLevels = 1;
        placeholder.loadState = TextureLoadState::Placeholder;
        m_textures.push_back(placeholder);
    }
    m_textureMap[path] = slotIndex;

    // Submit to async decoder for background CPU decode.
    // Use VFS-backed submission when VFS is available (set during last RenderFrame).
    // The resultKey = original path ensures m_asyncPendingSlots lookup works.
    if (m_asyncDecoder) {
        if (m_lastVfs) {
            m_asyncDecoder->SubmitVFS(m_lastVfs, path, isSRGB, path);
        } else {
            m_asyncDecoder->Submit(path, isSRGB, path);
        }
        m_asyncPendingSlots[path] = slotIndex;
        UZ_LOG(Renderer, Debug, "SceneRenderer: queued async texture load '{}' (slot={})", path, slotIndex);
    } else {
        // Fallback: queue for synchronous load in FlushPendingLoads.
        m_pendingTexturePaths.push_back(path);
        UZ_LOG(Renderer, Debug, "SceneRenderer: queued texture load '{}' (slot={})", path, slotIndex);
    }
    return slotIndex;
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

    // Reset the upload ring for this frame's suballocations. The swapchain
    // fence guarantees frame (N - frameCount) has completed, so this frame's
    // ring region is safe to overwrite.
    if (m_uploadRing && m_uploadRing->IsInitialized()) {
        m_uploadRing->BeginFrame(m_uploadRingFrameIndex++);
    }

    // ── Finalize previous frame's copy queue uploads ─────────────────────
    // If the copy queue had in-flight texture uploads last frame, wait for the
    // copy fence on the GPU side (no CPU stall) and transition the textures to
    // ShaderResource state on the graphics command list. Only then do the SRV
    // slots become usable for rendering.
    if (m_copyFenceValue > 0 && !m_pendingCopyUploads.empty()) {
        UZ_PROFILE_SCOPE("FlushPendingLoads::FinalizeCopyQueue");

        m_device->GraphicsWaitForCopy(m_copyFenceValue);

        for (auto& pending : m_pendingCopyUploads) {
            // Transition from CopyDst to ShaderResource on the graphics queue
            RHIBarrierDesc barrier{};
            barrier.texture = pending.result.handle;
            barrier.stateBefore = RHIResourceState::CopyDst;
            barrier.stateAfter = RHIResourceState::ShaderResource;
            cmdList->ResourceBarrier(&barrier, 1);

            // Update the texture slot with the real SRV and dimensions
            if (pending.slotIndex < static_cast<uint32_t>(m_textures.size())) {
                pending.result.loadState = TextureLoadState::FullyLoaded;
                m_textures[pending.slotIndex] = pending.result;
            }
        }

        UZ_LOG(Renderer, Info,
               "SceneRenderer: finalized {} copy queue texture uploads (fence={})",
               m_pendingCopyUploads.size(), m_copyFenceValue);

        m_pendingCopyUploads.clear();

        // Release staging buffers from the previous upload batch — the copy
        // fence guarantees the GPU is done reading from them.
        if (m_textureBatchUpload) {
            // Hand the previous batch's staging buffers to the deferred-destroy
            // queue rather than freeing them here. GraphicsWaitForCopy() above is
            // a GPU-side queue wait; the copy queue may still be reading these
            // buffers when this CPU code runs, and ReleaseStaging() surfaced as an
            // intermittent GPU page fault on 'TextureStagingBuffer'.
            for (RHIBufferHandle staging : m_textureBatchUpload->TakeStagingBuffers()) {
                QueueBufferDestroy(staging);
            }
            m_textureBatchUpload.reset();
        }

        m_copyFenceValue = 0;
        m_textureBatchCount = 0;
    }

    // ── Flush pending mesh uploads (deferred — batched into single submission) ──
    for (const std::string& path : m_pendingMeshPaths) {
        const uint32_t slotIndex = m_meshMap.at(path);
        Mesh cpuMesh{};
        // Try VFS first (consistent with LoadMesh), then fall back to filesystem.
        if (vfs && vfs->Exists(path)) {
            std::vector<uint8_t> data = vfs->Read(path);
            if (!data.empty()) {
                cpuMesh = uz::LoadMeshFromMemory(data, path);
            }
        }
        if (!cpuMesh.IsValid()) {
            // Filesystem fallback: resolve via VFS or use as-is.
            const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                                     (path.size() > 1 && path[1] == ':'));
            const std::string resolvedPath = (vfs && !isAbsolute)
                ? vfs->ResolvePath(path) : path;
            cpuMesh = uz::LoadMesh(resolvedPath);
        }
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
        m_meshBoundsMin[slotIndex] = cpuMesh.header.boundsMin;
        m_meshBoundsMax[slotIndex] = cpuMesh.header.boundsMax;
        m_deferredMeshUploads.push_back(std::move(deferred));  // move staging ownership

        // Queue the SDF bake for the Lua-path loaded mesh (same logic as LoadMesh; block B.3).
        if (m_sdfGiEnabled
            && cpuMesh.header.vertexFormat == MeshVertexFormat::Standard
            && cpuMesh.GetVertexCount() > 0 && cpuMesh.GetIndexCount() >= 3)
        {
            QueueSdfBake(slotIndex, cpuMesh);
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
    // (the queued bakes are serviced once per frame at the end of the sync: ServiceSdfBakes)

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
            UZ_PROFILE_SCOPE("FlushPendingLoads::FinalBatchFlush");

            if (m_copyCmdList) {
                // Submit remaining copies on the DMA queue — no CPU stall.
                // Barriers and SRV updates happen in the finalize block next frame.
                m_copyCmdList->End();
                m_copyCmdList->Execute();
                m_copyFenceValue = m_device->SignalCopyQueue();
                m_copyCmdList->Begin();

                UZ_LOG(Renderer, Info,
                       "SceneRenderer: final texture batch submitted to copy queue "
                       "({} textures, fence={})",
                       m_textureBatchCount, m_copyFenceValue);
            } else {
                // Fallback: no copy queue — use graphics queue with WaitIdle.
                auto flushStart = std::chrono::high_resolution_clock::now();

                cmdList->End();
                cmdList->Execute();
                m_device->WaitIdle();
                m_textureBatchUpload->ReleaseStaging();
                m_textureBatchUpload.reset();
                cmdList->Begin();

                auto flushEnd = std::chrono::high_resolution_clock::now();
                const auto flushUs = std::chrono::duration_cast<std::chrono::microseconds>(flushEnd - flushStart).count();
                const float flushMs = static_cast<float>(flushUs) / 1000.0f;
                UZ_LOG(Renderer, Info,
                       "SceneRenderer: final texture batch flush ({} textures, 1 WaitIdle, {:.1f}ms)",
                       m_textureBatchCount, flushMs);
                m_textureBatchCount = 0;
            }
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

        // When a copy command list is available, record uploads on the DMA queue
        // and skip barriers (they are issued on the graphics queue next frame).
        // Otherwise, fall back to the graphics command list with immediate barriers.
        const bool useCopyQueue = (m_copyCmdList != nullptr);
        IRHICommandList* uploadCmdList = useCopyQueue ? m_copyCmdList : cmdList;
        const bool skipBarrier = useCopyQueue;

        TextureLoadResult result;
        if (!d.uzTexData.empty()) {
            // .uztexture: pre-cooked GPU-ready data — upload directly.
            // Prefer the ring path (zero CreateBuffer calls) over UploadContext.
            // Falls back to UploadContext if the ring is not available or exhausted.
            if (m_uploadRing && m_uploadRing->IsInitialized()) {
                result = TextureLoader::LoadUzTexViaRing(
                    m_device, uploadCmdList, d.uzTexData.data(), d.uzTexData.size(),
                    d.path.c_str(), *m_uploadRing, skipBarrier);
            }
            if (!result.handle.IsValid()) {
                // Ring unavailable or exhausted — fall back to per-mip staging buffers.
                result = TextureLoader::LoadUzTex(
                    m_device, uploadCmdList, d.uzTexData.data(), d.uzTexData.size(),
                    d.path.c_str(), m_textureBatchUpload.get(), skipBarrier);
            }
            d.uzTexData.clear();
            d.uzTexData.shrink_to_fit();
        } else if (!d.pixels) {
            // Decode failed (null pixels, no uzTexData) — skip
            UZ_LOG(Renderer, Error,
                   "SceneRenderer: async decode failed for '{}' (slot={})",
                   d.path, slotIndex);
            m_asyncPendingSlots.erase(pendingIt);
            ++processed;
            continue;
        } else if (!d.mipData.empty() && d.mipCount > 1) {
            UZ_LOG(Renderer, Debug,
                   "SceneRenderer: using pre-computed mips for '{}' ({} mips, sRGB={})",
                   d.path, d.mipCount, d.isSRGB);
            result = TextureLoader::UploadPixelsWithMipsDeferred(
                m_device, uploadCmdList, d.pixels, d.width, d.height,
                d.path.c_str(), m_textureBatchUpload.get(),
                d.mipData.data(), d.mipWidths.data(), d.mipHeights.data(),
                d.mipCount, d.isSRGB, skipBarrier);
        } else {
            result = TextureLoader::UploadPixelsDeferred(
                m_device, uploadCmdList, d.pixels, d.width, d.height,
                d.path.c_str(), m_textureBatchUpload.get(), d.isSRGB,
                skipBarrier);
        }

        // Free stbi-allocated pixel data (null for .uztexture path).
        if (d.pixels) {
            stbi_image_free(d.pixels);
            d.pixels = nullptr;
        }

        for (auto* mip : d.mipData) {
            if (mip) std::free(mip);
        }
        d.mipData.clear();

        if (result.handle.IsValid()) {
            if (useCopyQueue) {
                // Defer the slot update — the texture is still in CopyDst state
                // on the copy queue. The finalize block next frame will barrier
                // and populate the slot after the copy fence completes.
                result.loadState = TextureLoadState::CopyInFlight;
                m_pendingCopyUploads.push_back({slotIndex, result});
            } else {
                // Graphics queue path: barrier was already issued, slot is ready.
                result.loadState = TextureLoadState::FullyLoaded;
                m_textures[slotIndex] = result;
            }
            ++uploadedCount;
            UZ_LOG(Renderer, Info,
                   "SceneRenderer: async texture uploaded '{}' (id={}, {}x{}, srv={}, copyQ={})",
                   d.path, slotIndex, result.width, result.height, result.srvIndex,
                   useCopyQueue);
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
    // Flush the GPU upload batch whenever we uploaded textures this frame.
    // With async VFS, textures trickle in 1-4 per frame as workers complete
    // reads. Flushing every frame keeps textures visible ASAP instead of
    // waiting for a large batch that may never accumulate.
    if (m_textureBatchCount > 0 && uploadedCount > 0) {
        UZ_PROFILE_SCOPE("FlushPendingLoads::BatchFlush");

        if (m_copyCmdList) {
            // Submit texture copy commands on the DMA queue — zero CPU stall.
            // The copy command list was used for all upload calls above.
            // Barriers and m_textures[] updates happen in the finalize block
            // at the start of the next FlushPendingLoads call.
            m_copyCmdList->End();
            m_copyCmdList->Execute();
            m_copyFenceValue = m_device->SignalCopyQueue();
            m_copyCmdList->Begin();

            UZ_LOG(Renderer, Info,
                   "SceneRenderer: texture batch submitted to copy queue "
                   "({} textures, fence={})",
                   m_textureBatchCount, m_copyFenceValue);
            // Do NOT reset m_textureBatchCount here — the finalize block does it.
        } else {
            // Fallback: no copy queue — flush on graphics queue with WaitIdle.
            auto flushStart = std::chrono::high_resolution_clock::now();

            cmdList->End();
            cmdList->Execute();
            m_device->WaitIdle();
            m_textureBatchUpload->ReleaseStaging();
            m_textureBatchUpload.reset();
            cmdList->Begin();

            auto flushEnd = std::chrono::high_resolution_clock::now();
            const auto flushUs = std::chrono::duration_cast<std::chrono::microseconds>(flushEnd - flushStart).count();
            const float flushMs = static_cast<float>(flushUs) / 1000.0f;
            UZ_LOG(Renderer, Info,
                   "SceneRenderer: texture batch flush ({} textures, 1 WaitIdle, {:.1f}ms)",
                   m_textureBatchCount, flushMs);
            m_textureBatchCount = 0;
        }
    }
}

} // namespace uz
