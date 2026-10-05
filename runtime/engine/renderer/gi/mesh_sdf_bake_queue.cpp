/// UzEngine GI — asynchronous, disk-cached mesh SDF bakes (lighting block B, item 3).
/// See mesh_sdf_bake_queue.h for the flow. Threading rules: workers touch only CPU memory and the
/// file system; every RHI call happens in `Service` / `Shutdown` on the main thread.

#include "mesh_sdf_bake_queue.h"
#include "mesh_sdf_bvh.h"

#include "cvar.h"
#include "gpu_mesh.h"
#include "log.h"
#include "mesh.h"
#include "rhi.h"

#include <lz4.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// Block B.3: the disk cache and the per-frame budgets of the asynchronous bake path. The budgets
// apply to the render loop only; the scene preload (nothing presented) runs unthrottled.
static auto* cv_MeshSdfCache = UZ_CVAR_BOOL("r.GI.MeshSdf.Cache", true,
    "Keep baked mesh distance fields on disk (cache/meshsdf) and upload them instead of baking again");
static auto* cv_MeshSdfBakeSlicesPerFrame = UZ_CVAR_INT("r.GI.MeshSdf.BakeSlicesPerFrame", 8,
    "Voxel Z slices of mesh distance fields dispatched per frame across the bakes in flight (multiples of 4)", 4, 1024);
static auto* cv_MeshSdfBakeStartsPerFrame = UZ_CVAR_INT("r.GI.MeshSdf.BakeStartsPerFrame", 1,
    "Mesh distance field bakes or cache uploads started per frame", 1, 64);
static auto* cv_MeshSdfCacheVerify = UZ_CVAR_BOOL("r.GI.MeshSdf.Diagnostic.CacheVerify", false,
    "Diagnostic: bake every cache hit again and log whether the cached volume and records are the same bits");

namespace uz {

namespace {

constexpr uint32_t kCompletionsPerFrame = 4;          // render loop: below r.GI.GDF.MaxRegions (8), regional recomposes only
constexpr uint32_t kCompletionsPerPreloadFrame = 8;   // preload: at the limit, still regional

struct KeyInputs {
    uint64_t vertexHash;
    uint64_t indexHash;
    uint32_t vertexCount;
    uint32_t vertexStride;
    uint32_t indexCount;
    uint32_t resolution;
    uint32_t twoSided;
    uint32_t vertexNormalOffset;
    uint64_t shaderHash;
};

} // namespace

// ───────────────────────────────────────
// Construction / shutdown
// ───────────────────────────────────────

MeshSdfBakeQueue::MeshSdfBakeQueue(IRHIDevice* device, IRHIFence* fence, std::string cacheDirectory,
                                   std::function<uint64_t()> shaderHash, int workerThreads)
    : m_device(device), m_fence(fence), m_cache(std::move(cacheDirectory)), m_shaderHashFn(std::move(shaderHash)) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(m_cache.CacheDirectory()) / "imported", ec);
    m_cacheReady = !ec;
    if (!m_cacheReady) {
        UZ_LOG(Renderer, Warning, "MeshSdfBakeQueue: cannot create '{}': {} — every bake will miss",
               m_cache.CacheDirectory(), ec.message());
    }
    if (!m_fence) UZ_LOG(Renderer, Warning, "MeshSdfBakeQueue: no fence — bakes are marked valid when recorded, as before block B.3");
    const int n = std::clamp(workerThreads, 1, 8);
    m_workers.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) m_workers.emplace_back(&MeshSdfBakeQueue::WorkerLoop, this);
    UZ_LOG(Renderer, Info, "MeshSdfBakeQueue: {} worker threads, cache '{}'", n, m_cache.CacheDirectory());
}

MeshSdfBakeQueue::~MeshSdfBakeQueue() {
    Shutdown();
}

void MeshSdfBakeQueue::Shutdown() {
    if (!m_stop.exchange(true)) {
        m_cv.notify_all();
        for (auto& w : m_workers) {
            if (w.joinable()) w.join();
        }
        m_workers.clear();
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.clear();
    m_completedPrepares.clear();
    m_ready.clear();
    for (auto& bake : m_gpu) DestroyScratch(bake);
    m_gpu.clear();
    m_pendingMeshes.clear();
    for (auto* pool : {&m_freeStaging, &m_freeReadback}) {
        for (auto& [bytes, buffers] : *pool) {
            for (auto& buffer : buffers) {
                if (m_device && buffer.IsValid()) m_device->DestroyBuffer(buffer);
            }
        }
        pool->clear();
    }
    m_queuedPrepares.store(0);
}

size_t MeshSdfBakeQueue::InFlight() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t gpu = 0;
    for (const auto& bake : m_gpu) gpu += bake.verify ? 0u : 1u;
    return m_queuedPrepares.load(std::memory_order_relaxed) + m_preparing.load(std::memory_order_relaxed)
         + m_completedPrepares.size() + m_ready.size() + gpu;
}

