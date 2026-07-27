/// UzEngine Async Mesh Loader — Background .uzmesh file read + parse.
///
/// Moves mesh file I/O and binary parsing off the render thread entirely.
/// The render thread submits paths, worker threads read + parse `.uzmesh`
/// files into CPU `Mesh` structs in parallel, and the render thread polls
/// completed results for batched GPU upload.
///
/// # Pattern
/// Mirrors `AsyncTextureDecoder` (Submit → worker → PollCompleted):
///   1. Render thread: `Submit(path)` / `SubmitVFS(vfs, path)`.
///   2. Worker thread: read file bytes + `LoadMeshFromMemory()`.
///   3. Render thread: `PollCompleted()` → `UploadMeshDeferred()` + batch flush.
///
/// # Thread Safety
/// `Submit()`, `SubmitVFS()`, `PollCompleted()`, and `PendingCount()` are
/// thread-safe. Workers only touch the thread-safe `IVFS` interface (or the
/// filesystem directly) and the pure `LoadMeshFromMemory` parser — no RHI.

#pragma once

#include "mesh.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uz {

class IVFS;  // Forward declaration — full definition in engine/asset/vfs.h

// ─────────────────────────────────────────────────────────────────────────────
// MeshLoadResultCPU — Output of a background mesh read + parse
// ─────────────────────────────────────────────────────────────────────────────

/// Result of an async mesh load (CPU-side only, no GPU work).
///
/// # Fields
/// - `path` — Logical path submitted for load (slot-map key on the caller side).
/// - `mesh` — Parsed CPU mesh. `mesh.IsValid() == false` signals a read or
///   parse failure for this path.
struct MeshLoadResultCPU {
    std::string path;
    Mesh        mesh;
};

// ─────────────────────────────────────────────────────────────────────────────
// AsyncMeshLoader
// ─────────────────────────────────────────────────────────────────────────────

/// Worker thread pool for CPU-side mesh loading (file read + parse).
///
/// # Lifetime
/// Workers start in the constructor and join in the destructor (or explicit
/// `Shutdown()`). Unclaimed completed results are dropped on shutdown — CPU
/// `Mesh` structs own their memory via std::vector, nothing leaks.
class AsyncMeshLoader {
public:
    /// Starts `numThreads` worker threads (clamped to [1, 8]).
    ///
    /// # Arguments
    /// * `numThreads` — Worker count. Half the hardware threads is a good
    ///   default: mesh loading is I/O + memcpy bound and shares the CPU with
    ///   the texture decode pool during scene loads.
    explicit AsyncMeshLoader(int numThreads = 2);

    /// Shuts down workers and drops any unclaimed results.
    ~AsyncMeshLoader();

    /// Non-copyable, non-movable — owns threads referencing `this`.
    AsyncMeshLoader(const AsyncMeshLoader&)            = delete;
    AsyncMeshLoader& operator=(const AsyncMeshLoader&) = delete;
    AsyncMeshLoader(AsyncMeshLoader&&)                 = delete;
    AsyncMeshLoader& operator=(AsyncMeshLoader&&)      = delete;

    /// Enqueues a resolved filesystem path for background read + parse.
    /// Thread-safe.
    ///
    /// Prefer this over `SubmitVFS` when the caller already resolved the file
    /// on disk — workers then read via plain file I/O and do not serialize on
    /// the VFS mutex.
    ///
    /// # Arguments
    /// * `fsPath` — Absolute or working-dir-relative path to the .uzmesh file.
    /// * `resultKey` — If non-empty, used as `result.path` instead of `fsPath`
    ///   (needed when the caller keys pending slots by logical VFS path).
    void Submit(const std::string& fsPath, const std::string& resultKey = {});

    /// Enqueues a VFS logical path for background read + parse. Thread-safe.
    ///
    /// The worker calls `vfs->Read(logicalPath)` on the background thread.
    ///
    /// # Arguments
    /// * `vfs` — Thread-safe VFS backend. Must outlive all pending requests.
    /// * `logicalPath` — VFS logical asset path (e.g. "meshes/foo.uzmesh").
    /// * `resultKey` — If non-empty, used as `result.path` instead of
    ///   `logicalPath`.
    void SubmitVFS(IVFS* vfs, const std::string& logicalPath,
                   const std::string& resultKey = {});

    /// Retrieves all results completed since the last poll. Thread-safe.
    ///
    /// # Returns
    /// Vector of completed results (may be empty). Results with
    /// `mesh.IsValid() == false` indicate a load failure for that path.
    [[nodiscard]] std::vector<MeshLoadResultCPU> PollCompleted();

    /// Number of pending requests (queued + currently loading). Thread-safe.
    [[nodiscard]] size_t PendingCount() const;

    /// Stops all workers and drops unclaimed results. Safe to call multiple
    /// times or without prior submissions.
    void Shutdown();

private:
    /// Worker thread entry point. Loops until `m_stop` is set.
    void WorkerLoop();

    /// Internal queue entry — a filesystem path or a VFS logical path.
    struct LoadRequest {
        std::string path;        ///< Filesystem path or VFS logical path.
        std::string resultKey;   ///< If non-empty, used as result.path.
        IVFS*       vfs = nullptr; ///< If non-null, worker reads via vfs->Read(path). Non-owning.
    };

    std::vector<std::thread>       m_workers;
    std::deque<LoadRequest>        m_queue;      ///< Requests waiting for a worker.
    std::vector<MeshLoadResultCPU> m_completed;  ///< Parsed meshes ready for pickup.
    mutable std::mutex             m_mutex;
    std::condition_variable        m_cv;
    std::atomic<bool>              m_stop{false};
    std::atomic<size_t>            m_inFlight{0}; ///< Currently loading on workers.
};

} // namespace uz
