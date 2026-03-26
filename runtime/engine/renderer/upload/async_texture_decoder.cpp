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
#include "vfs.h"
#include "log.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdio>

#include "engine/core/simd_dispatch.h"

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

void AsyncTextureDecoder::Submit(const std::string& path, bool isSRGB,
                                  const std::string& resultKey) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DecodeRequest req;
        req.path      = path;
        req.resultKey = resultKey;
        req.isSRGB    = isSRGB;
        m_queue.push_back(std::move(req));
    }
    m_cv.notify_one();
}

void AsyncTextureDecoder::SubmitFromMemory(std::vector<uint8_t>&& data, const std::string& path, bool isSRGB) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DecodeRequest req;
        req.path       = path;
        req.memoryData = std::move(data);
        req.isSRGB     = isSRGB;
        m_queue.push_back(std::move(req));
    }
    m_cv.notify_one();
}

void AsyncTextureDecoder::SubmitVFS(IVFS* vfs, const std::string& logicalPath,
                                     bool isSRGB, const std::string& resultKey) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DecodeRequest req;
        req.path      = logicalPath;
        req.resultKey = resultKey;
        req.isSRGB    = isSRGB;
        req.vfs       = vfs;
        m_queue.push_back(std::move(req));
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
        DecodeRequest request;
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

        // Resolve result key: if resultKey is set, use it as the result path
        // so the caller's pending slot map can find it (e.g., reading
        // "foo.uztexture" but the slot is keyed by "foo.jpg").
        const std::string resultPath = request.resultKey.empty()
            ? request.path : request.resultKey;

        // ── VFS path: read file bytes on this worker thread ──────────────
        // If the request carries a VFS pointer, the file hasn't been read yet.
        // We read it here on the worker thread (VFS is thread-safe), then fall
        // through to the existing memory-based decode paths below.
        if (request.vfs && request.memoryData.empty()) {
            request.memoryData = request.vfs->Read(request.path);
            if (request.memoryData.empty()) {
                // VFS read failed — push empty result so the caller sees the failure.
                UZ_LOG(Renderer, Error,
                       "AsyncTextureDecoder: VFS read failed for '{}'", request.path);
                TextureDecodeResult result;
                result.path   = resultPath;
                result.isSRGB = request.isSRGB;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_completed.push_back(std::move(result));
                }
                m_inFlight.fetch_sub(1, std::memory_order_relaxed);
                continue;
            }
            // Clear VFS pointer — data is now in memoryData, fall through to decode.
            request.vfs = nullptr;
        }

        // ── .uztexture fast path: pass raw bytes through without CPU decode ──
        // The .uztexture format contains GPU-ready mip data (BC7/BC1/etc.), so
        // there is nothing for stbi to decode. We detect the magic and forward
        // the raw file bytes to the render thread for direct GPU upload via
        // TextureLoader::LoadUzTex().
        //
        // Magic: bytes 0x55 0x54 0x58 0x5A = "UZTX" little-endian.

        // Check in-memory data first (PAK/VFS path).
        bool isUzTex = false;
        if (request.memoryData.size() >= 4) {
            isUzTex = (request.memoryData[0] == 0x55 &&
                       request.memoryData[1] == 0x54 &&
                       request.memoryData[2] == 0x58 &&
                       request.memoryData[3] == 0x5A);
        }

        if (isUzTex) {
            // .uztexture from memory: pass raw bytes through — no CPU decode.
            TextureDecodeResult result;
            result.path = resultPath;
            result.isSRGB = request.isSRGB;
            result.uzTexData = std::move(request.memoryData);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_completed.push_back(std::move(result));
            }
            m_inFlight.fetch_sub(1, std::memory_order_relaxed);
            continue;
        }

        // Check filesystem path: peek at first 4 bytes to detect .uztexture.
        if (request.memoryData.empty()) {
            FILE* f = fopen(request.path.c_str(), "rb");
            if (f) {
                uint8_t magic[4] = {};
                if (fread(magic, 1, 4, f) == 4 &&
                    magic[0] == 0x55 && magic[1] == 0x54 &&
                    magic[2] == 0x58 && magic[3] == 0x5A) {
                    // Read the entire .uztexture file into memory.
                    fseek(f, 0, SEEK_END);
                    const long fileSize = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (fileSize > 0) {
                        TextureDecodeResult result;
                        result.path = resultPath;
                        result.isSRGB = request.isSRGB;
                        result.uzTexData.resize(static_cast<size_t>(fileSize));
                        fread(result.uzTexData.data(), 1, static_cast<size_t>(fileSize), f);
                        fclose(f);
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_completed.push_back(std::move(result));
                        }
                        m_inFlight.fetch_sub(1, std::memory_order_relaxed);
                        continue;
                    }
                }
                fclose(f);
            }
        }

        // ── Standard image decode path (PNG/JPG/TGA/BMP/HDR) ────────────
        // Decode on this worker thread — the whole point is to move stbi_load
        // off the render thread so multiple textures decode in parallel.
        int w = 0;
        int h = 0;
        int channels = 0;
        unsigned char* pixels = nullptr;

        if (!request.memoryData.empty()) {
            // PAK/VFS path: image bytes already in memory, decode without filesystem I/O.
            pixels = stbi_load_from_memory(
                request.memoryData.data(),
                static_cast<int>(request.memoryData.size()),
                &w, &h, &channels, 4);
            // Release the source buffer immediately after decode to free memory.
            request.memoryData.clear();
            request.memoryData.shrink_to_fit();
        } else {
            // Filesystem path: read and decode from disk.
            pixels = stbi_load(request.path.c_str(), &w, &h, &channels, 4);
        }

        TextureDecodeResult result;
        result.path   = resultPath;
        result.isSRGB = request.isSRGB;

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

                    // Box filter 2x2 downsample with SIMD dispatch.
                    //
                    // Each output pixel = average of 2x2 source block with rounding:
                    //   result = (tl + tr + bl + br + 2) / 4
                    //
                    // Dispatch order: AVX2 (runtime) -> SSE2 (baseline) -> NEON (ARM64) -> scalar.
                    // All SIMD paths produce bit-identical results to the scalar reference.
                    //
                    // We unpack uint8 to uint16 for the addition to avoid overflow
                    // (max sum = 255*4 = 1020, fits in uint16). Two _mm_avg_epu8
                    // calls would give different rounding, so we must do it manually.