// ───────────────────────────────────────
// Submit
// ───────────────────────────────────────

void MeshSdfBakeQueue::Submit(MeshSdfBakeRequest&& request) {
    if (!m_shaderHashKnown) {
        m_shaderHash = m_shaderHashFn ? m_shaderHashFn() : 0;
        m_shaderHashKnown = true;
    }
    ++m_stats.submitted;
    m_pendingMeshes.insert(request.meshIndex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Task task;
        task.kind = Task::Kind::Prepare;
        task.request = std::move(request);
        task.verify = cv_MeshSdfCacheVerify->value != 0.0f;
        task.useCache = m_cacheReady && cv_MeshSdfCache->value != 0.0f;   // CVars are read on the main thread only
        m_tasks.push_back(std::move(task));
        m_queuedPrepares.fetch_add(1, std::memory_order_relaxed);
    }
    m_cv.notify_one();
}

uint64_t MeshSdfBakeQueue::CacheKey(const MeshSdfBakeRequest& request) const {
    KeyInputs in{};
    in.vertexHash = AssetCache::HashBytes(request.vertexData.data(), request.vertexData.size());
    in.indexHash = AssetCache::HashBytes(request.indices.data(), request.indices.size() * sizeof(uint32_t));
    in.vertexCount = request.vertexCount;
    in.vertexStride = request.vertexStride;
    in.indexCount = static_cast<uint32_t>(request.indices.size());
    in.resolution = request.resolution;
    in.twoSided = request.twoSided ? 1u : 0u;
    in.vertexNormalOffset = request.vertexNormalOffset;
    in.shaderHash = m_shaderHash;
    const uint64_t sourceHash = AssetCache::HashBytes(&in, offsetof(KeyInputs, resolution));
    const uint64_t optionsHash = AssetCache::HashBytes(&in.resolution, sizeof(KeyInputs) - offsetof(KeyInputs, resolution));
    return AssetCache::ComputeKey("meshsdf", kMeshSdfCacheVersion, sourceHash, optionsHash);
}

// ───────────────────────────────────────
// Workers
// ───────────────────────────────────────

void MeshSdfBakeQueue::WorkerLoop() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop.load() || !m_tasks.empty(); });
            if (m_stop.load() && m_tasks.empty()) return;
            // Prepares first: a store (disk write) must not delay the field of the next mesh.
            auto it = m_tasks.begin();
            for (auto cand = m_tasks.begin(); cand != m_tasks.end(); ++cand) {
                if (cand->kind == Task::Kind::Prepare) { it = cand; break; }
            }
            task = std::move(*it);
            m_tasks.erase(it);
            if (task.kind == Task::Kind::Prepare) {
                m_queuedPrepares.fetch_sub(1, std::memory_order_relaxed);
                m_preparing.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (task.kind == Task::Kind::Prepare) {
            Prepared out;
            Prepare(task, out);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_completedPrepares.push_back(std::move(out));
                m_preparing.fetch_sub(1, std::memory_order_relaxed);
            }
        } else if (task.kind == Task::Kind::Store) {
            Store(task);
        }
        // Discard: nothing to do — `task` and its payload die here, off the main thread.
    }
}

bool MeshSdfBakeQueue::FillFromMesh(MeshSdfBakeRequest& request, Mesh& mesh) {
    if (!mesh.IsValid() || mesh.header.vertexFormat != MeshVertexFormat::Standard
        || mesh.GetVertexCount() == 0 || mesh.GetIndexCount() < 3) return false;
    const uint32_t indexCount = mesh.GetIndexCount();   // LOD0; the LODs follow in indexData
    const size_t elementBytes = mesh.header.indexFormat == MeshIndexFormat::UInt16 ? 2u : 4u;
    if (mesh.indexData.size() < static_cast<size_t>(indexCount) * elementBytes) return false;
    request.vertexCount = mesh.GetVertexCount();
    request.vertexStride = mesh.header.vertexStride;
    request.indices.resize(indexCount);
    if (elementBytes == 2u) {
        const auto* src = reinterpret_cast<const uint16_t*>(mesh.indexData.data());
        for (uint32_t i = 0; i < indexCount; ++i) request.indices[i] = src[i];
    } else {
        std::memcpy(request.indices.data(), mesh.indexData.data(), static_cast<size_t>(indexCount) * sizeof(uint32_t));
    }
    request.vertexData = std::move(mesh.vertexData);
    return true;
}

