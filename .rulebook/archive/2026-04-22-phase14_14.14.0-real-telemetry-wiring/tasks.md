## 1. Implementation

- [x] 1.1 Added `VfxEditorOverlay::UpdateTelemetry(float fps, uint32_t drawCalls, float vramUsedMb, float vramTotalMb)` forwarding to `m_statusBar.SetFps/SetDrawCalls/SetVramUsed`
- [x] 1.2 Added `m_fpsSmoothed = 60.0f` member to `VFXViewportApp` + EMA update in `OnUpdate`: `alpha = dt / (dt + 0.1f)`, `m_fpsSmoothed += (1/dt - m_fpsSmoothed) * alpha` guarded by `dt > 1e-4f`
- [x] 1.3 `OnUpdate` calls `overlay->UpdateTelemetry(m_fpsSmoothed, 0u, 0.0f, 0.0f)` every frame; FPS chip colour tracks the live smoothed value
- [x] 1.4 Removed hardcoded `SetFps(60.0f)` call inside `BuildNiagaraUI`'s status-bar branch; host-driven telemetry wins

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
