## 1. Extension Scaffold
- [ ] 1.1 Create vscode-uzengine/ directory with package.json, tsconfig.json, .vscodeignore
- [ ] 1.2 Configure package.json: extension name, activation events, command contributions
- [ ] 1.3 Add VSCode settings contribution for uzengine.serverUrl (default ws://localhost:9002)
- [ ] 1.4 Create .vscode/launch.json for Extension Development Host debugging
- [ ] 1.5 Set up webpack.config.js for bundling webview scripts
- [ ] 1.6 Install dev dependencies: @types/vscode, typescript, webpack, ts-loader

## 2. Extension Core
- [ ] 2.1 Create src/extension.ts: register command "uzengine.openStreamViewer", activate/deactivate
- [ ] 2.2 Create src/stream-panel.ts: WebviewPanel manager with createOrShow() singleton pattern
- [ ] 2.3 Configure WebviewPanel with enableScripts=true and retainContextWhenHidden=true
- [ ] 2.4 Implement getWebviewContent(): generate HTML with CSP, video tag, script reference
- [ ] 2.5 Set Content Security Policy: connect-src ws://localhost:*; media-src blob:; script-src nonce

## 3. Webview HTML/CSS
- [ ] 3.1 Create src/webview/index.html template with dark theme, video element, status bar
- [ ] 3.2 Create src/webview/styles.css: flexbox layout, status indicators (green/red/yellow dots)
- [ ] 3.3 Status bar shows: connection status, server URL, FPS info

## 4. MSE Stream Player
- [ ] 4.1 Create src/webview/stream-viewer.ts with StreamViewer class
- [ ] 4.2 Implement WebSocket connection with binaryType='arraybuffer'
- [ ] 4.3 Implement MediaSource creation and URL.createObjectURL binding to video element
- [ ] 4.4 On sourceopen: add SourceBuffer with codec 'video/mp4; codecs="avc1.42E01F"' in sequence mode
- [ ] 4.5 Implement segment handling: first binary message = init segment, subsequent = media segments
- [ ] 4.6 Implement segment queue: buffer segments when SourceBuffer.updating is true
- [ ] 4.7 Implement processQueue(): on updateend, append next queued segment
- [ ] 4.8 Implement buffer trimming: remove buffered data older than 10 seconds
- [ ] 4.9 Implement live edge tracking: periodically set video.currentTime near buffered end

## 5. Connection Management
- [ ] 5.1 Implement reconnection with exponential backoff (1s, 2s, 4s... max 10s)
- [ ] 5.2 On reconnect: reset MediaSource state completely (close old, create new)
- [ ] 5.3 Update status indicator on connect/disconnect/reconnecting
- [ ] 5.4 Read server URL from VSCode configuration (uzengine.serverUrl)
- [ ] 5.5 Handle segment queue overflow: drop oldest segments if queue exceeds 30 items

## 6. Testing
- [ ] 6.1 Verify extension activates and command appears in command palette
- [ ] 6.2 Verify webview panel opens with correct HTML and status bar
- [ ] 6.3 Verify WebSocket connects to running C++ server
- [ ] 6.4 Verify video stream appears in webview within 2 seconds of connection
- [ ] 6.5 Verify FPS overlay is visible in the streamed video
- [ ] 6.6 Verify reconnection works after server restart
- [ ] 6.7 Verify no memory growth after 5 minutes of streaming (buffer trimming works)
- [ ] 6.8 Create standalone HTML test page (outside VSCode) for isolating MSE issues

## 7. Documentation
- [ ] 7.1 Document extension installation and usage instructions
- [ ] 7.2 Document MSE pipeline and codec string requirements
- [ ] 7.3 Document CSP configuration requirements for webview WebSocket