void MeshSdfBakeQueue::Prepare(Task& task, Prepared& out) {
    MeshSdfBakeRequest& request = task.request;
    out.meshIndex = request.meshIndex;
    out.twoSided = request.twoSided;
    out.verify = task.verify;
    if (request.vertexData.empty() && (!request.sourcePath.empty() || !request.sourceBytes.empty())) {
        // Re-trigger: parse the mesh here, off the main thread.
        Mesh mesh = request.sourceBytes.empty() ? uz::LoadMesh(request.sourcePath)
                                                : uz::LoadMeshFromMemory(request.sourceBytes, request.sourcePath);
        request.sourceBytes.clear();
        request.sourceBytes.shrink_to_fit();
        if (!FillFromMesh(request, mesh)) {
            UZ_LOG(Renderer, Warning, "MeshSdfBakeQueue: cannot re-read '{}' for its SDF", request.sourcePath);
            return;
        }
    }
    const bool useCache = task.useCache;
    out.key = useCache ? CacheKey(request) : 0;
    if (useCache && LoadFromCache(out.key, out)) {
        out.hit = true;
        out.ok = true;
        if (!task.verify) return;
    }
    const auto* positions = reinterpret_cast<const float*>(request.vertexData.data());
    MeshSdfCpuBake cpu = MeshSdfBaker::PrepareCPU(positions, request.vertexCount, request.vertexStride,
                                                  request.indices.empty() ? nullptr : request.indices.data(),
                                                  static_cast<uint32_t>(request.indices.size()), request.resolution,
                                                  request.vertexNormalOffset);
    if (out.hit) {
        out.verifyCpu = std::move(cpu);
        return;
    }
    out.ok = cpu.ok;
    out.cpu = std::move(cpu);
}

std::vector<uint8_t> MeshSdfBakeQueue::EncodeCacheEntry(MeshSdfCacheHeader h, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> entry;
    if (body.empty() || body.size() > static_cast<size_t>(LZ4_MAX_INPUT_SIZE)) return entry;
    if (h.volumeBytes + h.recordBytes != body.size()) return entry;   // the header must describe the body
    h.magic = kMeshSdfCacheMagic;
    h.version = kMeshSdfCacheVersion;
    h.bodyHash = AssetCache::HashBytes(body.data(), body.size());
    const int bound = LZ4_compressBound(static_cast<int>(body.size()));
    entry.resize(sizeof(h) + static_cast<size_t>(bound));
    const int compressed = LZ4_compress_default(reinterpret_cast<const char*>(body.data()),
                                                reinterpret_cast<char*>(entry.data() + sizeof(h)),
                                                static_cast<int>(body.size()), bound);
    if (compressed <= 0) {
        entry.clear();
        return entry;
    }
    h.compressedBytes = static_cast<uint64_t>(compressed);
    std::memcpy(entry.data(), &h, sizeof(h));
    entry.resize(sizeof(h) + static_cast<size_t>(compressed));
    return entry;
}

