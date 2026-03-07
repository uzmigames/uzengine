// ---------------------------------------------------------------------------
// stream.cpp -- JPEG streaming infrastructure for the editor viewport.
//
// Single encode thread draining an SPSC ring buffer, rate-limited to
// kStreamTargetFPS (60 FPS).  The engine renders freely at its native FPS
// (244 Hz); only kStreamTargetFPS frames/s are JPEG-encoded and sent over
// WebSocket.
//
// Root cause of the 17 FPS VSCode viewport (fixed here):
//   The old design had TWO encode threads each with an async sendBinary path.
//   IXWebSocket::sendBinary returns almost instantly (it queues internally),
//   so the broadcastBusy flag was cleared ~0.1 ms after each call.  Both
//   threads were flooding the WebSocket at ~200 frames/s.  Chromium's
//   WebSocket onmessage fires on the main thread — 200 calls/s at 400 KB each
//   caused severe main-thread overload in the VSCode WebView, dropping the
//   visible canvas FPS to ~17.
//
//   Fix: timestamp-based rate limiter capped at kStreamTargetFPS.  The encode
//   thread pops frames continuously (drains the ring fast) but only encodes
//   and broadcasts one frame per kMinInterval.
// ---------------------------------------------------------------------------

#include "stream.h"

#include "encoder/jpeg_encoder.h"
#include "network/ws_server.h"

#include <windows.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
uz::RingBuffer<uz::FrameData, kStreamRingSize> g_frameRing;

namespace {

std::atomic<bool> g_encodeRunning{false};
std::thread       g_encodeThread;

// ---------------------------------------------------------------------------
// Encode thread body
//
// Pops RGBA frames from g_frameRing, drops them if too soon (rate limiter),
// otherwise JPEG-encodes and broadcasts over WebSocket.
//
// The rate limiter is a simple timestamp check:
//   elapsed < kMinInterval → drop frame, keep draining the ring.
//   elapsed >= kMinInterval → encode + broadcast, update lastBroadcast.
//
// This guarantees the WS client never receives more than kStreamTargetFPS
// messages/s regardless of how fast the render thread pushes frames.
// ---------------------------------------------------------------------------
void encodeThreadFn(uz::WsServer* server)
{
    uz::JpegEncoder encoder;
    std::vector<uint8_t> jpegBlob;
    std::vector<uint8_t> encodedJpeg;

    // Pre-allocate to reduce reallocs for typical frame sizes.
    jpegBlob.reserve(512 * 1024);
    encodedJpeg.reserve(512 * 1024);

    using Clock = std::chrono::steady_clock;
    static const auto kMinInterval =
        std::chrono::microseconds(1'000'000 / kStreamTargetFPS);

    // Set lastBroadcast in the past so the first frame is sent immediately.
    auto lastBroadcast = Clock::now() - kMinInterval;

    OutputDebugStringA("[UzEngine] EncodeThread started (JPEG 60 FPS mode)\n");

    while (g_encodeRunning.load(std::memory_order_relaxed)) {
        auto frameOpt = g_frameRing.pop();
        if (!frameOpt) {
            // Ring empty — yield to avoid busy-waiting.
            std::this_thread::yield();
            continue;
        }

        // Rate limiter: drop frame if the minimum inter-frame interval has not
        // elapsed yet.  The frame is popped and discarded — this keeps the ring
        // drained so the render thread never stalls on a full buffer.
        auto now = Clock::now();
        if (now - lastBroadcast < kMinInterval) {
            continue;
        }

        const uz::FrameData& frame = *frameOpt;
        if (frame.pixels.empty()) {
            continue;
        }

        // Build WS message: [4-byte viewport_id prefix][JPEG payload]
        // Writing the prefix first avoids an extra allocation + memcpy.
        jpegBlob.clear();
        constexpr uint32_t kViewportId = 1;
        jpegBlob.resize(4);
        std::memcpy(jpegBlob.data(), &kViewportId, 4);

        // Quality 72 at full resolution gives better perceptual quality than
        // quality 85 at half resolution, while keeping payload ~240 KB (was ~400 KB
        // at q85 full-res).  This is the sweet spot for 60 FPS WebSocket streaming.
        constexpr int kJpegQuality = 72;

        encodedJpeg.clear();
        if (!encoder.encode(frame.pixels.data(),
                             static_cast<int>(frame.width),
                             static_cast<int>(frame.height),
                             kJpegQuality,
                             encodedJpeg)) {
            OutputDebugStringA("[UzEngine] JPEG encode failed — dropping frame\n");
            continue;
        }

        jpegBlob.insert(jpegBlob.end(), encodedJpeg.begin(), encodedJpeg.end());

        server->broadcast(jpegBlob);

        // Record timestamp AFTER broadcast so the next interval is measured
        // from delivery, not from encode start. This prevents frame pacing
        // jitter caused by variable encode times (5-8ms).
        lastBroadcast = Clock::now();
    }

    OutputDebugStringA("[UzEngine] EncodeThread stopped\n");
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void startEncodeThread(uz::WsServer* server) {
    g_encodeRunning.store(true, std::memory_order_relaxed);
    g_encodeThread = std::thread(encodeThreadFn, server);
}

void stopEncodeThread() {
    g_encodeRunning.store(false, std::memory_order_relaxed);
    if (g_encodeThread.joinable()) g_encodeThread.join();
}
