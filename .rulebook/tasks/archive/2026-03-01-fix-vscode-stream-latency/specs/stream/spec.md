## MODIFIED Requirements

### Requirement: IDR Keyframe Interval
The encoder SHALL emit an IDR keyframe every 30 frames (0.5 seconds at 60fps) instead of every 60 frames, minimizing the maximum recovery time after any MSE stall or WebSocket reconnect.

#### Scenario: Fast MSE Recovery After Reconnect
Given the WebSocket disconnects and the MSE pipeline resets
When a new WebSocket connection is established
Then the MSE player SHALL receive an IDR keyframe within 0.5 seconds and resume playback

## ADDED Requirements

### Requirement: Live-Edge Synchronization
The MSE player in the VSCode webview SHALL continuously synchronize playback position to the live edge by checking every 500ms whether the gap between `video.buffered.end(last)` and `video.currentTime` exceeds 200ms, and if so seeking to `buffered.end(last) - 50ms`.

#### Scenario: Drift Correction While Streaming
Given the stream has been playing for more than 10 seconds
When the playback position drifts more than 200ms behind the buffered live edge
Then the player SHALL seek to within 50ms of the live edge within the next 500ms interval

#### Scenario: Interval Cleanup on Disconnect
Given a live-edge sync interval is running
When `disconnect()` is called
Then the interval SHALL be cleared and no seek attempts SHALL occur after disconnection

### Requirement: Pending Segment Queue Cap
The MSE player SHALL cap the `pendingSegments` queue at a maximum of 3 entries. When a new segment arrives and the queue is already at capacity, the oldest segments SHALL be dropped, retaining only the most recent segment to maximize the chance of having a decodable IDR frame.

#### Scenario: Queue Overflow Under Load
Given the SourceBuffer is busy updating and segments are arriving faster than they are consumed
When the pending queue reaches 3 segments and a new segment arrives
Then all but the newest segment SHALL be dropped from the queue

### Requirement: Accurate FPS Display
The toolbar FPS counter SHALL measure actually decoded video frames using `requestVideoFrameCallback` when available, falling back to `video.ontimeupdate` on unsupported browsers, rather than counting incoming WebSocket messages.

#### Scenario: FPS Reflects Decoder Output
Given the stream is playing at 60fps from the engine
When the VSCode webview renders the video
Then the displayed FPS SHALL reflect the number of frames decoded by the video element per second, not the number of WebSocket packets received
