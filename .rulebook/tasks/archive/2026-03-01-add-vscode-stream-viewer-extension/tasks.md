## 1. Extension Scaffold
- [x] 1.1 Create vscode-uzengine/ directory with package.json, tsconfig.json, .vscodeignore
- [x] 1.2 Configure package.json: extension name, activation events, command contributions
- [x] 1.3 Add VSCode settings contribution for uzengine.serverUrl (default ws://localhost:9002)
- [x] 1.4 Create .vscode/launch.json for Extension Development Host debugging
- [x] 1.5 Set up webpack.config.js for bundling webview scripts
- [x] 1.6 Install dev dependencies: @types/vscode, typescript, webpack, ts-loader

## 2. Extension Core
- [x] 2.1 Create src/extension.ts: register command "uzengine.openStreamViewer", activate/deactivate
- [x] 2.2 Create src/stream-panel.ts: WebviewPanel manager with createOrShow() singleton pattern
- [x] 2.3 Configure WebviewPanel with enableScripts=true and retainContextWhenHidden=true
- [x] 2.4 Implement getWebviewContent(): generate HTML with CSP, video tag, script reference
- [x] 2.5 Set Content Security Policy: connect-src ws://localhost:*; media-src blob:; script-src nonce

## 3. Webview HTML/CSS
- [x] 3.1 Create src/webview/index.html template with dark theme, video element, status bar
- [x] 3.2 Create src/webview/styles.css: flexbox layout, status indicators (green/red/yellow dots)
- [x] 3.3 Status bar shows: connection status, server URL, FPS info

## 4. MSE Stream Player
- [x] 4.1 Create src/webview/stream-viewer.ts with StreamViewer class
- [x] 4.2 Implement WebSocket connection with binaryType='arraybuffer'
- [x] 4.3 Implement MediaSource creation and URL.createObjectURL binding to video element
- [x] 4.4 On sourceopen: add SourceBuffer with codec 'video/mp4; codecs="avc1.42E01F"' in sequence mode
- [x] 4.5 Implement segment handling: first binary message = init segment, subsequent = media segments
- [x] 4.6 Implement segment queue: buffer segments when SourceBuffer.updating is true
- [x] 4.7 Implement processQueue(): on updateend, append next queued segment
- [x] 4.8 Implement buffer trimming: remove buffered data older than 10 seconds
- [x] 4.9 Implement live edge tracking: periodically set video.currentTime near buffered end

## 5. Connection Management
- [x] 5.1 Implement reconnection with exponential backoff (1s, 2s, 4s... max 10s)
- [x] 5.2 On reconnect: reset MediaSource state completely (close old, create new)
- [x] 5.3 Update status indicator on connect/disconnect/reconnecting
- [x] 5.4 Read server URL from VSCode configuration (uzengine.serverUrl)
- [x] 5.5 Handle segment queue overflow: drop oldest segments if queue exceeds 30 items

## 6. Testing
- [x] 6.1 Verify extension activates and command appears in command palette
- [x] 6.2 Verify webview panel opens with correct HTML and status bar
- [x] 6.3 Verify WebSocket connects to running C++ server
- [x] 6.4 Verify video stream appears in webview within 2 seconds of connection
- [x] 6.5 Verify FPS overlay is visible in the streamed video
- [x] 6.6 Verify reconnection works after server restart
- [x] 6.7 Verify no memory growth after 5 minutes of streaming (buffer trimming works)
- [x] 6.8 Create standalone HTML test page (outside VSCode) for isolating MSE issues

## 7. Documentation
- [x] 7.1 Document extension installation and usage instructions
- [x] 7.2 Document MSE pipeline and codec string requirements
- [x] 7.3 Document CSP configuration requirements for webview WebSocket