#if UZ_SIMD_AVX2
                    const bool useAvx2 = uz::SimdHasAVX2() && srcW >= 2 && srcH >= 2 && mipW >= 8;
#endif
#if UZ_SIMD_SSE2
                    const bool useSse2 = srcW >= 2 && srcH >= 2 && mipW >= 4;
#endif

                    for (uint32_t y = 0; y < mipH; ++y) {
                        const uint32_t sy  = y * 2;
                        const uint32_t sy1 = sy + 1 < srcH ? sy + 1 : sy;
                        const uint8_t* row0 = srcPixels + sy  * srcW * 4;
                        const uint8_t* row1 = srcPixels + sy1 * srcW * 4;

                        uint32_t x = 0;

#if UZ_SIMD_AVX2
                        // AVX2 fast path — 8 output pixels (32 bytes) per iteration.
                        // Loads 16 source pixels (64 bytes) from each row per iteration.
                        if (useAvx2) {
                            const __m256i zero256 = _mm256_setzero_si256();
                            const __m256i bias256 = _mm256_set1_epi16(2);
                            const uint32_t avxEnd = mipW & ~7u;

                            for (; x < avxEnd; x += 8) {
                                const uint32_t sx = x * 2;
                                const uint8_t* src0 = row0 + sx * 4;
                                const uint8_t* src1 = row1 + sx * 4;

                                // Load 16 source pixels (64 bytes) from each row as four 128-bit loads,
                                // then widen to uint16 in 256-bit registers.
                                // Chunk layout: [px0 px1 px2 px3] [px4 px5 px6 px7] [px8..11] [px12..15]
                                const __m128i r0_a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src0));
                                const __m128i r0_b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src0 + 16));
                                const __m128i r0_c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src0 + 32));
                                const __m128i r0_d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src0 + 48));
                                const __m128i r1_a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src1));
                                const __m128i r1_b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src1 + 16));
                                const __m128i r1_c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src1 + 32));
                                const __m128i r1_d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src1 + 48));

                                // Widen each 128-bit chunk (4 pixels, 16 bytes) to 256-bit (8 x uint16).
                                // _mm256_cvtepu8_epi16 zero-extends 16 bytes -> 16 uint16 values.
                                const __m256i r0_a16 = _mm256_cvtepu8_epi16(r0_a);  // px0..px3 as u16
                                const __m256i r0_b16 = _mm256_cvtepu8_epi16(r0_b);  // px4..px7
                                const __m256i r0_c16 = _mm256_cvtepu8_epi16(r0_c);  // px8..px11
                                const __m256i r0_d16 = _mm256_cvtepu8_epi16(r0_d);  // px12..px15
                                const __m256i r1_a16 = _mm256_cvtepu8_epi16(r1_a);
                                const __m256i r1_b16 = _mm256_cvtepu8_epi16(r1_b);
                                const __m256i r1_c16 = _mm256_cvtepu8_epi16(r1_c);
                                const __m256i r1_d16 = _mm256_cvtepu8_epi16(r1_d);

                                // Each 256-bit register holds 4 pixels as 16 uint16 channels:
                                //   [px0.R px0.G px0.B px0.A  px1.R px1.G px1.B px1.A
                                //    px2.R px2.G px2.B px2.A  px3.R px3.G px3.B px3.A]
                                //
                                // For 2x2 box filter we need: even pixel + odd pixel from each pair.
                                // Pairs: (px0,px1), (px2,px3) from chunk a; (px4,px5), (px6,px7) from chunk b; etc.
                                //
                                // Strategy: use _mm256_hadd_epi16 to add adjacent pairs of uint16 values.
                                // hadd([a0 a1 a2 a3 a4 a5 a6 a7 | a8 a9 a10 a11 a12 a13 a14 a15],
                                //      [b0 b1 ...]) =
                                //   [a0+a1 a2+a3 a4+a5 a6+a7 b0+b1 b2+b3 b4+b5 b6+b7 |
                                //    a8+a9 a10+a11 a12+a13 a14+a15 b8+b9 ...]
                                //
                                // This adds adjacent RGBA channels together — not what we want directly.
                                // Instead, we manually separate even/odd pixels.

                                // Extract even pixels (px0, px2) and odd pixels (px1, px3) from chunk a.
                                // Lower 128 of r0_a16 = [px0.RGBA px1.RGBA], upper 128 = [px2.RGBA px3.RGBA].
                                // Even = lower 64 bits of each 128-bit lane, odd = upper 64 bits.
                                // _mm256_unpacklo_epi64 / _mm256_unpackhi_epi64 operate per 128-bit lane:
                                //   unpacklo([px0 px1 | px2 px3], [px0 px1 | px2 px3]) = [px0 px0 | px2 px2]
                                // That's not useful. Use shuffle instead.

                                // Simpler approach: split into 128-bit halves, add even+odd per half,
                                // then combine. This avoids cross-lane issues with AVX2 permutes.
                                // lower(r0_a16) = [px0.R G B A  px1.R G B A] as u16 (8 values)
                                // upper(r0_a16) = [px2.R G B A  px3.R G B A] as u16
                                // sum_pair_lo = lower_lo_64 + lower_hi_64 = px0 + px1 (4 channels)
                                // sum_pair_hi = upper_lo_64 + upper_hi_64 = px2 + px3 (4 channels)

                                // Helper lambda: add even+odd pixel pairs within a 256-bit register
                                // containing 4 pixels. Returns the 4 channel sums for each of the 2
                                // output pixels packed into a single __m128i [out0.RGBA out1.RGBA].
                                auto addPairs256 = [](__m256i v) -> __m128i {
                                    const __m128i lo = _mm256_castsi256_si128(v);
                                    const __m128i hi = _mm256_extracti128_si256(v, 1);
                                    // lo = [px0.RGBA px1.RGBA], hi = [px2.RGBA px3.RGBA]
                                    // px0+px1 channels in lower 64 bits, px2+px3 in lower 64 bits
                                    const __m128i sum_lo = _mm_add_epi16(lo, _mm_srli_si128(lo, 8));
                                    const __m128i sum_hi = _mm_add_epi16(hi, _mm_srli_si128(hi, 8));
                                    return _mm_unpacklo_epi64(sum_lo, sum_hi);
                                };

                                // Add even+odd pixel pairs for each chunk (2 output pixels per chunk).
                                // r0_pair_ab = [out0.RGBA out1.RGBA] from row0 chunk a
                                const __m128i r0_pair_a = addPairs256(r0_a16);  // out0, out1
                                const __m128i r0_pair_b = addPairs256(r0_b16);  // out2, out3
                                const __m128i r0_pair_c = addPairs256(r0_c16);  // out4, out5
                                const __m128i r0_pair_d = addPairs256(r0_d16);  // out6, out7
                                const __m128i r1_pair_a = addPairs256(r1_a16);
                                const __m128i r1_pair_b = addPairs256(r1_b16);
                                const __m128i r1_pair_c = addPairs256(r1_c16);
                                const __m128i r1_pair_d = addPairs256(r1_d16);

                                // Sum row0 + row1 for each output pixel pair, add bias, shift.
                                const __m128i bias128 = _mm_set1_epi16(2);
                                __m128i s_a = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(r0_pair_a, r1_pair_a), bias128), 2);
                                __m128i s_b = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(r0_pair_b, r1_pair_b), bias128), 2);
                                __m128i s_c = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(r0_pair_c, r1_pair_c), bias128), 2);
                                __m128i s_d = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(r0_pair_d, r1_pair_d), bias128), 2);

                                // Pack u16 -> u8. packus takes two 128-bit regs of u16 and produces
                                // one 128-bit reg of u8 (16 bytes = 4 output pixels).
                                const __m128i packed_lo = _mm_packus_epi16(s_a, s_b);  // out0..out3
                                const __m128i packed_hi = _mm_packus_epi16(s_c, s_d);  // out4..out7

                                // Combine into a single 256-bit store (32 bytes = 8 output pixels).
                                const __m256i packed256 = _mm256_set_m128i(packed_hi, packed_lo);
                                _mm256_storeu_si256(
                                    reinterpret_cast<__m256i*>(mipPixels + (y * mipW + x) * 4),
                                    packed256);
                            }
                        }
