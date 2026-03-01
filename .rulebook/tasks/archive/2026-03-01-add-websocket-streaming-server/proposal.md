# Proposal: Add WebSocket Streaming Server and Pipeline Integration

## Why
The WebSocket server is the delivery mechanism that connects the C++ rendering pipeline to the VSCode extension viewer. It serves as the network bridge: when a client (VSCode webview) connects, it receives the fMP4 initialization segment followed by a continuous stream of fMP4 media segments in real-time. This phase also integrates the entire pipeline end-to-end: render thread produces frames, encode thread compresses them, and the network thread broadcasts them to connected clients. A lock-free ring buffer between threads ensures the render loop is never blocked by encoding or network latency.

## What Changes
- Add IXWebSocket library dependency via vcpkg
- Implement WebSocket server (StreamServer class) listening on port 9002
- Cache the fMP4 init segment and send it to each new client on connect
- Broadcast encoded fMP4 media segments to all connected clients as binary WebSocket messages
- Implement lock-free ring buffer (RingBuffer) for frame passing between render and encode threads
- Create the encode thread that consumes frames from the ring buffer, encodes, muxes, and broadcasts
- Wire the complete pipeline: main.cpp orchestrates renderer, encoder, muxer, and server
- Add graceful shutdown with thread joining and resource cleanup

## Impact
- Affected specs: network/spec.md (new), pipeline/spec.md (new)
- Affected code: src/network/ws_server.h, src/network/ws_server.cpp, src/common/ring_buffer.h, src/main.cpp, vcpkg.json, CMakeLists.txt
- Breaking change: NO (new modules + main.cpp integration)
- User benefit: Complete real-time streaming pipeline from GPU render to network delivery
