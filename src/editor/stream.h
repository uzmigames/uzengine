// ---------------------------------------------------------------------------
// stream.h -- JPEG streaming infrastructure for the editor viewport.
//
// Single encode thread drains an SPSC ring buffer, rate-limited to
// kStreamTargetFPS, encoding RGBA frames to JPEG and broadcasting via
// WebSocket.  The engine renders freely at its native FPS; only
// kStreamTargetFPS frames/s are actually transmitted to the WebView.
// ---------------------------------------------------------------------------
#pragma once

#include "common/ring_buffer.h"
#include "common/types.h"

#include <atomic>
#include <cstddef>

namespace uz { class WsServer; }

// Ring buffer sizing (single ring, encode thread drains quickly).
constexpr std::size_t kStreamRingSize = 4;

// Maximum frames per second transmitted to the WebView.
// Keeping this at 60 prevents the Chromium WebSocket receive handler from
// being flooded, which was the root cause of the 17 FPS VSCode viewport.
constexpr int kStreamTargetFPS = 60;

// Global ring buffer — render thread pushes, encode thread pops.
extern uz::RingBuffer<uz::FrameData, kStreamRingSize> g_frameRing;

/// Start the JPEG encode thread.  Call once after wsServer.start().
void startEncodeThread(uz::WsServer* server);

/// Signal the encode thread to stop and join it.  Call before shutdown.
void stopEncodeThread();