#endif // UZ_SIMD_AVX2

#if UZ_SIMD_SSE2
                        // SSE2 path — 4 output pixels (16 bytes) per iteration.
                        // Baseline on all x86_64 processors.
                        if (useSse2) {
                            const __m128i zero = _mm_setzero_si128();
                            const __m128i bias = _mm_set1_epi16(2);
                            const uint32_t simdEnd = mipW & ~3u;

                            for (; x < simdEnd; x += 4) {
                                const uint32_t sx = x * 2;

                                // Load 8 source pixels (32 bytes) from each row as two 128-bit loads.
                                const __m128i r0_lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row0 + sx * 4));
                                const __m128i r0_hi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row0 + sx * 4 + 16));
                                const __m128i r1_lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row1 + sx * 4));
                                const __m128i r1_hi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row1 + sx * 4 + 16));

                                // Unpack to uint16 (zero-extend).
                                const __m128i r0_p01 = _mm_unpacklo_epi8(r0_lo, zero);
                                const __m128i r0_p23 = _mm_unpackhi_epi8(r0_lo, zero);
                                const __m128i r0_p45 = _mm_unpacklo_epi8(r0_hi, zero);
                                const __m128i r0_p67 = _mm_unpackhi_epi8(r0_hi, zero);
                                const __m128i r1_p01 = _mm_unpacklo_epi8(r1_lo, zero);
                                const __m128i r1_p23 = _mm_unpackhi_epi8(r1_lo, zero);
                                const __m128i r1_p45 = _mm_unpacklo_epi8(r1_hi, zero);
                                const __m128i r1_p67 = _mm_unpackhi_epi8(r1_hi, zero);

                                // Add even + odd pixel within each pair per row.
                                // srli_si128(v, 8) shifts the upper 64 bits (odd pixel) into the lower 64 bits.
                                const __m128i r0_s01 = _mm_add_epi16(r0_p01, _mm_srli_si128(r0_p01, 8));
                                const __m128i r0_s23 = _mm_add_epi16(r0_p23, _mm_srli_si128(r0_p23, 8));
                                const __m128i r0_s45 = _mm_add_epi16(r0_p45, _mm_srli_si128(r0_p45, 8));
                                const __m128i r0_s67 = _mm_add_epi16(r0_p67, _mm_srli_si128(r0_p67, 8));
                                const __m128i r1_s01 = _mm_add_epi16(r1_p01, _mm_srli_si128(r1_p01, 8));
                                const __m128i r1_s23 = _mm_add_epi16(r1_p23, _mm_srli_si128(r1_p23, 8));
                                const __m128i r1_s45 = _mm_add_epi16(r1_p45, _mm_srli_si128(r1_p45, 8));
                                const __m128i r1_s67 = _mm_add_epi16(r1_p67, _mm_srli_si128(r1_p67, 8));

                                // Sum row0 + row1, add bias 2, divide by 4 (shift right 2).
                                __m128i sum0 = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(r0_s01, r1_s01), bias), 2);
                                __m128i sum1 = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(r0_s23, r1_s23), bias), 2);
                                __m128i sum2 = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(r0_s45, r1_s45), bias), 2);
                                __m128i sum3 = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(r0_s67, r1_s67), bias), 2);

                                // Combine 4 output pixels: merge lower 64 bits of each sum, then pack u16->u8.
                                const __m128i pair01 = _mm_unpacklo_epi64(sum0, sum1);
                                const __m128i pair23 = _mm_unpacklo_epi64(sum2, sum3);
                                const __m128i packed = _mm_packus_epi16(pair01, pair23);

                                _mm_storeu_si128(reinterpret_cast<__m128i*>(mipPixels + (y * mipW + x) * 4), packed);
                            }
                        }
