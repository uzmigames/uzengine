/// UzEngine Async Texture Decoder — Background CPU image decode via worker threads.
///
/// Moves stbi_load() off the render thread entirely. The render thread submits
/// file paths, worker threads decode images in parallel, and the render thread
/// polls completed results for batched GPU upload.
///
/// # Pattern
/// Matches PoC1 AsyncTextureLoader (WorkerThreadPool + Submit-Poll):
///   1. Render thread: `Submit(path)` — enqueue for background decode.
///   2. Worker thread: `stbi_load()` — CPU decode on N threads.
///   3. Render thread: `PollCompleted()` — retrieve decoded pixel data.
///   4. Render thread: create GPU texture + upload (batched, single WaitIdle).
///
/// # Pixel Ownership
/// `PollCompleted()` transfers ownership of `pixels` to the caller. The caller
/// MUST free each result's pixel data via `stbi_image_free()` after GPU upload.
/// Any unclaimed results are freed during `Shutdown()` / destructor.
///
/// # Thread Safety
/// `Submit()`, `PollCompleted()`, and `PendingCount()` are all thread-safe.
/// The decoder has no dependency on RHI — it is purely CPU work.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TextureDecodeResult — Output of a background CPU decode
// ─────────────────────────────────────────────────────────────────────────────

/// Result of an async texture decode (CPU-side only, no GPU work).
///
/// # Fields
/// - `path`   — Original file path submitted for decode.
/// - `pixels` — RGBA8 pixel data (stbi-allocated). Caller must free with
///              `stbi_image_free()` after GPU upload. Null if decode failed.
/// - `width`  — Image width in texels. 0 if decode failed.
/// - `height` — Image height in texels. 0 if decode failed.
struct TextureDecodeResult {
    std::string path;
    uint8_t*    pixels = nullptr;   ///< Mip 0 RGBA8 data (stbi-allocated). Caller frees via stbi_image_free.
    uint32_t    width  = 0;
    uint32_t    height = 0;

    /// Pre-computed CPU mipchain (mip 1, 2, ..., N-1). Generated on worker thread.
    /// Each entry is malloc'd by the worker. Caller frees via free().
    /// mipData[0] = mip 1 pixels, mipData[1] = mip 2, etc.
    std::vector<uint8_t*> mipData;
    std::vector<uint32_t> mipWidths;
    std::vector<uint32_t> mipHeights;
    uint16_t              mipCount = 1; ///< Total mip levels (including mip 0)
};

// ─────────────────────────────────────────────────────────────────────────────
// AsyncTextureDecoder
// ─────────────────────────────────────────────────────────────────────────────

/// Worker thread pool for CPU texture decoding.
///
/// # Usage
/// ```cpp
/// AsyncTextureDecoder decoder(2);          // 2 worker threads
/// decoder.Submit("assets/textures/brick.png");
/// // ... next frame(s) ...
/// auto results = decoder.PollCompleted();
/// for (auto& r : results) {
///     if (r.pixels) {
///         // Create GPU texture, upload r.pixels via UploadContext
///         stbi_image_free(r.pixels);
///     }
/// }
/// ```
///
/// # Lifetime
/// Workers are started in the constructor and joined in the destructor
/// (or explicit `Shutdown()`). Any unclaimed completed results have their
/// pixel data freed during shutdown.
class AsyncTextureDecoder {
public:
    /// Starts `numThreads` worker threads (clamped to [1, 8]).
    ///
    /// # Arguments
    /// * `numThreads` — Number of worker threads. Defaults to 2, which
    ///   provides good parallelism without starving the render thread
    ///   on typical 4-8 core CPUs.
    explicit AsyncTextureDecoder(int numThreads = 2);

    /// Shuts down workers and frees any unclaimed results.
    ~AsyncTextureDecoder();

    /// Non-copyable — owns threads.
    AsyncTextureDecoder(const AsyncTextureDecoder&) = delete;
    AsyncTextureDecoder& operator=(const AsyncTextureDecoder&) = delete;

    /// Non-movable — threads reference `this`.
    AsyncTextureDecoder(AsyncTextureDecoder&&) = delete;
    AsyncTextureDecoder& operator=(AsyncTextureDecoder&&) = delete;

    /// Enqueues a texture file path for background decode. Thread-safe.
    ///
    /// The worker thread will call `stbi_load(path, ..., 4)` to produce
    /// RGBA8 pixel data. The result is retrievable via `PollCompleted()`.
    ///
    /// # Arguments
    /// * `path` — Absolute or VFS-resolved file path to the image.
    void Submit(const std::string& path);

    /// Retrieves all decode results completed since the last poll.
    /// Thread-safe.
    ///
    /// Transfers pixel data ownership to the caller. Each result with
    /// non-null `pixels` must be freed via `stbi_image_free()`.
    /// Results with null `pixels` indicate a decode failure for that path.
    ///
    /// # Returns
    /// Vector of completed results (may be empty if nothing finished yet).
    [[nodiscard]] std::vector<TextureDecodeResult> PollCompleted();

    /// Number of pending tasks (queued + currently decoding). Thread-safe.
    [[nodiscard]] size_t PendingCount() const;

    /// Stops all workers and frees unclaimed results. Safe to call
    /// multiple times or without prior `Submit()` calls.
    void Shutdown();

private:
    /// Worker thread entry point. Loops until `m_stop` is set.
    void WorkerLoop();

    std::vector<std::thread>              m_workers;
    std::deque<std::string>               m_queue;      ///< Paths waiting to be decoded.
    std::vector<TextureDecodeResult>      m_completed;  ///< Decoded results ready for pickup.
    mutable std::mutex                    m_mutex;
    std::condition_variable               m_cv;
    std::atomic<bool>                     m_stop{false};
    std::atomic<size_t>                   m_inFlight{0}; ///< Currently decoding on workers.
};

} // namespace uz
