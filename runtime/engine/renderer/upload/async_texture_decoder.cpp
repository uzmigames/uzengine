/// UzEngine Async Texture Decoder — Implementation.
///
/// Workers block on a condition variable waiting for submitted paths.
/// When a path arrives, the worker calls stbi_load() to decode the image
/// into RGBA8 pixels and pushes the result to the completed queue. The
/// render thread polls completed results via PollCompleted().
///
/// Shutdown joins all workers and frees any unclaimed pixel buffers.
///
/// # Thread Safety
/// All shared state (m_queue, m_completed) is protected by m_mutex.
/// m_inFlight uses relaxed atomics because it is only used for the
/// PendingCount() diagnostic query — exact ordering is not required.

#include "async_texture_decoder.h"
#include "log.h"

#include <stb_image.h>

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

AsyncTextureDecoder::AsyncTextureDecoder(int numThreads) {
    const int n = std::clamp(numThreads, 1, 8);
    m_workers.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        m_workers.emplace_back(&AsyncTextureDecoder::WorkerLoop, this);
    }
    UZ_LOG(Renderer, Info, "AsyncTextureDecoder: started {} worker threads", n);
}

AsyncTextureDecoder::~AsyncTextureDecoder() {
    Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void AsyncTextureDecoder::Submit(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(path);
    }
    m_cv.notify_one();
}

std::vector<TextureDecodeResult> AsyncTextureDecoder::PollCompleted() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<TextureDecodeResult> results;
    results.swap(m_completed);
    return results;
}

size_t AsyncTextureDecoder::PendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size() + m_inFlight.load(std::memory_order_relaxed);
}

void AsyncTextureDecoder::Shutdown() {
    // Only run shutdown logic once. If already stopped, return immediately.
    if (m_stop.exchange(true)) return;

    // Wake all workers so they observe m_stop and exit.
    m_cv.notify_all();
    for (auto& w : m_workers) {
        if (w.joinable()) w.join();
    }
    m_workers.clear();

    // Free any unclaimed completed results to avoid leaking stbi-allocated memory.
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& r : m_completed) {
        if (r.pixels) {
            stbi_image_free(r.pixels);
            r.pixels = nullptr;
        }
        for (auto* mip : r.mipData) {
            if (mip) std::free(mip);
        }
        r.mipData.clear();
    }
    m_completed.clear();
    m_queue.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Worker Thread
// ─────────────────────────────────────────────────────────────────────────────

void AsyncTextureDecoder::WorkerLoop() {
    while (true) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_stop.load(std::memory_order_relaxed) || !m_queue.empty();
            });

            if (m_stop.load(std::memory_order_relaxed) && m_queue.empty()) {
                return;
            }

            path = std::move(m_queue.front());
            m_queue.erase(m_queue.begin());
            m_inFlight.fetch_add(1, std::memory_order_relaxed);
        }

        // Decode on this worker thread — the whole point is to move stbi_load
        // off the render thread so multiple textures decode in parallel.
        int w = 0;
        int h = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);

        TextureDecodeResult result;
        result.path = std::move(path);

        if (pixels && w > 0 && h > 0) {
            result.pixels = pixels;
            result.width  = static_cast<uint32_t>(w);
            result.height = static_cast<uint32_t>(h);

            // Generate CPU mipchain on worker thread (box filter downsampling)
            // This moves the expensive mip computation off the main thread entirely.
            const uint32_t tw = result.width;
            const uint32_t th = result.height;
            const uint32_t maxDim = tw > th ? tw : th;
            uint16_t totalMips = 1;
            { uint32_t d = maxDim; while (d > 1) { d >>= 1; ++totalMips; } }
            result.mipCount = totalMips;

            if (totalMips > 1) {
                result.mipData.resize(totalMips - 1);
                result.mipWidths.resize(totalMips - 1);
                result.mipHeights.resize(totalMips - 1);

                const uint8_t* srcPixels = pixels;
                uint32_t srcW = tw;
                uint32_t srcH = th;

                for (uint16_t mip = 1; mip < totalMips; ++mip) {
                    uint32_t mipW = srcW > 1 ? srcW / 2 : 1;
                    uint32_t mipH = srcH > 1 ? srcH / 2 : 1;
                    uint32_t mipSize = mipW * mipH * 4;
                    auto* mipPixels = static_cast<uint8_t*>(std::malloc(mipSize));

                    // Box filter 2x2 downsample
                    for (uint32_t y = 0; y < mipH; ++y) {
                        for (uint32_t x = 0; x < mipW; ++x) {
                            uint32_t sx = x * 2;
                            uint32_t sy = y * 2;
                            uint32_t sx1 = sx + 1 < srcW ? sx + 1 : sx;
                            uint32_t sy1 = sy + 1 < srcH ? sy + 1 : sy;
                            for (int c = 0; c < 4; ++c) {
                                uint32_t sum = srcPixels[(sy * srcW + sx) * 4 + c]
                                             + srcPixels[(sy * srcW + sx1) * 4 + c]
                                             + srcPixels[(sy1 * srcW + sx) * 4 + c]
                                             + srcPixels[(sy1 * srcW + sx1) * 4 + c];
                                mipPixels[(y * mipW + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
                            }
                        }
                    }

                    result.mipData[mip - 1]    = mipPixels;
                    result.mipWidths[mip - 1]  = mipW;
                    result.mipHeights[mip - 1] = mipH;

                    srcPixels = mipPixels;
                    srcW = mipW;
                    srcH = mipH;
                }
            }
        } else {
            // Decode failed — log from the worker thread. UZ_LOG is thread-safe.
            if (pixels) {
                stbi_image_free(pixels);
            }
            UZ_LOG(Renderer, Error,
                   "AsyncTextureDecoder: failed to decode '{}': {}",
                   result.path,
                   stbi_failure_reason() ? stbi_failure_reason() : "unknown");
            result.pixels = nullptr;
            result.width  = 0;
            result.height = 0;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_completed.push_back(std::move(result));
        }
        m_inFlight.fetch_sub(1, std::memory_order_relaxed);
    }
}

} // namespace uz
