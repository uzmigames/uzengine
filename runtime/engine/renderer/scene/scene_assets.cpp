/// UzEngine Scene Renderer — Asset loading (LoadMesh, LoadTexture, Queue*, DestroyAssets).
///
/// Texture loading uses a two-phase async pipeline:
///   1. Submit phase: `LoadTexture()` / `QueueTextureLoad()` reserve a slot in
///      m_textures and submit the path to the AsyncTextureDecoder for background
///      CPU decode (stbi_load on worker threads).
///   2. Flush phase: `FlushPendingLoads()` polls completed decodes and batch-
///      uploads all decoded pixels to the GPU in a single command list submission
///      (one WaitIdle for the entire batch instead of one per texture).
///
/// This provides a 2-4x speedup for scenes with many textures (e.g., 45 textures
/// for 9 PBR materials) by overlapping CPU decode work across 2 worker threads
/// and batching GPU uploads.

#include "scene_renderer.h"

#include "async_texture_decoder.h"
#include "mesh.h"
#include "gpu_mesh.h"
#include "gi/mesh_sdf_baker.h"
#include "texture_loader.h"
#include "upload_context.h"
#include "vfs.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <stb_image.h>

#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Asset Loading — Meshes (unchanged, synchronous)
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
        // Resolve via VFS for relative paths so demos using VFS-relative asset names work.
        const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                                 (path.size() > 1 && path[1] == ':'));
        const std::string resolvedPath = (vfs && !isAbsolute)
            ? vfs->ResolvePath(path)
            : path;
        cpuMesh = uz::LoadMesh(resolvedPath);
    }
    if (!cpuMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to load mesh: '{}'", path);
        return UINT32_MAX;
    }

    GPUMesh gpuMesh = UploadMesh(m_device, cmdList, cpuMesh);
    if (!gpuMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to upload mesh: '{}'", path);
        return UINT32_MAX;
    }

    // ── Bake mesh SDF for GI system (Global Distance Field composite) ────
    // Extract vertex positions and indices from the CPU mesh for the SDF baker.
    // Position is the first 3 floats at each vertex (Vertex::position = Vec3).
    // The baker produces a dense R16F 3D texture for sphere-march GI tracing.
    if (cpuMesh.header.vertexFormat == MeshVertexFormat::Standard
        && cpuMesh.GetVertexCount() > 0 && cpuMesh.GetIndexCount() >= 3)
    {
        const auto* positions = reinterpret_cast<const float*>(cpuMesh.vertexData.data());
        const uint32_t vertexStride = cpuMesh.header.vertexStride;
        const auto* indices = reinterpret_cast<const uint32_t*>(cpuMesh.indexData.data());

        MeshSDFData sdfData = MeshSdfBaker::BakeMesh(
            positions,
            cpuMesh.GetVertexCount(),
            vertexStride,
            indices,
            cpuMesh.GetIndexCount(),
            32); // 32^3 resolution — balanced quality vs bake time

        if (sdfData.IsValid()) {
            gpuMesh.sdf = MeshSdfBaker::UploadToGPU(m_device, cmdList, sdfData);
            if (gpuMesh.sdf.valid) {
                UZ_LOG(Renderer, Info,
                       "SceneRenderer: baked mesh SDF '{}' (res={}, SRV={})",
                       path, sdfData.resolution, gpuMesh.sdf.srvIndex);
            } else {
                UZ_LOG(Renderer, Warning,
                       "SceneRenderer: SDF GPU upload failed for '{}'", path);
            }
        } else {
            UZ_LOG(Renderer, Warning,
                   "SceneRenderer: SDF bake failed for '{}' ({} verts, {} indices)",
                   path, cpuMesh.GetVertexCount(), cpuMesh.GetIndexCount());
        }
    }

    const auto index = static_cast<uint32_t>(m_meshes.size());
    m_meshes.push_back(gpuMesh);
    m_meshMap[path] = index;

    UZ_LOG(Renderer, Info, "SceneRenderer: loaded mesh '{}' (id={}, {} vertices, {} indices)",
           path, index, gpuMesh.vertexCount, gpuMesh.indexCount);
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

    // Resolve via VFS for relative paths
    const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                             (path.size() > 1 && path[1] == ':'));
    const std::string resolvedPath = (vfs && !isAbsolute)
        ? vfs->ResolvePath(path)
        : path;

    // Check if already submitted for async decode (by resolved path)
    if (m_asyncPendingSlots.find(resolvedPath) != m_asyncPendingSlots.end()) {
        // Already in flight — return the reserved slot index.
        // The slot has an invalid placeholder that will be filled when the
        // decode completes and FlushPendingLoads uploads it.
        return m_textureMap.at(path);
    }

    // Reserve a slot with an invalid placeholder. The texture will become
    // usable after FlushPendingLoads() uploads the decoded pixels.
    const auto slotIndex = static_cast<uint32_t>(m_textures.size());
    m_textures.emplace_back(); // placeholder (handle.IsValid() == false)
    m_textureMap[path] = slotIndex;

    // Also register the resolved path so we can match decode results back
    // to the correct slot. If path != resolvedPath, register both mappings.
    if (resolvedPath != path) {
        m_textureMap[resolvedPath] = slotIndex;
    }

    // Submit for background CPU decode
    if (m_asyncDecoder) {
        m_asyncDecoder->Submit(resolvedPath);
        m_asyncPendingSlots[resolvedPath] = slotIndex;
        UZ_LOG(Renderer, Debug, "SceneRenderer: submitted async texture decode '{}' (slot={})",
               resolvedPath, slotIndex);
    } else {
        // Fallback: no async decoder available (shouldn't happen after Init).
        // Do synchronous load to avoid silent data loss.
        TextureLoadResult result = TextureLoader::Load(m_device, cmdList, resolvedPath);
        if (result.handle.IsValid()) {
            m_textures[slotIndex] = result;
            UZ_LOG(Renderer, Info, "SceneRenderer: loaded texture (sync fallback) '{}' (id={}, {}x{}, srv={})",
                   path, slotIndex, result.width, result.height, result.srvIndex);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to load texture: '{}'", path);
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

    // ── Flush pending mesh uploads (synchronous) ──────────────────────
    for (const std::string& path : m_pendingMeshPaths) {
        const uint32_t slotIndex = m_meshMap.at(path);
        Mesh cpuMesh = uz::LoadMesh(path);
        if (!cpuMesh.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to load queued mesh '{}'", path);
            continue;
        }
        GPUMesh gpuMesh = UploadMesh(m_device, cmdList, cpuMesh);
        if (!gpuMesh.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to upload queued mesh '{}'", path);
            continue;
        }

        // Bake mesh SDF for GI system (same logic as LoadMesh).
        if (cpuMesh.header.vertexFormat == MeshVertexFormat::Standard
            && cpuMesh.GetVertexCount() > 0 && cpuMesh.GetIndexCount() >= 3)
        {
            const auto* positions = reinterpret_cast<const float*>(cpuMesh.vertexData.data());
            const auto* indices = reinterpret_cast<const uint32_t*>(cpuMesh.indexData.data());

            MeshSDFData sdfData = MeshSdfBaker::BakeMesh(
                positions, cpuMesh.GetVertexCount(), cpuMesh.header.vertexStride,
                indices, cpuMesh.GetIndexCount(), 32);

            if (sdfData.IsValid()) {
                gpuMesh.sdf = MeshSdfBaker::UploadToGPU(m_device, cmdList, sdfData);
                if (gpuMesh.sdf.valid) {
                    UZ_LOG(Renderer, Info,
                           "SceneRenderer: baked mesh SDF '{}' (res={}, SRV={})",
                           path, sdfData.resolution, gpuMesh.sdf.srvIndex);
                }
            }
        }

        m_meshes[slotIndex] = gpuMesh;
        UZ_LOG(Renderer, Info,
            "SceneRenderer: flushed mesh '{}' (id={}, {} verts, {} indices)",
            path, slotIndex, gpuMesh.vertexCount, gpuMesh.indexCount);
    }
    m_pendingMeshPaths.clear();

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

    // Append newly completed decodes to the deferred queue.
    auto newlyDecoded = m_asyncDecoder->PollCompleted();
    for (auto& d : newlyDecoded) {
        m_deferredDecoded.push_back(std::move(d));
    }

    if (m_deferredDecoded.empty()) return;

    // Limit uploads per frame to avoid GPU stalls.
    // 4 textures/frame at 500 FPS = all 43 textures loaded in ~0.2 seconds
    // with no perceptible frame drops.
    static constexpr uint32_t kMaxUploadsPerFrame = 4;

    const uint32_t toProcess = std::min(
        static_cast<uint32_t>(m_deferredDecoded.size()), kMaxUploadsPerFrame);

    UZ_LOG(Renderer, Debug, "SceneRenderer: uploading {}/{} decoded textures this frame",
           toProcess, m_deferredDecoded.size());

    UploadContext batchUpload(m_device);
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

        TextureLoadResult result = TextureLoader::UploadPixelsDeferred(
            m_device, cmdList, d.pixels, d.width, d.height,
            d.path.c_str(), &batchUpload);

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

    // Flush the command list ONCE for this frame's batch, then wait.
    if (uploadedCount > 0) {
        cmdList->End();
        cmdList->Execute();
        m_device->WaitIdle();
        batchUpload.ReleaseStaging();
        cmdList->Begin();

        UZ_LOG(Renderer, Info,
               "SceneRenderer: batch GPU upload complete ({} textures, 1 WaitIdle)",
               uploadedCount);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

// CreateShadowAtlas() moved to ShadowAtlas::Init() in shadows/shadow_atlas.cpp

void SceneRenderer::DestroyAssets() {
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