bool MeshSdfBakeQueue::DecodeCacheEntry(const std::vector<uint8_t>& entry, MeshSdfCacheHeader& h, std::vector<uint8_t>& body) {
    if (entry.size() < sizeof(MeshSdfCacheHeader)) return false;
    std::memcpy(&h, entry.data(), sizeof(h));
    const uint64_t volumeBytes = static_cast<uint64_t>(h.resolution) * h.resolution * h.resolution * 2u;
    const uint64_t recordBytes = (static_cast<uint64_t>(h.triangleCount) + h.nodeCount) * sizeof(MeshSdfGpuTriangle);
    if (h.magic != kMeshSdfCacheMagic || h.version != kMeshSdfCacheVersion || h.resolution == 0
        || h.volumeBytes != volumeBytes || h.recordBytes != recordBytes || h.triangleCount == 0
        || h.compressedBytes != entry.size() - sizeof(h)
        || volumeBytes + recordBytes > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    body.resize(static_cast<size_t>(volumeBytes + recordBytes));
    const int decoded = LZ4_decompress_safe(reinterpret_cast<const char*>(entry.data() + sizeof(h)),
                                            reinterpret_cast<char*>(body.data()),
                                            static_cast<int>(h.compressedBytes), static_cast<int>(body.size()));
    return decoded == static_cast<int>(body.size()) && AssetCache::HashBytes(body.data(), body.size()) == h.bodyHash;
}

bool MeshSdfBakeQueue::LoadFromCache(uint64_t key, Prepared& out) const {
    if (!m_cache.Contains(key)) return false;   // Lookup logs a warning on a missing file
    auto data = m_cache.Lookup(key);
    if (!data) return false;
    MeshSdfCacheHeader h{};
    std::vector<uint8_t> body;
    if (!DecodeCacheEntry(*data, h, body)) {
        UZ_LOG(Renderer, Warning, "MeshSdfBakeQueue: cache entry {:016x} is stale or corrupt — baking", key);
        return false;
    }
    const uint64_t volumeBytes = h.volumeBytes;
    out.volume.assign(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(volumeBytes));
    MeshSdfCpuBake& cpu = out.cpu;
    cpu.records.assign(body.begin() + static_cast<std::ptrdiff_t>(volumeBytes), body.end());
    std::memcpy(cpu.volumeCenter, h.volumeCenter, sizeof(cpu.volumeCenter));
    std::memcpy(cpu.volumeHalfExtent, h.volumeHalfExtent, sizeof(cpu.volumeHalfExtent));
    std::memcpy(cpu.volumeMin, h.volumeMin, sizeof(cpu.volumeMin));
    cpu.voxelSize = h.voxelSize;
    cpu.resolution = h.resolution;
    cpu.triangleCount = h.triangleCount;
    cpu.nodeCount = h.nodeCount;
    cpu.orientedTriangleCount = h.orientedTriangleCount;
    cpu.ok = true;
    return true;
}

void MeshSdfBakeQueue::Store(Task& task) {
    const std::vector<uint8_t> payload = EncodeCacheEntry(task.header, task.payload);
    if (payload.empty()) {
        UZ_LOG(Renderer, Warning, "MeshSdfBakeQueue: cannot encode cache entry {:016x} (header/body mismatch or LZ4)", task.key);
        return;
    }
    if (!m_cache.Store(task.key, payload)) {
        UZ_LOG(Renderer, Warning, "MeshSdfBakeQueue: could not write cache entry {:016x}", task.key);
    }
}

// ───────────────────────────────────────
// Service (main thread)
// ───────────────────────────────────────

MeshSdfBakeQueue::ServiceStats MeshSdfBakeQueue::Service(IRHICommandList* cmdList, RHIPipelineHandle bakePipeline,
                                                         std::vector<GPUMesh>& meshes, uint64_t fenceValue, bool unthrottled) {
    ServiceStats stats;
    if (!cmdList || !m_device) return stats;
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    const auto t0 = Clock::now();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& p : m_completedPrepares) m_ready.push_back(std::move(p));
        m_completedPrepares.clear();
    }

    // ── Complete first: frees the slots whose fence passed before new work is recorded ───────
    uint32_t completions = 0;
    for (size_t i = 0; i < m_gpu.size();) {
        GpuBake& bake = m_gpu[i];
        // The cap holds during the preload too: every completed field is an instance change for the
        // global distance field, and more than r.GI.GDF.MaxRegions in one frame means a whole-window
        // recompose (and the history reset that comes with it).
        const auto f0 = Clock::now();
        const bool done = bake.recorded && (!m_fence || m_fence->IsComplete(bake.fenceValue));
        const auto f1 = Clock::now();
        stats.fenceMs += ms(f0, f1);
        if (done && completions < (unthrottled ? kCompletionsPerPreloadFrame : kCompletionsPerFrame)) {
            Complete(bake, meshes);
            const auto f2 = Clock::now();
            m_gpu.erase(m_gpu.begin() + static_cast<std::ptrdiff_t>(i));
            stats.completeBodyMs += ms(f1, f2);
            stats.eraseMs += ms(f2, Clock::now());
            ++completions;
        } else {
            ++i;
        }
    }

    const auto t1 = Clock::now();
    stats.completed = completions;
    stats.completeMs = ms(t0, t1);

    // ── Start prepared bakes within the budget ────────────────────────────────────────────────
    const uint32_t startBudget = unthrottled ? UINT32_MAX : static_cast<uint32_t>(std::max(1.0f, cv_MeshSdfBakeStartsPerFrame->value));
    uint32_t started = 0;
    while (!m_ready.empty() && started < startBudget) {
        Prepared prepared = std::move(m_ready.front());
        m_ready.pop_front();
        if (!prepared.ok) {
            ++m_stats.failed;
            m_pendingMeshes.erase(prepared.meshIndex);
            UZ_LOG(Renderer, Warning, "SceneRenderer: SDF bake failed for meshIdx={} (CPU half)", prepared.meshIndex);
            continue;
        }
        if (!bakePipeline.IsValid() && (!prepared.hit || prepared.verify)) {
            ++m_stats.failed;
            m_pendingMeshes.erase(prepared.meshIndex);
            UZ_LOG(Renderer, Warning, "SceneRenderer: SDF bake pipeline not available — skipping meshIdx={}", prepared.meshIndex);
            continue;
        }
        StartPrepared(cmdList, prepared, fenceValue);
        ++started;
    }
    if (started > 0) {
        UZ_LOG(Renderer, Info, "SceneRenderer: batch SDF bake — {} meshes", started);
    }
    const auto t2 = Clock::now();
    stats.started = started;
    stats.startMs = ms(t1, t2);

    // ── Record the next Z slabs of the bakes in flight ────────────────────────────────────────
    uint32_t sliceBudget = unthrottled ? UINT32_MAX
                         : std::max(4u, static_cast<uint32_t>(cv_MeshSdfBakeSlicesPerFrame->value) & ~3u);
    for (auto& bake : m_gpu) {
        if (bake.recorded) continue;
        if (sliceBudget == 0) break;
        RecordSlabs(cmdList, bakePipeline, bake, sliceBudget, fenceValue);
        ++stats.slabs;
    }
    stats.slabMs = ms(t2, Clock::now());
    return stats;
}

