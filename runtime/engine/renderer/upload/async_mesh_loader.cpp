/// UzEngine Async Mesh Loader — Background .uzmesh file read + parse.
///
/// Worker threads read mesh files (plain file I/O or thread-safe VFS) and
/// parse them with LoadMeshFromMemory. See async_mesh_loader.h for the
/// Submit → worker → PollCompleted contract.

#include "async_mesh_loader.h"

#include "vfs.h"
#include "log.h"
#include "profiler.h"

#include <cstdio>
#include <utility>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

AsyncMeshLoader::AsyncMeshLoader(int numThreads) {
    const int n = numThreads < 1 ? 1 : (numThreads > 8 ? 8 : numThreads);
    m_workers.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        m_workers.emplace_back(&AsyncMeshLoader::WorkerLoop, this);
    }
    UZ_LOG(Renderer, Info, "AsyncMeshLoader: started {} worker threads", n);
}

AsyncMeshLoader::~AsyncMeshLoader() {
    Shutdown();
}

void AsyncMeshLoader::Shutdown() {
    m_stop.store(true, std::memory_order_relaxed);
    m_cv.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_completed.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Submit / Poll
// ─────────────────────────────────────────────────────────────────────────────

void AsyncMeshLoader::Submit(const std::string& fsPath, const std::string& resultKey) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(LoadRequest{fsPath, resultKey, nullptr});
    }
    m_cv.notify_one();
}

void AsyncMeshLoader::SubmitVFS(IVFS* vfs, const std::string& logicalPath,
                                const std::string& resultKey) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(LoadRequest{logicalPath, resultKey, vfs});
    }
    m_cv.notify_one();
}

std::vector<MeshLoadResultCPU> AsyncMeshLoader::PollCompleted() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_completed, std::vector<MeshLoadResultCPU>{});
}

size_t AsyncMeshLoader::PendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size() + m_inFlight.load(std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
// Worker
// ─────────────────────────────────────────────────────────────────────────────

void AsyncMeshLoader::WorkerLoop() {
    while (true) {
        LoadRequest request;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_stop.load(std::memory_order_relaxed) || !m_queue.empty();
            });

            if (m_stop.load(std::memory_order_relaxed) && m_queue.empty()) {
                return;
            }

            request = std::move(m_queue.front());
            m_queue.pop_front();
            m_inFlight.fetch_add(1, std::memory_order_relaxed);
        }

        UZ_PROFILE_SCOPE("AsyncMeshLoader::Load");

        MeshLoadResultCPU result;
        result.path = request.resultKey.empty() ? request.path : request.resultKey;

        // ── Read file bytes (VFS or plain file I/O) ──────────────────────
        std::vector<uint8_t> fileData;
        if (request.vfs) {
            fileData = request.vfs->Read(request.path);
            if (fileData.empty()) {
                UZ_LOG(Renderer, Error,
                       "AsyncMeshLoader: VFS read failed for '{}'", request.path);
            }
        } else {
            FILE* f = fopen(request.path.c_str(), "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                const long fileSize = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (fileSize > 0) {
                    fileData.resize(static_cast<size_t>(fileSize));
                    const size_t bytesRead =
                        fread(fileData.data(), 1, fileData.size(), f);
                    if (bytesRead != fileData.size()) {
                        UZ_LOG(Renderer, Error,
                               "AsyncMeshLoader: short read for '{}' ({}/{} bytes)",
                               request.path, bytesRead, fileData.size());
                        fileData.clear();
                    }
                }
                fclose(f);
            } else {
                UZ_LOG(Renderer, Error,
                       "AsyncMeshLoader: cannot open '{}'", request.path);
            }
        }

        // ── Parse (pure CPU, no shared state) ────────────────────────────
        if (!fileData.empty()) {
            result.mesh = LoadMeshFromMemory(fileData, result.path);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_completed.push_back(std::move(result));
        }
        m_inFlight.fetch_sub(1, std::memory_order_relaxed);
    }
}

} // namespace uz