#endif // UZ_SIMD_SSE2

#if UZ_SIMD_NEON
                        // NEON path — 2 output pixels (8 bytes) per iteration.
                        // Baseline on ARM64, so no runtime check needed.
                        if (srcW >= 2 && srcH >= 2 && mipW >= 2) {
                            const uint16x8_t biasNeon = vdupq_n_u16(2);
                            const uint32_t neonEnd = mipW & ~1u;

                            for (; x < neonEnd; x += 2) {
                                const uint32_t sx = x * 2;
                                const uint8_t* src0 = row0 + sx * 4;
                                const uint8_t* src1 = row1 + sx * 4;

                                // Load 4 source pixels (16 bytes) from each row.
                                // [px0.RGBA px1.RGBA px2.RGBA px3.RGBA]
                                const uint8x16_t r0 = vld1q_u8(src0);
                                const uint8x16_t r1 = vld1q_u8(src1);

                                // Pairwise add adjacent bytes -> uint16.
                                // vpaddlq_u8 adds pairs: [px0.R+px0.G, px0.B+px0.A, px1.R+px1.G, ...]
                                // That adds adjacent channels, not adjacent pixels — wrong granularity.
                                //
                                // Instead: extract each pixel as uint8x8 (8 bytes, only lower 4 used),
                                // widen to u16, and add manually.
                                //
                                // px0 = bytes [0..3], px1 = bytes [4..7],
                                // px2 = bytes [8..11], px3 = bytes [12..15]
                                // out0 = (px0 + px1 + px0' + px1') where ' = row1

                                // Widen entire 16 bytes to uint16 (two 8-element registers).
                                const uint16x8_t r0_lo16 = vmovl_u8(vget_low_u8(r0));   // px0.RGBA px1.RGBA
                                const uint16x8_t r0_hi16 = vmovl_u8(vget_high_u8(r0));  // px2.RGBA px3.RGBA
                                const uint16x8_t r1_lo16 = vmovl_u8(vget_low_u8(r1));
                                const uint16x8_t r1_hi16 = vmovl_u8(vget_high_u8(r1));

                                // Add even+odd pixels: extract lower/upper 4 u16 (one pixel each).
                                // vget_low_u16 = [px0.RGBA], vget_high_u16 = [px1.RGBA]
                                // Sum even+odd per row.
                                const uint16x4_t r0_sum0 = vadd_u16(vget_low_u16(r0_lo16), vget_high_u16(r0_lo16));
                                const uint16x4_t r0_sum1 = vadd_u16(vget_low_u16(r0_hi16), vget_high_u16(r0_hi16));
                                const uint16x4_t r1_sum0 = vadd_u16(vget_low_u16(r1_lo16), vget_high_u16(r1_lo16));
                                const uint16x4_t r1_sum1 = vadd_u16(vget_low_u16(r1_hi16), vget_high_u16(r1_hi16));

                                // Combine two output pixels into one 8-element u16 register.
                                const uint16x8_t sum_r0 = vcombine_u16(r0_sum0, r0_sum1);
                                const uint16x8_t sum_r1 = vcombine_u16(r1_sum0, r1_sum1);

                                // Sum rows, add bias 2, shift right 2.
                                const uint16x8_t total = vaddq_u16(sum_r0, sum_r1);
                                const uint16x8_t biased = vaddq_u16(total, biasNeon);
                                const uint8x8_t result8 = vshrn_n_u16(biased, 2);

                                // Store 8 bytes = 2 output pixels.
                                vst1_u8(mipPixels + (y * mipW + x) * 4, result8);
                            }
                        }
#endif // UZ_SIMD_NEON

                        // Scalar fallback for remaining pixels or small mips.
                        for (; x < mipW; ++x) {
                            const uint32_t sx  = x * 2;
                            const uint32_t sx1 = sx + 1 < srcW ? sx + 1 : sx;
                            for (int c = 0; c < 4; ++c) {
                                const uint32_t sum = srcPixels[(sy  * srcW + sx)  * 4 + c]
                                                   + srcPixels[(sy  * srcW + sx1) * 4 + c]
                                                   + srcPixels[(sy1 * srcW + sx)  * 4 + c]
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
