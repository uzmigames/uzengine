// ---------------------------------------------------------------------------
// stream.h -- JPEG streaming infrastructure for the editor viewport.
//
// Extracts the dual-threaded JPEG encode pipeline from main.cpp.
// Two encoder threads drain SPSC ring buffers in round-robin,
// encoding RGBA frames to JPEG and broadcasting via WebSocket.
// ---------------------------------------------------------------------------
#pragma once

#include "common/ring_buffer.h"
#include "common/types.h"

#include <atomic>
#include <cstddef>

namespace uz { class WsServer; }

// Ring buffer sizing.
constexpr std::size_t kStreamRingSize = 8;  // power-of-2, absorbs encode jitter

// Global ring buffers — render thread pushes, encode threads pop.
extern uz::RingBuffer<uz::FrameData, kStreamRingSize> g_frameRingA;
extern uz::RingBuffer<uz::FrameData, kStreamRingSize> g_frameRingB;

// Round-robin dispatch toggle (written only by the render thread).
extern bool g_dispatchToggle;

/// Start the dual JPEG encode threads.  Call once after wsServer.start().
void startEncodeThreads(uz::WsServer* server);

/// Signal encode threads to stop and join them. Call before shutdown.
void stopEncodeThreads();
