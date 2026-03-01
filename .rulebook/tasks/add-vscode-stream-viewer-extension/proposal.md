# Proposal: Add VSCode Stream Viewer Extension

## Why
This is the second and final part of the PoC: a VSCode extension that connects to the C++ streaming server and displays the live GPU-rendered content inside a VSCode tab. This proves the core concept - that a game engine can be visually integrated into VSCode by streaming its rendered output to a webview panel. The extension uses the browser-native Media Source Extensions (MSE) API within VSCode's Electron-based webview to decode and display the H.264 fMP4 stream in real-time. If this works, we have validated the entire architecture for a full engine integration.

## What Changes
- Create a VSCode extension project (TypeScript) in vscode-uzengine/ directory
- Register a command "UzEngine: Open Stream Viewer" in the command palette
- Create a WebviewPanel with retainContextWhenHidden for persistent stream playback
- Implement WebSocket client in the webview that connects to ws://localhost:9002
- Implement MSE MediaSource + SourceBuffer pipeline to decode fMP4 segments
- Implement segment queue for handling SourceBuffer async updates
- Implement buffer trimming to prevent memory growth (keep last 10s)
- Add connection status indicator (connected/disconnected/reconnecting)
- Implement automatic reconnection with exponential backoff
- Add configurable server URL via VSCode settings (uzengine.serverUrl)
- Set custom Content Security Policy allowing ws://localhost:* and blob: media sources

## Impact
- Affected specs: extension/spec.md (new)
- Affected code: vscode-uzengine/ (entire new directory)
- Breaking change: NO (separate project, new extension)
- User benefit: Live GPU-rendered 3D content viewable inside VSCode
