/// UzEngine GI — asynchronous, disk-cached mesh SDF bakes (lighting block B, item 3).
///
/// A mesh that needs a distance field is submitted with its bytes. Worker threads hash the mesh,
/// look the field up in the on-disk cache (`cache/meshsdf`, an `AssetCache`) and, on a miss, run
/// the CPU half of the bake (`MeshSdfBaker::PrepareCPU`: volume placement, triangle records, BVH).
/// The main thread then creates the GPU resources once per frame within a budget, uploads a cached
/// volume or records the bake dispatch in Z slabs over several frames, reads a fresh bake back for
/// the cache, and marks the SDF valid only when the renderer's frame fence says the GPU finished —
/// never `WaitIdle`, never an RHI call off the main thread.
///
/// Cache entry = `MeshSdfCacheHeader` + LZ4 block of (R16F volume ‖ records), keyed on the mesh
/// bytes, the bake options and the bake shader's bytecode; a corrupt or stale entry is a miss and
/// is rewritten after the bake. A cache hit is the same bits the bake produced (the volume is the
/// GPU's own readback); `r.GI.MeshSdf.Diagnostic.CacheVerify` re-bakes every hit and logs the
/// comparison.

#pragma once

#include "mesh_sdf_baker.h"
#include "asset_cache.h"
#include "rhi_types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class IRHIFence;
struct GPUMesh;

/// A mesh handed to the queue. The bake options are captured here, at submit time. Either the
/// vertex/index data is given, or a source (`sourcePath` on the file system, or `sourceBytes` read
/// from a pak) that the worker parses — the re-trigger path, which keeps the parse off the main thread.
struct MeshSdfBakeRequest {
    uint32_t              meshIndex = UINT32_MAX;   ///< Index into the renderer's mesh array.
    std::string           sourcePath;               ///< Mesh file to parse on the worker (empty: data given).
    std::vector<uint8_t>  sourceBytes;              ///< Mesh bytes to parse on the worker (pak mode).
    std::vector<uint8_t>  vertexData;               ///< Vertex bytes (positions at offset 0).
    uint32_t              vertexCount = 0;
    uint32_t              vertexStride = 0;
    std::vector<uint32_t> indices;                  ///< 32-bit triangle indices (16-bit meshes converted).
    uint32_t              resolution = 32;
    bool                  twoSided = false;
    uint32_t              vertexNormalOffset = UINT32_MAX;
};

/// On-disk layout of a cache entry (little-endian, 104 bytes), followed by the LZ4 block.
struct MeshSdfCacheHeader {
    uint32_t magic;                  ///< kMeshSdfCacheMagic
    uint32_t version;                ///< kMeshSdfCacheVersion
    uint32_t resolution;
    uint32_t twoSided;
    float    volumeCenter[3];
    float    volumeHalfExtent[3];
    float    volumeMin[3];
    float    voxelSize;
    uint32_t triangleCount;
    uint32_t nodeCount;
    uint32_t orientedTriangleCount;
    uint32_t reserved;
    uint64_t volumeBytes;            ///< resolution^3 * 2, tightly packed R16F.
    uint64_t recordBytes;            ///< (triangleCount + nodeCount) * 64.
    uint64_t compressedBytes;        ///< Size of the LZ4 block that follows the header.
    uint64_t bodyHash;               ///< XXH64 of the uncompressed body (volume ‖ records).
};
static_assert(sizeof(MeshSdfCacheHeader) == 104, "cache header layout");

inline constexpr uint32_t kMeshSdfCacheMagic   = 0x43534D55u;   // 'UMSC'
inline constexpr uint32_t kMeshSdfCacheVersion = 1u;

struct Mesh;

class MeshSdfBakeQueue {
public:
    /// Fills the request's geometry from a CPU mesh (16-bit indices widened; the vertex bytes are
    /// moved out). False when the mesh is not a standard-format mesh with at least one triangle.
    static bool FillFromMesh(MeshSdfBakeRequest& request, Mesh& mesh);

    /// A cache entry: `header` (its sizes and counts set by the caller; `compressedBytes` and
    /// `bodyHash` are filled here) followed by the LZ4 block of `body` (volume ‖ records). Empty on
    /// an LZ4 failure or a body larger than LZ4 accepts.
    [[nodiscard]] static std::vector<uint8_t> EncodeCacheEntry(MeshSdfCacheHeader header, const std::vector<uint8_t>& body);