void MeshSdfBakeQueue::StartPrepared(IRHICommandList* cmdList, Prepared& prepared, uint64_t fenceValue) {
    GpuBake bake;
    bake.meshIndex = prepared.meshIndex;
    bake.key = prepared.key;
    bake.hit = prepared.hit;
    bake.cpu = std::move(prepared.cpu);
    bake.sdf = MeshSdfBaker::CreateResources(m_device, bake.cpu, prepared.twoSided);
    if (!bake.sdf.texture3D.IsValid()) {
        ++m_stats.failed;
        m_pendingMeshes.erase(bake.meshIndex);
        UZ_LOG(Renderer, Warning, "SceneRenderer: SDF resources failed for meshIdx={}", bake.meshIndex);
        return;
    }
    const uint32_t res = bake.sdf.resolution;
    if (bake.hit) {
        // Upload the cached volume: rows at the 256-byte pitch D3D12 expects, whole volume in one copy.
        const uint32_t rowPitch = MeshSdfBaker::VolumeRowPitch(res);
        const uint64_t bytes = static_cast<uint64_t>(rowPitch) * res * res;
        bake.staging = AcquireBuffer(m_freeStaging, bytes, RHIBufferUsage::CopySrc, RHIMemoryType::Upload, "MeshSDF_CacheStaging");
        bake.stagingBytes = bytes;
        auto* mapped = bake.staging.IsValid() ? static_cast<uint8_t*>(m_device->MapBuffer(bake.staging)) : nullptr;
        if (!mapped) {
            ++m_stats.failed;
            m_pendingMeshes.erase(bake.meshIndex);
            UZ_LOG(Renderer, Warning, "SceneRenderer: SDF cache staging failed for meshIdx={}", bake.meshIndex);
            DestroyScratch(bake);
            return;
        }
        const size_t rowBytes = static_cast<size_t>(res) * 2u;
        if (rowPitch == rowBytes) {
            std::memcpy(mapped, prepared.volume.data(), prepared.volume.size());   // 128^3: rows are already 256 bytes
        } else {
            for (uint32_t z = 0; z < res; ++z) {
                for (uint32_t y = 0; y < res; ++y) {
                    std::memcpy(mapped + (static_cast<uint64_t>(z) * res + y) * rowPitch,
                                prepared.volume.data() + (static_cast<size_t>(z) * res + y) * rowBytes, rowBytes);
                }
            }
        }
        m_device->UnmapBuffer(bake.staging);
        // The volume is freed on a worker unless the verify re-bake below still needs it.
        if (!(prepared.verify && prepared.verifyCpu.ok)) DiscardLater(std::move(prepared.volume));

        RHIBarrierDesc toCopy{};
        toCopy.texture     = bake.sdf.texture3D;
        toCopy.stateBefore = RHIResourceState::Undefined;
        toCopy.stateAfter  = RHIResourceState::CopyDst;
        cmdList->ResourceBarrier(&toCopy, 1);
        cmdList->CopyBufferToTexture(bake.staging, 0, rowPitch, bake.sdf.texture3D, 0, 0);
        RHIBarrierDesc toSrv{};
        toSrv.texture     = bake.sdf.texture3D;
        toSrv.stateBefore = RHIResourceState::CopyDst;
        toSrv.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&toSrv, 1);
        bake.recorded = true;
        bake.fenceValue = fenceValue;
    } else {
        RHIBarrierDesc toUav{};
        toUav.texture     = bake.sdf.texture3D;
        toUav.stateBefore = RHIResourceState::Undefined;
        toUav.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&toUav, 1);
    }
    m_gpu.push_back(std::move(bake));

    // Diagnostic: bake the hit again into scratch resources and compare at completion.
    if (prepared.hit && prepared.verify && prepared.verifyCpu.ok) {
        GpuBake check;
        check.meshIndex = prepared.meshIndex;
        check.key = prepared.key;
        check.verify = true;
        check.cpu = std::move(prepared.verifyCpu);
        check.expectedVolume = std::move(prepared.volume);
        check.expectedRecords = m_gpu.back().cpu.records;
        check.sdf = MeshSdfBaker::CreateResources(m_device, check.cpu, prepared.twoSided);
        if (!check.sdf.texture3D.IsValid()) return;
        RHIBarrierDesc toUav{};
        toUav.texture     = check.sdf.texture3D;
        toUav.stateBefore = RHIResourceState::Undefined;
        toUav.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&toUav, 1);
        m_gpu.push_back(std::move(check));
    }
}

