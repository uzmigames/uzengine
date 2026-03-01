## 1. WebSocket Server
- [x] 1.1 Add ixwebsocket to vcpkg.json manifest
- [x] 1.2 Update CMakeLists.txt to find and link ixwebsocket
- [x] 1.3 Create src/network/ws_server.h with StreamServer class (start, stop, broadcastFrame, setInitSegment)
- [x] 1.4 Implement server startup on configurable port (default 9002)
- [x] 1.5 Implement client connection handler: log connect, send cached init segment as first binary message
- [x] 1.6 Implement client disconnect handler: remove from client set, log disconnect
- [x] 1.7 Implement broadcastFrame(): send fMP4 media segment as binary message to all connected clients
- [x] 1.8 Implement graceful stop: close all connections, stop server

## 2. Lock-Free Ring Buffer
- [x] 2.1 Create src/common/ring_buffer.h with RingBuffer<T> template class
- [x] 2.2 Implement fixed-capacity circular buffer (3 slots for frame pipelining)
- [x] 2.3 Implement tryPush(): non-blocking enqueue, returns false if full (frame dropped)
- [x] 2.4 Implement tryPop(): non-blocking dequeue, returns false if empty
- [x] 2.5 Use std::atomic for head/tail indices to ensure thread safety without mutexes

## 3. Pipeline Integration
- [x] 3.1 Update main.cpp: create and initialize renderer, encoder (via factory), muxer, and server
- [x] 3.2 Generate fMP4 init segment from encoder's codec config, cache in server
- [x] 3.3 Start encode thread: loop consuming FrameData from ring buffer
- [x] 3.4 In encode thread: encode frame -> mux to fMP4 media segment -> broadcast via server
- [x] 3.5 In render loop: after readback, push FrameData to ring buffer (drop if full, log warning)
- [x] 3.6 Log pipeline status on startup: encoder type, server port, resolution, target FPS
- [x] 3.7 Implement graceful shutdown: signal threads, join, shutdown server/encoder/renderer

## 4. Testing
- [x] 4.1 Verify WebSocket server starts and listens on port 9002
- [x] 4.2 Verify client connection receives init segment as first binary message
- [x] 4.3 Verify subsequent frames are received as binary messages
- [x] 4.4 Verify client disconnect does not crash server
- [x] 4.5 Test ring buffer: push/pop correctness, overflow behavior, concurrent access from two threads
- [x] 4.6 Verify end-to-end pipeline: triangle renders, encodes, streams via WebSocket
- [x] 4.7 Verify graceful shutdown: no crashes, no hanging threads, all resources freed
- [x] 4.8 Connect with wscat or browser test page to verify binary data reception

## 5. Documentation
- [x] 5.1 Document WebSocket protocol: init segment on connect, binary media segments
- [x] 5.2 Document threading model and ring buffer design in docs/architecture.md
- [x] 5.3 Document server configuration (port, etc.)