    /// The inverse: false when the magic, version, sizes, counts, block size or checksum disagree.
    [[nodiscard]] static bool DecodeCacheEntry(const std::vector<uint8_t>& entry, MeshSdfCacheHeader& header, std::vector<uint8_t>& body);

    struct Stats {
        uint32_t submitted = 0;
        uint32_t baked = 0;          ///< Fresh bakes completed.
        uint32_t cached = 0;         ///< Cache hits completed.
        uint32_t stored = 0;         ///< Entries written (counted when the store task is queued).
        uint32_t failed = 0;
        uint32_t verified = 0;       ///< Hits compared with a fresh bake (diagnostic).
        uint32_t verifyMismatch = 0;
    };

    /// `fence` is a direct-queue fence the renderer signals after every frame's Execute with a
    /// strictly increasing value (the value is passed to `Service`). `shaderHash` is evaluated
    /// once, on the main thread, at the first submit (the bake shader's bytecode hash).
    MeshSdfBakeQueue(IRHIDevice* device, IRHIFence* fence, std::string cacheDirectory,
                     std::function<uint64_t()> shaderHash, int workerThreads);
    ~MeshSdfBakeQueue();

    MeshSdfBakeQueue(const MeshSdfBakeQueue&) = delete;
    MeshSdfBakeQueue& operator=(const MeshSdfBakeQueue&) = delete;

    /// Main thread. The request's bytes are moved to a worker.
    void Submit(MeshSdfBakeRequest&& request);

    /// Main thread, once per frame with the frame's command list in recording state. Starts the
    /// bakes the workers prepared (GPU resources, cache-hit uploads), records the next Z slabs of
    /// the bakes in flight, and completes the ones whose fence passed: `meshes[i].sdf` becomes
    /// valid, a fresh bake's readback goes to a worker to be stored. `fenceValue` is the value the
    /// renderer will signal after this frame's Execute. `unthrottled` (scene preload, nothing is
    /// presented) lifts the per-frame budgets.
    struct ServiceStats {
        uint32_t completed = 0, started = 0, slabs = 0;
        double   completeMs = 0.0, startMs = 0.0, slabMs = 0.0;   ///< Main-thread time of each phase this frame.
        double   fenceMs = 0.0, completeBodyMs = 0.0, eraseMs = 0.0; ///< Inside the completion phase.
    };
    ServiceStats Service(IRHICommandList* cmdList, RHIPipelineHandle bakePipeline, std::vector<GPUMesh>& meshes,
                         uint64_t fenceValue, bool unthrottled);

    /// Meshes submitted and not yet valid (any thread).
    [[nodiscard]] size_t InFlight() const;

    /// Main thread: true while a bake of this mesh is queued or in flight (the re-trigger skips it).
    [[nodiscard]] bool IsPending(uint32_t meshIndex) const { return m_pendingMeshes.count(meshIndex) != 0; }

    /// Joins the workers and destroys the GPU scratch of unfinished bakes. The device must be
    /// idle (the renderer waits before calling). Safe to call twice.
    void Shutdown();

    [[nodiscard]] const Stats& GetStats() const { return m_stats; }

private:
    struct Task {
        enum class Kind { Prepare, Store, Discard } kind = Kind::Prepare;   ///< Discard: free big buffers off the main thread.
        MeshSdfBakeRequest   request;      // Prepare
        bool                 verify = false;
        bool                 useCache = true;
        float                debugDelayMs = 0.f;   ///< r.GI.MeshSdf.Diagnostic.PrepareDelayMs, read on the main thread
        uint64_t             key = 0;      // Store
        MeshSdfCacheHeader   header{};     // Store: filled but for compressedBytes
        std::vector<uint8_t> payload;      // Store: the raw body (volume then records); the worker compresses it
    };

    struct Prepared {
        uint32_t             meshIndex = UINT32_MAX;
        uint64_t             key = 0;
        bool                 ok = false;
        bool                 hit = false;
        bool                 twoSided = false;
        MeshSdfCpuBake       cpu;          // from the cache on a hit, from PrepareCPU on a miss
        std::vector<uint8_t> volume;       // hit: the cached R16F volume, tightly packed
        bool                 verify = false;
        MeshSdfCpuBake       verifyCpu;    // hit + verify: a fresh PrepareCPU to compare the records
    };

