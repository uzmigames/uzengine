#pragma once

// texture_loader.h -- Async texture decode on worker threads.
//
// Moves stbi_load() off the main/render thread entirely.
// Main thread submits paths, worker threads decode, main thread polls results.
//
// Pattern: Godot 4 ResourceLoader (WorkerThreadPool + Request-Poll-Retrieve)
// Reference: phase2-fast-texture-loading task

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uz {

// Result of an async texture decode (CPU-side only, no GPU work).
struct DecodeResult {
    std::string path;
    uint8_t*    pixels     = nullptr;  // RGBA8 stbi-allocated (caller frees with stbi_image_free)
    uint32_t    width      = 0;
    uint32_t    height     = 0;
    size_t      pixel_size = 0;        // width * height * 4
};

// ---------------------------------------------------------------------------
// AsyncTextureLoader -- worker thread pool for CPU texture decoding.
//
// Usage:
//   loader.submit("assets/textures/brick.png");
//   // ... next frame(s) ...
//   auto results = loader.poll_completed();
//   for (auto& r : results) { /* create GPU texture, upload */ }
// ---------------------------------------------------------------------------
class AsyncTextureLoader {
public:
    explicit AsyncTextureLoader(int num_threads = 2);
    ~AsyncTextureLoader();

    // Submit a texture path for async decode. Thread-safe.
    void submit(const std::string& path);

    // Poll completed decode results. Returns all completed since last poll.
    // Moves ownership of pixel data to caller. Thread-safe.
    std::vector<DecodeResult> poll_completed();

    // Number of pending (queued + in-flight) decode tasks.
    size_t pending_count() const;

    // Shutdown workers (called automatically by destructor).
    void shutdown();

private:
    void worker_loop();

    std::vector<std::thread>    workers_;
    std::vector<std::string>    queue_;          // paths waiting to be decoded
    std::vector<DecodeResult>   completed_;      // decoded results ready for pickup
    mutable std::mutex          mutex_;
    std::condition_variable     cv_;
    std::atomic<bool>           stop_{false};
    std::atomic<size_t>         in_flight_{0};   // currently decoding on workers
};

} // namespace uz
