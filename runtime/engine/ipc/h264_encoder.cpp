/// H264Encoder implementation — FFmpeg libavcodec H.264 encoding with
/// hardware acceleration and automatic software fallback.
///
/// The encoder probes hardware encoders in priority order and selects
/// the first one that successfully opens. Color conversion from RGBA
/// to NV12 is performed by libswscale on every frame.

#include "h264_encoder.h"

#include "log.h"
#include "profiler.h"
#if defined(UZ_HAS_FFMPEG) || defined(_WIN32)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace uz {

UZ_DEFINE_LOG_CATEGORY(H264, Info);

// ─────────────────────────────────────────────────────────────────────────────
// PIMPL implementation
// ─────────────────────────────────────────────────────────────────────────────

struct H264Encoder::Impl {
    AVCodecContext* codecCtx = nullptr;
    SwsContext* swsCtx       = nullptr;
    AVFrame* frame           = nullptr;
    AVPacket* packet         = nullptr;

    /// Accumulated output buffer for the current frame's NAL units.
    /// Reused across calls to avoid per-frame allocation.
    std::vector<uint8_t> outputBuffer;

    /// Current configuration (copied at Initialize time).
    Config config{};

    /// Name of the active encoder codec (e.g., "h264_nvenc").
    const char* encoderName = "none";

    /// Monotonically increasing presentation timestamp.
    int64_t pts = 0;

    /// When true, the next frame will be forced to IDR.
    /// Atomic because RequestKeyframe() may be called from any thread while
    /// the encoder thread reads it.
    std::atomic<bool> forceKeyframe{false};

    /// Target bitrate in bits/sec — updated by SetBitrate().
    /// Atomic because SetBitrate() may be called from the render thread while
    /// the encoder thread reads it during encoding.
    std::atomic<int64_t> targetBitrate{0};

    // ── Async encoder thread state ──────────────────────────────────────

    std::thread encoderThread;
    std::mutex mutex;
    std::condition_variable cv;
    bool threadShouldExit = false;

    /// Double-buffered staging: render thread writes to inputStaging,
    /// encoder thread reads from it after swap under the lock.
    std::vector<uint8_t> inputStaging;
    uint32_t inputStagingStride = 0;
    bool inputReady = false;

    /// Double-buffered output: encoder thread writes to pendingOutput,
    /// render thread reads from completedOutput after swap.
    std::vector<uint8_t> pendingOutput;
    std::vector<uint8_t> completedOutput;
    uint32_t completedOutputSize = 0;
    bool outputReady = false;

    /// Applies the atomic targetBitrate to the codec context if it has
    /// changed. Must be called on the thread that owns the codec context.
    void ApplyBitrateIfChanged();

    /// Converts RGBA pixel data to NV12 via swscale, sets frame metadata
    /// (pts, keyframe flags), and applies pending bitrate changes.
    /// Returns false on swscale setup failure.
    bool PrepareFrame(const uint8_t* rgbaData, uint32_t rgbaStride);

    /// The core encoding logic shared by both sync and async paths.
    /// Encodes the RGBA data currently in the NV12 frame into the given
    /// output buffer. Caller must have already run swscale and set frame
    /// metadata (pts, keyframe flags).
    ///
    /// Returns true if encoded data was produced.
    bool EncodeInto(std::vector<uint8_t>& output);

    /// Background thread entry point for async encoding.
    void EncoderThreadMain();
};

// ─────────────────────────────────────────────────────────────────────────────
// Hardware encoder probe order
// ─────────────────────────────────────────────────────────────────────────────

/// List of H.264 encoders to try, in priority order.
/// Hardware encoders are attempted first for lowest latency, then software.
static constexpr const char* kEncoderProbeOrder[] = {
    "h264_nvenc",   // NVIDIA NVENC
    "h264_amf",     // AMD AMF (Advanced Media Framework)
    "h264_qsv",     // Intel Quick Sync Video
    "h264_mf",      // Windows Media Foundation
    "libx264",      // Software fallback (x264)
};

