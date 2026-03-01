# Network Specification - WebSocket Streaming Server

This specification defines the requirements for the UzEngine WebSocket streaming server, responsible for delivering fMP4 video segments to connected clients in real-time.

## ADDED Requirements

### Requirement: WebSocket Server Lifecycle
The system MUST provide a WebSocket server that starts on a configurable port and supports multiple concurrent client connections.

#### Scenario: Server startup
Given the streaming pipeline is initialized
When StreamServer::start() is called with port 9002
Then the WebSocket server begins listening on port 9002
And a log message "WebSocket server listening on port 9002" is emitted

#### Scenario: Server shutdown
Given the server has active client connections
When StreamServer::stop() is called
Then all client connections are closed gracefully
And the server stops listening
And all server resources are freed

### Requirement: Client Connection Protocol
The system MUST send the fMP4 initialization segment as the first binary WebSocket message to each newly connected client, followed by a continuous stream of fMP4 media segments.

#### Scenario: New client receives init segment
Given a client connects to the WebSocket server
When the connection is established
Then the cached fMP4 init segment is sent as the first binary message
And subsequent fMP4 media segments are broadcast to the client

#### Scenario: Client disconnect handling
Given a connected client disconnects
When the disconnection event fires
Then the client is removed from the broadcast list
And no error occurs when broadcasting to remaining clients
And a log message notes the disconnection

### Requirement: Binary Frame Broadcasting
The system MUST broadcast each fMP4 media segment as a single complete binary WebSocket message to all connected clients.

#### Scenario: Frame broadcast to multiple clients
Given two clients are connected to the server
When broadcastFrame() is called with an fMP4 media segment
Then both clients receive the segment as a binary WebSocket message
And each message contains exactly one complete moof+mdat pair
