# VSCode Stream Viewer Extension Specification

This specification defines the requirements for the UzEngine VSCode extension that connects to the C++ streaming server and displays live GPU-rendered content inside a VSCode webview panel using Media Source Extensions (MSE).

## ADDED Requirements

### Requirement: Extension Command Registration
The system MUST register a VSCode command "UzEngine: Open Stream Viewer" that opens a webview panel for stream playback.

#### Scenario: Command activation
Given the extension is installed and active
When the user executes "UzEngine: Open Stream Viewer" from the command palette
Then a new webview panel opens in the active editor column
And the panel title is "UzEngine Stream"

#### Scenario: Singleton panel behavior
Given a stream viewer panel is already open
When the user executes the command again
Then the existing panel is revealed (focused) instead of creating a new one

### Requirement: WebSocket Connection to Stream Server
The system MUST establish a WebSocket connection from the webview to the C++ streaming server at a configurable URL.

#### Scenario: Successful connection
Given the C++ streaming server is running on ws://localhost:9002
When the webview initializes
Then a WebSocket connection is established with binaryType='arraybuffer'
And the status indicator shows "Connected" with a green dot

#### Scenario: Connection failure with retry
Given the C++ streaming server is not running
When the webview attempts to connect
Then the status shows "Disconnected" with a red dot
And reconnection is attempted with exponential backoff (1s, 2s, 4s... max 10s)

#### Scenario: Configurable server URL
Given the user has set uzengine.serverUrl in VSCode settings
When the webview initializes
Then the WebSocket connects to the configured URL instead of the default

### Requirement: MSE Video Playback Pipeline
The system MUST use Media Source Extensions to decode and display the fMP4 H.264 stream received via WebSocket.

#### Scenario: Initialization segment handling
Given the WebSocket connection is established
When the first binary message is received
Then a MediaSource is created and attached to the video element via blob URL
And a SourceBuffer is added with codec 'video/mp4; codecs="avc1.42E01F"' in sequence mode
And the first message (init segment) is appended to the SourceBuffer

#### Scenario: Media segment continuous playback
Given the init segment has been appended
When subsequent binary messages arrive
Then each message is appended to the SourceBuffer as an fMP4 media segment
And the video element plays the decoded frames in real-time
And the video stays near the live edge (within 0.5 seconds of latest data)

#### Scenario: Segment queue during SourceBuffer updates
Given the SourceBuffer is currently updating
When a new binary message arrives
Then the segment is queued in memory
And on the next updateend event, the queued segment is appended

### Requirement: Memory Management
The system MUST prevent unbounded memory growth by trimming the SourceBuffer and limiting the segment queue.

#### Scenario: Buffer trimming
Given the video has been playing for more than 10 seconds
When a new segment is appended
Then buffered data older than 10 seconds before currentTime is removed
And the SourceBuffer size stays bounded

#### Scenario: Queue overflow protection
Given the segment queue exceeds 30 pending segments
When a new segment arrives
Then the oldest queued segments are dropped to maintain the limit
And a warning is logged

### Requirement: Reconnection Resilience
The system MUST automatically reconnect and resume playback when the connection is lost and restored.

#### Scenario: Server restart recovery
Given the streaming server is restarted
When the WebSocket detects disconnection
Then the status shows "Reconnecting..."
And the existing MediaSource is closed and a new one is created
And after reconnection, the init segment is received and playback resumes

### Requirement: Content Security Policy
The system MUST configure a webview CSP that permits WebSocket connections and blob media sources while maintaining security.

#### Scenario: CSP allows streaming requirements
Given the webview HTML is generated
When the CSP meta tag is set
Then connect-src permits ws://localhost:* for WebSocket
And media-src permits blob: for MediaSource object URLs
And script-src uses nonce-based restriction
And default-src is 'none' for security