// ─────────────────────────────────────────────────────────────────────────────
// Encoder-specific low-latency option configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Applies encoder-specific options for low-latency CBR streaming.
///
/// Each hardware encoder has its own private option namespace for tuning
/// latency, rate control, and quality. This function sets the options
/// that matter most for real-time viewport streaming: single-frame
/// latency, CBR rate control, and baseline/main profile for WebCodecs
/// compatibility.
static void ApplyEncoderOptions(AVCodecContext* ctx, const char* encoderName) {
    if (std::strcmp(encoderName, "h264_nvenc") == 0) {
        // NVENC: ultra-low latency preset, CBR, no B-frames, zero-latency.
        av_opt_set(ctx->priv_data, "preset",  "p1",           0);  // fastest
        av_opt_set(ctx->priv_data, "tune",    "ull",          0);  // ultra-low latency
        av_opt_set(ctx->priv_data, "rc",      "cbr",          0);  // constant bitrate
        av_opt_set(ctx->priv_data, "zerolatency", "1",        0);
        av_opt_set(ctx->priv_data, "delay",   "0",            0);
    } else if (std::strcmp(encoderName, "h264_amf") == 0) {
        // AMF: speed preset, CBR, low latency.
        av_opt_set(ctx->priv_data, "usage",          "ultralowlatency", 0);
        av_opt_set(ctx->priv_data, "rc",             "cbr",             0);
        av_opt_set(ctx->priv_data, "quality",        "speed",           0);
        av_opt_set(ctx->priv_data, "log_to_dbg",     "0",              0);
    } else if (std::strcmp(encoderName, "h264_qsv") == 0) {
        // QSV: very fast preset, CBR.
        av_opt_set(ctx->priv_data, "preset",  "veryfast", 0);
        av_opt_set(ctx->priv_data, "look_ahead", "0",     0);
    } else if (std::strcmp(encoderName, "h264_mf") == 0) {
        // Media Foundation: CBR, low latency.
        av_opt_set(ctx->priv_data, "rate_control", "cbr", 0);
    } else if (std::strcmp(encoderName, "libx264") == 0) {
        // x264: ultrafast preset, zerolatency tune, CBR.
        av_opt_set(ctx->priv_data, "preset",  "ultrafast",   0);
        av_opt_set(ctx->priv_data, "tune",    "zerolatency", 0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TryOpenEncoder — attempt to open a single encoder by name
// ─────────────────────────────────────────────────────────────────────────────

/// Attempts to find and open the named encoder with the given config.
///
/// Returns a fully configured and opened AVCodecContext on success, or
/// nullptr if the encoder is not available or fails to open. The caller
/// takes ownership of the returned context.
static AVCodecContext* TryOpenEncoder(const char* name, const H264Encoder::Config& config) {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) {
        return nullptr;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        UZ_LOG(H264, Error, "Failed to allocate context for {}", name);
        return nullptr;
    }

    // Frame dimensions.
    ctx->width  = static_cast<int>(config.width);
    ctx->height = static_cast<int>(config.height);

    // Pixel format — all H.264 encoders accept NV12.
    ctx->pix_fmt = AV_PIX_FMT_NV12;

    // Time base: 1/fps for uniform frame durations.
    ctx->time_base = AVRational{1, static_cast<int>(config.fps)};
    ctx->framerate = AVRational{static_cast<int>(config.fps), 1};

    // Bitrate (CBR target).
    ctx->bit_rate = static_cast<int64_t>(config.bitrateKbps) * 1000;
    ctx->rc_max_rate = ctx->bit_rate;
    ctx->rc_buffer_size = static_cast<int>(ctx->bit_rate); // 1 second buffer

    // IDR interval — keyframe every N frames.
    ctx->gop_size = static_cast<int>(config.keyframeInterval);

    // No B-frames for minimum latency (single-frame encoding delay).
    ctx->max_b_frames = 0;

    // Baseline profile for maximum WebCodecs decoder compatibility.
    // Some HW encoders ignore this (NVENC defaults to main), which is fine —
    // WebCodecs handles main profile equally well.
    ctx->profile = AV_PROFILE_H264_BASELINE;

    // Threading: single-threaded encoding for deterministic frame output.
    // Hardware encoders ignore this (they use GPU parallelism internally).
    ctx->thread_count = 1;

    // Emit SPS/PPS in-band (Annex-B start codes before every IDR) rather
    // than in extradata. The WebCodecs VideoDecoder is configured without a
    // codec 'description' so it relies on in-band parameter sets.
    // Do NOT set AV_CODEC_FLAG_GLOBAL_HEADER.

    // Apply encoder-specific low-latency tuning options.
    ApplyEncoderOptions(ctx, name);

    // Attempt to open the encoder.
    const int ret = avcodec_open2(ctx, codec, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        UZ_LOG(H264, Debug, "Encoder {} failed to open: {}", name, errBuf);
        avcodec_free_context(&ctx);
        return nullptr;
    }

    return ctx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor / Move
// ─────────────────────────────────────────────────────────────────────────────

H264Encoder::H264Encoder()  = default;

H264Encoder::~H264Encoder() {
    Shutdown();
}

H264Encoder::H264Encoder(H264Encoder&& other) noexcept
    : impl_(other.impl_) {
    other.impl_ = nullptr;
}

H264Encoder& H264Encoder::operator=(H264Encoder&& other) noexcept {
    if (this != &other) {
        Shutdown();
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize
// ─────────────────────────────────────────────────────────────────────────────

bool H264Encoder::Initialize(const Config& config) {
    UZ_PROFILE_SCOPE(H264Encoder_Initialize);

    // Prevent double-init — caller must Shutdown() first.
    if (impl_) {
        UZ_LOG(H264, Error, "Already initialized — call Shutdown() before re-initializing");
        return false;
    }

    if (config.width == 0 || config.height == 0) {
        UZ_LOG(H264, Error, "Invalid dimensions: {}x{}", config.width, config.height);
        return false;
    }
    if (config.fps == 0) {
        UZ_LOG(H264, Error, "Invalid FPS: {}", config.fps);
        return false;
    }

    auto* state = new (std::nothrow) Impl{};
    if (!state) {
        UZ_LOG(H264, Error, "Failed to allocate encoder state");
        return false;
    }
    state->config = config;
    state->targetBitrate = static_cast<int64_t>(config.bitrateKbps) * 1000;

    // Probe encoders in priority order.
    AVCodecContext* ctx = nullptr;
    const char* selectedName = "none";
    for (const char* name : kEncoderProbeOrder) {
        ctx = TryOpenEncoder(name, config);
        if (ctx) {
            selectedName = name;
            UZ_LOG(H264, Info, "H.264 encoder opened: {} ({}x{} @ {} fps, {} kbps)",
                   name, config.width, config.height, config.fps, config.bitrateKbps);
            break;
        }
    }

    if (!ctx) {
        UZ_LOG(H264, Error, "No H.264 encoder available — tried: "
               "h264_nvenc, h264_amf, h264_qsv, h264_mf, libx264");
        delete state;
        return false;
    }

    state->codecCtx = ctx;
    state->encoderName = selectedName;

    // Allocate the NV12 frame buffer.
    state->frame = av_frame_alloc();
    if (!state->frame) {
        UZ_LOG(H264, Error, "Failed to allocate AVFrame");
        avcodec_free_context(&state->codecCtx);
        delete state;
        return false;
    }
    state->frame->format = AV_PIX_FMT_NV12;
    state->frame->width  = static_cast<int>(config.width);
    state->frame->height = static_cast<int>(config.height);

    int ret = av_frame_get_buffer(state->frame, 32);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        UZ_LOG(H264, Error, "Failed to allocate frame buffer: {}", errBuf);
        av_frame_free(&state->frame);
        avcodec_free_context(&state->codecCtx);
        delete state;
        return false;
    }

    // Allocate the output packet.
    state->packet = av_packet_alloc();
    if (!state->packet) {
        UZ_LOG(H264, Error, "Failed to allocate AVPacket");
        av_frame_free(&state->frame);
        avcodec_free_context(&state->codecCtx);
        delete state;
        return false;
    }

    // Create the RGBA-to-NV12 color conversion context.
    // SWS_BILINEAR is the best quality/speed tradeoff for real-time conversion.
    state->swsCtx = sws_getContext(
        static_cast<int>(config.width), static_cast<int>(config.height),
        AV_PIX_FMT_RGBA,
        static_cast<int>(config.width), static_cast<int>(config.height),
        AV_PIX_FMT_NV12,
        SWS_BILINEAR,
        nullptr, nullptr, nullptr);

    if (!state->swsCtx) {
        UZ_LOG(H264, Error, "Failed to create swscale context for RGBA->NV12");
        av_packet_free(&state->packet);
        av_frame_free(&state->frame);
        avcodec_free_context(&state->codecCtx);
        delete state;
        return false;
    }

    // Pre-allocate output buffer to avoid first-frame allocation spike.
    // A reasonable upper bound is 1/8 of the raw frame size.
    const size_t estimatedMaxNalSize = (config.width * config.height) / 8;
    state->outputBuffer.reserve(estimatedMaxNalSize > 4096 ? estimatedMaxNalSize : 4096);

    state->pts = 0;
    state->forceKeyframe.store(false, std::memory_order_relaxed);

    // Pre-allocate async staging buffers to avoid first-frame allocation.
    const size_t rawFrameBytes = static_cast<size_t>(config.width) * 4 * config.height;
    state->inputStaging.reserve(rawFrameBytes);
    state->pendingOutput.reserve(estimatedMaxNalSize > 4096 ? estimatedMaxNalSize : 4096);
    state->completedOutput.reserve(estimatedMaxNalSize > 4096 ? estimatedMaxNalSize : 4096);

    // Start the background encoder thread. It will block on the condition
    // variable until SubmitFrameAsync() provides the first frame.
    state->threadShouldExit = false;
    state->encoderThread = std::thread(&Impl::EncoderThreadMain, state);

    UZ_LOG(H264, Info, "Async encoder thread launched");

    impl_ = state;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void H264Encoder::Shutdown() {
    if (!impl_) return;

    auto* state = impl_;
    impl_ = nullptr;

    // Signal the encoder thread to exit and wait for it to finish.
    // This MUST happen before freeing FFmpeg resources because the thread
    // may be in the middle of an avcodec_send_frame/receive_packet call.
    if (state->encoderThread.joinable()) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->threadShouldExit = true;
        }
        state->cv.notify_one();
        state->encoderThread.join();
        UZ_LOG(H264, Info, "Async encoder thread joined");
    }

    // Free resources in reverse allocation order.
    if (state->swsCtx) {
        sws_freeContext(state->swsCtx);
        state->swsCtx = nullptr;
    }
    if (state->packet) {
        av_packet_free(&state->packet);
    }
    if (state->frame) {
        av_frame_free(&state->frame);
    }
    if (state->codecCtx) {
        avcodec_free_context(&state->codecCtx);
    }

    UZ_LOG(H264, Info, "H.264 encoder shut down (was: {})", state->encoderName);
    delete state;
}

// ─────────────────────────────────────────────────────────────────────────────
// EncodeFrame
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Impl::ApplyBitrateIfChanged
// ─────────────────────────────────────────────────────────────────────────────

void H264Encoder::Impl::ApplyBitrateIfChanged() {
    const int64_t desired = targetBitrate.load(std::memory_order_relaxed);
    if (desired != codecCtx->bit_rate) {
        codecCtx->bit_rate    = desired;
        codecCtx->rc_max_rate = desired;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Impl::PrepareFrame — swscale + metadata (shared by sync and async paths)
// ─────────────────────────────────────────────────────────────────────────────

bool H264Encoder::Impl::PrepareFrame(const uint8_t* rgbaData, uint32_t rgbaStride) {
    // ── RGBA to NV12 conversion ──────────────────────────────────────────
    {
        UZ_PROFILE_SCOPE(H264Encoder_ColorConvert);

        const int makeRet = av_frame_make_writable(frame);
        if (makeRet < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(makeRet, errBuf, sizeof(errBuf));
            UZ_LOG(H264, Error, "av_frame_make_writable failed: {}", errBuf);
            return false;
        }

        const uint8_t* srcSlice[1] = { rgbaData };
        const int srcStride[1] = { static_cast<int>(rgbaStride) };

        sws_scale(swsCtx,
                  srcSlice, srcStride,
                  0, static_cast<int>(config.height),
                  frame->data, frame->linesize);
    }

    // ── Set frame metadata ───────────────────────────────────────────────
    frame->pts = pts++;

    // Force IDR if requested (error recovery / panel reveal).
    if (forceKeyframe.exchange(false, std::memory_order_acq_rel)) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->flags |= AV_FRAME_FLAG_KEY;
    } else {
        frame->pict_type = AV_PICTURE_TYPE_NONE;
        frame->flags &= ~AV_FRAME_FLAG_KEY;
    }

    // Apply dynamic bitrate changes before encoding.
    ApplyBitrateIfChanged();

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Impl::EncodeInto — encode the current NV12 frame into an output buffer
// ─────────────────────────────────────────────────────────────────────────────

bool H264Encoder::Impl::EncodeInto(std::vector<uint8_t>& output) {
    auto* ctx = codecCtx;

    // ── Send frame to encoder ────────────────────────────────────────────
    {
        UZ_PROFILE_SCOPE(H264Encoder_Send);

        const int sendRet = avcodec_send_frame(ctx, frame);
        if (sendRet < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(sendRet, errBuf, sizeof(errBuf));
            UZ_LOG(H264, Error, "avcodec_send_frame failed: {}", errBuf);
            return false;
        }
    }

    // ── Receive encoded packets ──────────────────────────────────────────
    // With max_b_frames=0, the encoder typically produces one packet per
    // input frame, but we drain all available packets into a single output
    // buffer to handle any encoder-internal buffering.
    output.clear();

    {
        UZ_PROFILE_SCOPE(H264Encoder_Receive);

        while (true) {
            const int recvRet = avcodec_receive_packet(ctx, packet);
            if (recvRet == AVERROR(EAGAIN)) {
                break;
            }
            if (recvRet == AVERROR_EOF) {
                break;
            }
            if (recvRet < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(recvRet, errBuf, sizeof(errBuf));
                UZ_LOG(H264, Error, "avcodec_receive_packet failed: {}", errBuf);
                return false;
            }

            const size_t prevSize = output.size();
            output.resize(prevSize + static_cast<size_t>(packet->size));
            std::memcpy(output.data() + prevSize,
                        packet->data,
                        static_cast<size_t>(packet->size));

            av_packet_unref(packet);
        }
    }

    return !output.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// EncodeFrame (synchronous — original API, unchanged behavior)
// ─────────────────────────────────────────────────────────────────────────────

bool H264Encoder::EncodeFrame(const uint8_t* rgbaData, uint32_t rgbaStride,
                               const uint8_t** outData, uint32_t* outSize) {
    UZ_PROFILE_SCOPE(H264Encoder_EncodeFrame);

    if (outData) *outData = nullptr;
    if (outSize) *outSize = 0;

    if (!impl_ || !rgbaData || !outData || !outSize) {
        return false;
    }

    auto* state = impl_;

    if (!state->PrepareFrame(rgbaData, rgbaStride)) {
        return false;
    }

    if (!state->EncodeInto(state->outputBuffer)) {
        return false;
    }

    *outData = state->outputBuffer.data();
    *outSize = static_cast<uint32_t>(state->outputBuffer.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Impl::EncoderThreadMain — background encoding loop
// ─────────────────────────────────────────────────────────────────────────────

void H264Encoder::Impl::EncoderThreadMain() {
    UZ_LOG(H264, Info, "Encoder thread started");

    // Local buffer to hold input data while encoding — avoids holding the
    // mutex during the expensive swscale + avcodec work.
    std::vector<uint8_t> localInput;
    uint32_t localStride = 0;

    while (true) {
        // Wait for new input or exit signal.
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] { return inputReady || threadShouldExit; });

            if (threadShouldExit && !inputReady) {
                break;
            }

            // Take ownership of the staging buffer by swapping — releases
            // the lock quickly so the render thread can submit the next frame.
            localInput.swap(inputStaging);
            localStride = inputStagingStride;
            inputReady = false;
        }

        // Do the heavy work OUTSIDE the lock.
        {
            UZ_PROFILE_SCOPE(H264Encoder_AsyncEncode);

            if (!PrepareFrame(localInput.data(), localStride)) {
                continue;
            }

            if (!EncodeInto(pendingOutput)) {
                // Encoder buffered the frame (rare with max_b_frames=0).
                continue;
            }
        }

        // Publish the result — swap pendingOutput into completedOutput.
        {
            std::lock_guard<std::mutex> lock(mutex);
            completedOutput.swap(pendingOutput);
            completedOutputSize = static_cast<uint32_t>(completedOutput.size());
            outputReady = true;
        }
    }

    UZ_LOG(H264, Info, "Encoder thread exiting");
}

// ─────────────────────────────────────────────────────────────────────────────
// SubmitFrameAsync
// ─────────────────────────────────────────────────────────────────────────────

void H264Encoder::SubmitFrameAsync(const uint8_t* rgbaData, uint32_t rgbaStride) {
    UZ_PROFILE_SCOPE(H264Encoder_SubmitFrameAsync);

    if (!impl_ || !rgbaData) return;

    auto* state = impl_;
    const uint32_t width  = state->config.width;
    const uint32_t height = state->config.height;
    const size_t frameBytes = static_cast<size_t>(rgbaStride) * height;

    {
        std::lock_guard<std::mutex> lock(state->mutex);

        // If the encoder thread hasn't consumed the previous frame yet,
        // drop this one — the encoder cannot keep up with the render rate.
        if (state->inputReady) {
            UZ_LOG(H264, Warning, "Async encoder busy — dropping frame "
                   "(encoder can't keep up at current resolution/bitrate)");
            return;
        }

        // Copy RGBA data into the staging buffer. Pre-allocated to avoid
        // reallocation after the first frame.
        state->inputStaging.resize(frameBytes);
        std::memcpy(state->inputStaging.data(), rgbaData, frameBytes);
        state->inputStagingStride = rgbaStride;
        state->inputReady = true;
    }

    // Wake the encoder thread.
    state->cv.notify_one();
}

// ─────────────────────────────────────────────────────────────────────────────
// GetEncodedFrame
// ─────────────────────────────────────────────────────────────────────────────

bool H264Encoder::GetEncodedFrame(const uint8_t** outData, uint32_t* outSize) {
    if (outData) *outData = nullptr;
    if (outSize) *outSize = 0;

    if (!impl_ || !outData || !outSize) return false;

    auto* state = impl_;

    std::lock_guard<std::mutex> lock(state->mutex);

    if (!state->outputReady) {
        return false;
    }

    *outData = state->completedOutput.data();
    *outSize = state->completedOutputSize;
    state->outputReady = false;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RequestKeyframe
// ─────────────────────────────────────────────────────────────────────────────

void H264Encoder::RequestKeyframe() {
    if (impl_) {
        impl_->forceKeyframe = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SetBitrate
// ─────────────────────────────────────────────────────────────────────────────

void H264Encoder::SetBitrate(uint32_t kbps) {
    if (!impl_) return;

    const int64_t newBitrate = static_cast<int64_t>(kbps) * 1000;
    if (newBitrate == impl_->targetBitrate.load(std::memory_order_relaxed)) return;

    // Store the new target atomically. The actual codec context update
    // happens on the thread that next calls EncodeFrame() or on the
    // encoder thread in the async path — both check this value before
    // encoding and apply it to the codec context.
    impl_->targetBitrate.store(newBitrate, std::memory_order_relaxed);

    UZ_LOG(H264, Debug, "Bitrate updated to {} kbps", kbps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

bool H264Encoder::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

const char* H264Encoder::GetEncoderName() const noexcept {
    return impl_ ? impl_->encoderName : "none";
}

bool H264Encoder::GetExtradata(const uint8_t** outData, uint32_t* outSize) const noexcept {
    if (!impl_ || !impl_->codecCtx || !outData || !outSize) {
        if (outData) *outData = nullptr;
        if (outSize) *outSize = 0;
        return false;
    }

    if (impl_->codecCtx->extradata && impl_->codecCtx->extradata_size > 0) {
        *outData = impl_->codecCtx->extradata;
        *outSize = static_cast<uint32_t>(impl_->codecCtx->extradata_size);
        return true;
    }

    *outData = nullptr;
    *outSize = 0;
    return false;
}

} // namespace uz

#else // !UZ_HAS_FFMPEG && !_WIN32

// FFmpeg is vendored only with Windows binaries; on other platforms the
// encoder reports itself unavailable. Callers already degrade gracefully
// (runtime.cpp: "H.264 encoder not available — frame streaming disabled").
namespace uz {

UZ_DEFINE_LOG_CATEGORY(H264, Info);

struct H264Encoder::Impl {};

H264Encoder::H264Encoder() = default;
H264Encoder::~H264Encoder() = default;
H264Encoder::H264Encoder(H264Encoder&&) noexcept = default;
H264Encoder& H264Encoder::operator=(H264Encoder&&) noexcept = default;

bool H264Encoder::Initialize(const Config& config) {
    (void)config;
    UZ_LOG(H264, Warning,
           "H264Encoder: FFmpeg unavailable on this platform — frame streaming disabled");
    return false;
}

void H264Encoder::Shutdown() {}

bool H264Encoder::EncodeFrame(const uint8_t* rgbaData, uint32_t rgbaStride,
                              const uint8_t** outData, uint32_t* outSize) {
    (void)rgbaData; (void)rgbaStride;
    if (outData) *outData = nullptr;
    if (outSize) *outSize = 0;
    return false;
}

void H264Encoder::SubmitFrameAsync(const uint8_t* rgbaData, uint32_t rgbaStride) {
    (void)rgbaData; (void)rgbaStride;
}

bool H264Encoder::GetEncodedFrame(const uint8_t** outData, uint32_t* outSize) {
    if (outData) *outData = nullptr;
    if (outSize) *outSize = 0;
    return false;
}

void H264Encoder::RequestKeyframe() {}
void H264Encoder::SetBitrate(uint32_t kbps) { (void)kbps; }
bool H264Encoder::IsInitialized() const noexcept { return false; }
const char* H264Encoder::GetEncoderName() const noexcept { return "unavailable"; }

bool H264Encoder::GetExtradata(const uint8_t** outData, uint32_t* outSize) const noexcept {
    if (outData) *outData = nullptr;
    if (outSize) *outSize = 0;
    return false;
}

} // namespace uz

#endif // UZ_HAS_FFMPEG || _WIN32