    struct GpuBake {
        uint32_t             meshIndex = UINT32_MAX;
        uint64_t             key = 0;
        bool                 hit = false;
        bool                 verify = false;        ///< Scratch re-bake of a hit; never reaches the mesh.
        MeshSDF              sdf;
        MeshSdfCpuBake       cpu;
        uint32_t             nextSlice = 0;
        bool                 recorded = false;      ///< All GPU commands recorded; waits for fenceValue.
        uint64_t             fenceValue = 0;
        RHIBufferHandle      staging;               ///< Hit: upload buffer with the cached volume.
        RHIBufferHandle      readback;              ///< Miss / verify: the baked volume comes back here.
        uint64_t             stagingBytes = 0;      ///< Pool keys of the two buffers above.
        uint64_t             readbackBytes = 0;
        std::vector<uint8_t> expectedVolume;        ///< Verify: the cached volume to compare with.
        std::vector<uint8_t> expectedRecords;       ///< Verify: the cached records to compare with.
    };

    void WorkerLoop();
    void Prepare(Task& task, Prepared& out);
    bool LoadFromCache(uint64_t key, Prepared& out) const;
    void Store(Task& task);
    [[nodiscard]] uint64_t CacheKey(const MeshSdfBakeRequest& request) const;
    void StartPrepared(IRHICommandList* cmdList, Prepared& prepared, uint64_t fenceValue);
    void RecordSlabs(IRHICommandList* cmdList, RHIPipelineHandle bakePipeline, GpuBake& bake,
                     uint32_t& sliceBudget, uint64_t fenceValue);
    void Complete(GpuBake& bake, std::vector<GPUMesh>& meshes);
    void DestroyScratch(GpuBake& bake);
    /// Hands a large buffer to a worker to be freed there: releasing 8 MB of records on the main
    /// thread cost ~38 ms per field (the frame's `erase` of the bake entry).
    void DiscardLater(std::vector<uint8_t>&& bytes);

    IRHIDevice*                 m_device = nullptr;
    IRHIFence*                  m_fence = nullptr;
    AssetCache                  m_cache;
    bool                        m_cacheReady = false;
    std::function<uint64_t()>   m_shaderHashFn;
    uint64_t                    m_shaderHash = 0;
    bool                        m_shaderHashKnown = false;

    // Workers (the AsyncTextureDecoder pattern: deque + mutex + condition variable, poll on the
    // main thread).
    std::vector<std::thread>    m_workers;
    std::deque<Task>            m_tasks;
    std::vector<Prepared>       m_completedPrepares;
    mutable std::mutex          m_mutex;
    std::condition_variable     m_cv;
    /// Notified by a worker after each preparation it finishes; the pre-load waits on it (Service).
    std::condition_variable     m_preparedCv;
    std::atomic<bool>           m_stop{false};
    std::atomic<size_t>         m_preparing{0};     ///< Prepare tasks running on workers.
    std::atomic<size_t>         m_queuedPrepares{0};

    // Main-thread state.
    std::deque<Prepared>        m_ready;            ///< Prepared, waiting for a GPU start slot.
    std::vector<GpuBake>        m_gpu;              ///< Resources created; dispatching or awaiting the fence.
    std::unordered_set<uint32_t> m_pendingMeshes;  ///< Mesh indices submitted and not yet completed (main thread).
    /// Staging (upload) and readback buffers are reused rather than destroyed per field: releasing a
    /// 4 MiB buffer cost the main thread tens of milliseconds per completion. Keyed by size, a few per size.
    std::unordered_map<uint64_t, std::vector<RHIBufferHandle>> m_freeStaging;
    std::unordered_map<uint64_t, std::vector<RHIBufferHandle>> m_freeReadback;
    RHIBufferHandle AcquireBuffer(std::unordered_map<uint64_t, std::vector<RHIBufferHandle>>& pool, uint64_t bytes,
                                  RHIBufferUsage usage, RHIMemoryType memory, const char* debugName);
    void ReleaseBuffer(std::unordered_map<uint64_t, std::vector<RHIBufferHandle>>& pool, RHIBufferHandle& buffer, uint64_t bytes);
    Stats                       m_stats;
};

} // namespace uz