void MeshSdfBakeQueue::RecordSlabs(IRHICommandList* cmdList, RHIPipelineHandle bakePipeline, GpuBake& bake,
                                   uint32_t& sliceBudget, uint64_t fenceValue) {
    const uint32_t res = bake.sdf.resolution;
    const uint32_t slices = std::min(res - bake.nextSlice, sliceBudget);
    if (slices == 0) return;
    MeshSdfBaker::RecordBakeSlices(cmdList, bakePipeline, m_device, bake.sdf, bake.cpu, bake.nextSlice, slices);
    bake.nextSlice += slices;
    if (sliceBudget != UINT32_MAX) sliceBudget -= slices;
    if (bake.nextSlice < res) return;

    // Last slab: read the volume back (the cache entry and, for a verify, the comparison), then
    // leave the texture as a shader resource.
    const uint32_t rowPitch = MeshSdfBaker::VolumeRowPitch(res);
    const uint64_t slicePitch = static_cast<uint64_t>(rowPitch) * res;
    bake.readback = AcquireBuffer(m_freeReadback, slicePitch * res, RHIBufferUsage::CopyDst, RHIMemoryType::Readback, "MeshSDF_Readback");
    bake.readbackBytes = slicePitch * res;

    RHIBarrierDesc barrier{};
    barrier.texture     = bake.sdf.texture3D;
    barrier.stateBefore = RHIResourceState::UnorderedAccess;
    barrier.stateAfter  = bake.readback.IsValid() ? RHIResourceState::CopySrc : RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);
    if (bake.readback.IsValid()) {
        for (uint32_t z = 0; z < res; ++z) {
            cmdList->CopyTextureToBuffer(bake.sdf.texture3D, 0, bake.readback, z * slicePitch, rowPitch, z);
        }
        barrier.stateBefore = RHIResourceState::CopySrc;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    } else {
        UZ_LOG(Renderer, Warning, "SceneRenderer: SDF readback allocation failed for meshIdx={} — not cached", bake.meshIndex);
    }
    bake.recorded = true;
    bake.fenceValue = fenceValue;
}

// ───────────────────────────────────────
// Completion (main thread, fence passed)
// ───────────────────────────────────────

