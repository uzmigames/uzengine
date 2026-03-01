## 1. Implementation

### 1.1 `src/main.cpp` — Encoder
- [x] 1.1.1 Reduce IDR interval from `kTargetFPS` (60) to `kTargetFPS / 2` (30 frames = 0.5s)

### 1.2 `vscode-uzengine/src/stream-panel.ts` — MSE Player
- [x] 1.2.1 Add live-edge sync: 500ms interval that seeks to `buffered.end(last) - 0.05` when drift > 0.2s
- [x] 1.2.2 Add `pendingSegments` queue cap: max 3 entries, drop oldest when exceeded
- [x] 1.2.3 Replace WebSocket-message-based FPS counter with `requestVideoFrameCallback` (fallback to `ontimeupdate`)
- [x] 1.2.4 Clear the live-edge interval inside `disconnect()` to avoid leaks

## 2. Manual Validation
- [ ] 2.1 Open engine in windowed mode + VSCode panel side by side
- [ ] 2.2 Confirm drift between native window and webview is < 200ms after 30s of streaming
- [ ] 2.3 Confirm that closing and reopening the panel recovers playback in < 1s
- [ ] 2.4 Confirm displayed FPS reflects actual playback smoothness (not packet count)

## 3. Documentation
- [x] 3.1 Update relevant comments in `stream-panel.ts` explaining the live-edge sync strategy
