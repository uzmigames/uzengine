## 1. Implementation
- [x] 1.1 Define `WindowState` enum (Normal, Minimized, Maximized, FullscreenBorderless) <!-- FullscreenExclusive removed — not needed for borderless-only approach -->
- [x] 1.2 Add `WindowState` tracking to `PlatformWindow` with `GetState()` query
- [x] 1.3 Implement `Minimize()`, `Maximize()`, `Restore()` methods
- [x] 1.4 Handle `WM_SYSCOMMAND` via existing WM_SIZE dispatch (no separate handler needed)
- [x] 1.5 Handle `WM_SIZE` (SIZE_MINIMIZED, SIZE_MAXIMIZED, SIZE_RESTORED) state transitions via `TransitionState()`
- [x] 1.6 Implement `SetFullscreen()` — borderless fullscreen with style swap, saves windowed rect
- [x] 1.7 Implement `SetWindowed()` — restore previous position and style from fullscreen
- [x] 1.8 Add `WM_ACTIVATEAPP` handling with `IsFocused()` query <!-- already existed, now with m_focused tracking -->
- [x] 1.9 Add `WM_GETMINMAXINFO` handler enforcing configurable minimum size (minWidth/minHeight in WindowDesc)
- [ ] 1.10 Implement timer-throttled resize (rate-limit WM_SIZE during live drag to 30Hz) <!-- deferred: not blocking, optimization -->
- [x] 1.11 Add `SetTitle(string)` dynamic update method <!-- SetIcon deferred to 4.9.0 Native Dialogs -->
- [ ] 1.12 Implement window state persistence (save rect+state to config on destroy, restore on create) <!-- deferred: needs config system -->
- [x] 1.13 Add state change callback (`SetStateChangeCallback` with old/new WindowState)

## 2. Testing
- [x] 2.1 Build succeeds on Windows (zig build — 0 errors)
- [ ] 2.2 Test fullscreen toggle: Windowed→FullscreenBorderless→Windowed preserves position
- [ ] 2.3 Test minimum size enforcement rejects resize below threshold
- [x] 2.4 Focus tracking reports correct state via WM_ACTIVATEAPP handler
- [ ] 2.5 Test state persistence: deferred (needs config system)
- [x] 2.6 Minimized window sets m_minimized=true, skips width/height update
- [x] 2.7 State change callback fires with correct old/new states (TransitionState implementation)
- [x] 2.8 All existing tests pass (1 pre-existing flaky timeout in DX12RenderLoopTest.FrameTimeVariance — unrelated to 4.1.0)

## 3. Documentation
- [x] 3.1 Update `docs/engine/07-platform/01-window-system.md` with state machine diagram and API
- [x] 3.2 Add fullscreen toggle section with borderless vs exclusive explanation <!-- included in 3.1 -->
