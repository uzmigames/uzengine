// ---------------------------------------------------------------------------
// texture_loader.cpp -- Async texture decode on worker threads
//
// RULE CS-7:  No exceptions
// RULE RHI-1: No backend types (CPU-only, no GPU work here)
// ---------------------------------------------------------------------------

#include "texture_loader.h"
#include "texture.h"
#include "engine/core/diag.h"

namespace uz {

AsyncTextureLoader::AsyncTextureLoader(int num_threads) {
    int n = (num_threads < 1) ? 1 : (num_threads > 8 ? 8 : num_threads);
    workers_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
        workers_.emplace_back(&AsyncTextureLoader::worker_loop, this);
    }
    DIAG_FMT("AsyncTextureLoader: started %d worker threads", n);
}

AsyncTextureLoader::~AsyncTextureLoader() {
    shutdown();
}

void AsyncTextureLoader::submit(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(path);
    }
    cv_.notify_one();
}

std::vector<DecodeResult> AsyncTextureLoader::poll_completed() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DecodeResult> results;
    results.swap(completed_);
    return results;
}

size_t AsyncTextureLoader::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() + in_flight_.load(std::memory_order_relaxed);
}

void AsyncTextureLoader::shutdown() {
    if (stop_.exchange(true)) return;  // already stopped

    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();

    // Free any unclaimed completed results.
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& r : completed_) {
        if (r.pixels) {
            free_decoded_texture_pixels(r.pixels);
        }
    }
    completed_.clear();
    queue_.clear();
}

void AsyncTextureLoader::worker_loop() {
    while (true) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_relaxed) || !queue_.empty();
            });

            if (stop_.load(std::memory_order_relaxed) && queue_.empty()) {
                return;
            }

            path = std::move(queue_.front());
            queue_.erase(queue_.begin());
            in_flight_.fetch_add(1, std::memory_order_relaxed);
        }

        // Decode on this worker thread (the whole point -- off the main thread).
        DecodedTexture decoded = decode_texture_from_file(path.c_str());

        DecodeResult result;
        result.path       = std::move(path);
        result.pixels     = decoded.pixels;
        result.width      = decoded.width;
        result.height     = decoded.height;
        result.pixel_size = decoded.pixel_size;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.push_back(std::move(result));
        }
        in_flight_.fetch_sub(1, std::memory_order_relaxed);
    }
}

} // namespace uz
