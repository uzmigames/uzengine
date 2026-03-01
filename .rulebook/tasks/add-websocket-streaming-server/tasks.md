## 1. WebSocket Server
- [ ] 1.1 Add ixwebsocket to vcpkg.json manifest
- [ ] 1.2 Update CMakeLists.txt to find and link ixwebsocket
- [ ] 1.3 Create src/network/ws_server.h with StreamServer class (start, stop, broadcastFrame, setInitSegment)
- [ ] 1.4 Implement server startup on configurable port (default 9002)
- [ ] 1.5 Implement client connection handler: log connect, send cached init segment as first binary message
- [ ] 1.6 Implement client disconnect handler: remove from client set, log disconnect
- [ ] 1.7 Implement broadcastFrame(): send fMP4 media segment as binary message to all connected clients
- [ ] 1.8 Implement graceful stop: close all connections, stop server

## 2. Lock-Free Ring Buffer
- [ ] 2.1 Create src/common/ring_buffer.h with RingBuffer<T> template class
- [ ] 2.2 Implement fixed-capacity circular buffer (3 slots for frame pipelining)
- [ ] 2.3 Implement tryPush(): non-blocking enqueue, returns false if full (frame dropped)
- [ ] 2.4 Implement tryPop(): non-blocking dequeue, returns false if empty
- [ ] 2.5 Use std::atomic for head/tail indices to ensure thread safety without mutexes

## 3. Pipeline Integration
- [ ] 3.1 Update main.cpp: create and initialize renderer, encoder (via factory), muxer, and server
- [ ] 3.2 Generate fMP4 init segment from encoder's codec config, cache in server
- [ ] 3.3 Start encode thread: loop consuming FrameData from ring buffer
- [ ] 3.4 In encode thread: encode frame -> mux to fMP4 media segment -> broadcast via server
- [ ] 3.5 In render loop: after readback, push FrameData to ring buffer (drop if full, log warning)
- [ ] 3.6 Log pipeline status on startup: encoder type, server port, resolution, target FPS
- [ ] 3.7 Implement graceful shutdown: signal threads, join, shutdown server/encoder/renderer

## 4. Testing
- [ ] 4.1 Verify WebSocket server starts and listens on port 9002
- [ ] 4.2 Verify client connection receives init segment as first binary message
- [ ] 4.3 Verify subsequent frames are received as binary messages
- [ ] 4.4 Verify client disconnect does not crash server
- [ ] 4.5 Test ring buffer: push/pop correctness, overflow behavior, concurrent access from two threads
- [ ] 4.6 Verify end-to-end pipeline: triangle renders, encodes, streams via WebSocket
- [ ] 4.7 Verify graceful shutdown: no crashes, no hanging threads, all resources freed
- [ ] 4.8 Connect with wscat or browser test page to verify binary data reception

## 5. Documentation
- [ ] 5.1 Document WebSocket protocol: init segment on connect, binary media segments
- [ ] 5.2 Document threading model and ring buffer design in docs/architecture.md
- [ ] 5.3 Document server configuration (port, etc.)