void MeshSdfBakeQueue::Complete(GpuBake& bake, std::vector<GPUMesh>& meshes) {
    const uint32_t res = bake.sdf.resolution;
    const uint32_t rowPitch = MeshSdfBaker::VolumeRowPitch(res);
    const size_t rowBytes = static_cast<size_t>(res) * 2u;
    std::vector<uint8_t> volume;
    if (bake.readback.IsValid()) {
        const auto* mapped = static_cast<const uint8_t*>(m_device->MapBuffer(bake.readback));
        if (mapped) {
            volume.resize(rowBytes * res * res);
            if (rowPitch == rowBytes) {
                std::memcpy(volume.data(), mapped, volume.size());
            } else {
                for (uint32_t z = 0; z < res; ++z) {
                    for (uint32_t y = 0; y < res; ++y) {
                        std::memcpy(volume.data() + (static_cast<size_t>(z) * res + y) * rowBytes,
                                    mapped + (static_cast<uint64_t>(z) * res + y) * rowPitch, rowBytes);
                    }
                }
            }
            m_device->UnmapBuffer(bake.readback);
        }
        ReleaseBuffer(m_freeReadback, bake.readback, bake.readbackBytes);
    }
    if (bake.staging.IsValid()) {
        ReleaseBuffer(m_freeStaging, bake.staging, bake.stagingBytes);
    }

    if (bake.verify) {
        size_t identical = 0;
        float maxDelta = 0.0f;
        const size_t voxels = volume.size() / 2;
        const bool sameSize = volume.size() == bake.expectedVolume.size() && !volume.empty();
        if (sameSize) {
            const auto* a = reinterpret_cast<const uint16_t*>(volume.data());
            const auto* b = reinterpret_cast<const uint16_t*>(bake.expectedVolume.data());
            auto half = [](uint16_t h) {
                const uint32_t sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1fu, mant = h & 0x3ffu;
                float v = exp == 0 ? std::ldexp(static_cast<float>(mant), -24)
                        : exp == 31 ? std::numeric_limits<float>::infinity()
                        : std::ldexp(1.0f + static_cast<float>(mant) / 1024.0f, static_cast<int>(exp) - 15);
                return sign ? -v : v;
            };
            for (size_t i = 0; i < voxels; ++i) {
                if (a[i] == b[i]) { ++identical; continue; }
                maxDelta = std::max(maxDelta, std::abs(half(a[i]) - half(b[i])));
            }
        }
        const bool recordsSame = bake.cpu.records == bake.expectedRecords;
        const bool pass = sameSize && identical == voxels && recordsSame;
        ++m_stats.verified;
        if (!pass) ++m_stats.verifyMismatch;
        UZ_LOG(Renderer, Info,
               "MeshSDF cache verify: meshIdx={} key={:016x} volumeIdentical={}/{} maxAbsDelta={} recordsIdentical={} result={}",
               bake.meshIndex, bake.key, identical, voxels, maxDelta, recordsSame, pass ? "IDENTICAL" : "DIFFERENT");
        DiscardLater(std::move(bake.expectedVolume));
        DiscardLater(std::move(bake.expectedRecords));
        DiscardLater(std::move(bake.cpu.records));
        DiscardLater(std::move(volume));
        DestroyScratch(bake);
        return;
    }

    m_pendingMeshes.erase(bake.meshIndex);
    MeshSDF sdf = bake.sdf;
    sdf.triangleBuffer = sdf.pendingTriBuffer;
    sdf.pendingTriBuffer = RHIBufferHandle{};
    sdf.valid = true;
    if (bake.meshIndex < meshes.size() && meshes[bake.meshIndex].sdf.valid) {
        // Submitted twice (SDF GI toggled while a bake was in flight): keep the first field.
        DestroyScratch(bake);
        return;
    }
    if (bake.meshIndex < meshes.size()) {
        meshes[bake.meshIndex].sdf = sdf;
    } else {
        ++m_stats.failed;
        UZ_LOG(Renderer, Warning, "SceneRenderer: SDF for meshIdx={} has no mesh — dropped", bake.meshIndex);
        DestroyScratch(bake);
        return;
    }
    if (bake.hit) {
        ++m_stats.cached;
        UZ_LOG(Renderer, Info, "SceneRenderer: mesh SDF from cache (meshIdx={}, res={}, SRV={}, {} triangles, {} BVH nodes, key={:016x})",
               bake.meshIndex, res, sdf.srvIndex, bake.cpu.triangleCount, bake.cpu.nodeCount, bake.key);
        DiscardLater(std::move(bake.cpu.records));   // the records live on in the mesh's triangle buffer
        return;
    }
    ++m_stats.baked;
    UZ_LOG(Renderer, Info,
           "MeshSdfBaker: baked {}^3 SDF ({} triangles, {} BVH nodes, {} normals reoriented, twoSided={}) on GPU, SRV={}",
           res, bake.cpu.triangleCount, bake.cpu.nodeCount, bake.cpu.orientedTriangleCount, sdf.twoSided, sdf.srvIndex);
    UZ_LOG(Renderer, Info, "SceneRenderer: GPU-baked mesh SDF (meshIdx={}, res={}, SRV={})", bake.meshIndex, res, sdf.srvIndex);

    if (bake.key == 0 || volume.empty() || m_stop.load()) {
        DiscardLater(std::move(bake.cpu.records));
        return;
    }
    // The cache entry: header + LZ4 block of (volume ‖ records), hashed before compression.
    std::vector<uint8_t> body;
    body.reserve(volume.size() + bake.cpu.records.size());
    body.insert(body.end(), volume.begin(), volume.end());
    body.insert(body.end(), bake.cpu.records.begin(), bake.cpu.records.end());
    if (body.size() > static_cast<size_t>(LZ4_MAX_INPUT_SIZE)) return;
    MeshSdfCacheHeader h{};
    h.magic = kMeshSdfCacheMagic;
    h.version = kMeshSdfCacheVersion;
    h.resolution = res;
    h.twoSided = sdf.twoSided ? 1u : 0u;
    std::memcpy(h.volumeCenter, bake.cpu.volumeCenter, sizeof(h.volumeCenter));
    std::memcpy(h.volumeHalfExtent, bake.cpu.volumeHalfExtent, sizeof(h.volumeHalfExtent));
    std::memcpy(h.volumeMin, bake.cpu.volumeMin, sizeof(h.volumeMin));
    h.voxelSize = bake.cpu.voxelSize;
    h.triangleCount = bake.cpu.triangleCount;
    h.nodeCount = bake.cpu.nodeCount;
    h.orientedTriangleCount = bake.cpu.orientedTriangleCount;
    h.volumeBytes = volume.size();
    h.recordBytes = bake.cpu.records.size();
    h.bodyHash = AssetCache::HashBytes(body.data(), body.size());
    DiscardLater(std::move(bake.cpu.records));   // after the header took its size (an earlier order wrote recordBytes 0: every entry read as corrupt)
    // The worker compresses and writes it (Store); the main thread only hands the bytes over.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Task task;
        task.kind = Task::Kind::Store;
        task.key = bake.key;
        task.header = h;
        task.payload = std::move(body);
        m_tasks.push_back(std::move(task));
    }
    m_cv.notify_one();
    ++m_stats.stored;
}

