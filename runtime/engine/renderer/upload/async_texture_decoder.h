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

class IVFS;  // Forward declaration — full definition in engine/asset/vfs.h

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
    bool        isSRGB = false;     ///< True for color data (albedo, emissive). False for linear data (normal, roughness, metallic, AO, ORM).

    /// Pre-computed CPU mipchain (mip 1, 2, ..., N-1). Generated on worker thread.
    /// Each entry is malloc'd by the worker. Caller frees via free().
    /// mipData[0] = mip 1 pixels, mipData[1] = mip 2, etc.
    std::vector<uint8_t*> mipData;
    std::vector<uint32_t> mipWidths;
    std::vector<uint32_t> mipHeights;
    uint16_t              mipCount = 1; ///< Total mip levels (including mip 0)

    /// Pre-parsed .uztexture data. When non-empty, the texture was a .uztexture
    /// file and should be uploaded via TextureLoader::LoadUzTex() instead of
    /// the normal RGBA8 pixel upload path. The mipData/mipWidths/mipHeights
    /// fields are unused in this case.
    std::vector<uint8_t> uzTexData;
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
    /// * `isSRGB` — True if this texture contains color data (albedo, emissive)
    ///   that should be created with an sRGB format for automatic gamma-to-linear
    ///   conversion on GPU sample. False for linear data (normal, roughness, etc.).
    void Submit(const std::string& path, bool isSRGB = false,
                const std::string& resultKey = {});

    /// Enqueues pre-read image bytes for background decode. Thread-safe.
    /// Takes ownership of the data buffer via std::vector move.
    ///
    /// Used for PAK/VFS textures where the compressed image bytes are already
    /// in memory. The worker thread will call `stbi_load_from_memory()` instead
    /// of `stbi_load()`, avoiding a redundant filesystem read.
    ///
    /// # Arguments
    /// * `data` — Compressed image bytes (PNG/JPEG/etc). Moved into the decoder.
    /// * `path` — Logical path for the texture (used as a key in decode results).
    /// * `isSRGB` — True if this texture contains color data (albedo, emissive)
    ///   that should be created with an sRGB format. Propagated to DecodeResult.
    void SubmitFromMemory(std::vector<uint8_t>&& data, const std::string& path, bool isSRGB = false);

    /// Enqueues a VFS logical path for background read + decode. Thread-safe.
    ///
    /// The worker thread calls `vfs->Read(logicalPath)` on the background thread
    /// to load the file bytes, then decodes (stbi) or passes through (.uztexture).
    /// This moves ALL file I/O off the render thread — the main thread only enqueues.
    ///
    /// # Arguments
    /// * `vfs` — Thread-safe VFS backend. Must outlive all pending requests.
    ///   PakVFS and LooseFileVFS are both internally thread-safe.
    /// * `logicalPath` — VFS logical asset path (e.g., "textures/brick.uztexture").
    /// * `isSRGB` — True if this texture contains color data (albedo, emissive).
    /// # Arguments
    /// * `resultKey` — If non-empty, used as `result.path` instead of `logicalPath`.
    ///   Needed when the VFS read path differs from the key used in pending slot maps
    ///   (e.g., reading "foo.uztexture" but the slot is keyed by "foo.jpg").
    void SubmitVFS(IVFS* vfs, const std::string& logicalPath, bool isSRGB = false,
                    const std::string& resultKey = {});

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

    /// Internal queue entry — holds either a file path (for filesystem loads)
    /// or pre-read memory bytes (for PAK/VFS loads). If `memoryData` is non-empty,
    /// the worker uses `stbi_load_from_memory()`; otherwise `stbi_load()`.
    struct DecodeRequest {
        std::string          path;          ///< File path or VFS logical path to read.
        std::string          resultKey;     ///< If non-empty, used as result.path instead of path.
        std::vector<uint8_t> memoryData;    ///< Empty = load from file path or VFS.
        bool                 isSRGB = false;///< Propagated to DecodeResult for GPU format selection.
        IVFS*                vfs = nullptr; ///< If non-null, worker reads via vfs->Read(path). Non-owning.
    };

    std::vector<std::thread>              m_workers;
    std::deque<DecodeRequest>             m_queue;      ///< Requests waiting to be decoded.
    std::vector<TextureDecodeResult>      m_completed;  ///< Decoded results ready for pickup.
    mutable std::mutex                    m_mutex;
    std::condition_variable               m_cv;
    std::atomic<bool>                     m_stop{false};
    std::atomic<size_t>                   m_inFlight{0}; ///< Currently decoding on workers.
};

} // namespace uz