RHIBufferHandle MeshSdfBakeQueue::AcquireBuffer(std::unordered_map<uint64_t, std::vector<RHIBufferHandle>>& pool, uint64_t bytes,
                                                RHIBufferUsage usage, RHIMemoryType memory, const char* debugName) {
    auto it = pool.find(bytes);
    if (it != pool.end() && !it->second.empty()) {
        RHIBufferHandle handle = it->second.back();
        it->second.pop_back();
        return handle;
    }
    RHIBufferDesc desc{};
    desc.sizeBytes = bytes;
    desc.stride    = 2;
    desc.usage     = usage;
    desc.memory    = memory;
    desc.debugName = debugName;
    return m_device->CreateBuffer(desc);
}

void MeshSdfBakeQueue::ReleaseBuffer(std::unordered_map<uint64_t, std::vector<RHIBufferHandle>>& pool, RHIBufferHandle& buffer, uint64_t bytes) {
    constexpr size_t kPooledPerSize = 8;   // 8 x 4 MiB per pool at 128^3
    if (!buffer.IsValid()) return;
    auto& free = pool[bytes];
    if (free.size() < kPooledPerSize) {
        free.push_back(buffer);
    } else {
        m_device->DestroyBuffer(buffer);
    }
    buffer = RHIBufferHandle{};
}

void MeshSdfBakeQueue::DiscardLater(std::vector<uint8_t>&& bytes) {
    if (bytes.empty() || m_stop.load()) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Task task;
        task.kind = Task::Kind::Discard;
        task.payload = std::move(bytes);
        m_tasks.push_back(std::move(task));
    }
    m_cv.notify_one();
}

void MeshSdfBakeQueue::DestroyScratch(GpuBake& bake) {
    if (!m_device) return;
    if (bake.readback.IsValid()) m_device->DestroyBuffer(bake.readback);
    if (bake.staging.IsValid()) m_device->DestroyBuffer(bake.staging);
    if (bake.sdf.pendingTriBuffer.IsValid()) m_device->DestroyBuffer(bake.sdf.pendingTriBuffer);
    if (bake.sdf.triangleBuffer.IsValid()) m_device->DestroyBuffer(bake.sdf.triangleBuffer);
    if (bake.sdf.texture3D.IsValid()) m_device->DestroyTexture(bake.sdf.texture3D);
    bake.readback = bake.staging = RHIBufferHandle{};
    bake.sdf = MeshSDF{};
}

} // namespace uz
