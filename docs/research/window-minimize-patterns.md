# Window Minimization & Focus Loss Patterns from UE5 5.6

**Analysis Date**: 2026-03-12
**Source**: W:\UE_5.6_Source (Unreal Engine 5.6)
**Scope**: Windows platform, game engine rendering loop

## Quick Answer to Your Questions

### 1. How does UE5 detect window minimized/unfocused state?

**Not via window queries on each frame.** Instead:
- Windows messages `WM_ACTIVATE`, `WM_ACTIVATEAPP`, `WM_SHOWWINDOW` are captured and deferred
- Platform-level focus callback: `GetForegroundWindow() == GetCurrentProcessId()`
- Check happens once per frame in `FEngineLoop::ShouldUseIdleMode()`

**Where**:
- `WindowsPlatformApplicationMisc.cpp:96-100` — `IsThisApplicationForeground()`
- `LaunchEngineLoop.cpp:5080-5087` — Focus check gated by `CVarIdleWhenNotForeground`

### 2. How does it throttle/pause rendering when minimized or unfocused?

**Via an "idle mode" flag passed to Tick()**:
```cpp
bool bIdleMode = ShouldUseIdleMode();  // Checked every frame
GEngine->Tick(DeltaSeconds, bIdleMode);
```

When `bIdleMode = true`:
- World ticks skipped
- Physics skipped
- Rendering skipped
- Assets not streamed (unless unloaded levels need loading)
- Thread naturally idles (no work to do)

**Where**: `GameEngine.cpp:1735-1990` — Conditional logic based on `!bIdleMode`

### 3. How does it handle swapchain when window size is 0x0 (minimized)?

**Never creates a 0x0 swapchain.** Because rendering is skipped entirely when minimized:
- `EndDrawingViewport()` is not called
- `Present()` is not called
- Swapchain stays at previous size
- DXGI_ERROR_DEVICE_REMOVED is handled gracefully by DX12 backend (lost device recovery)

**Where**: `GameEngine.cpp:1960` — `if (GameViewport != NULL && !bIdleMode)`

### 4. How does it resume full-speed rendering when focus returns?

**Polling-based, with 1-frame latency**:
- Focus check happens every frame (line 5557 in LaunchEngineLoop.cpp)
- When `GetForegroundWindow()` changes, `ShouldUseIdleMode()` returns false
- Next frame: `bIdleMode = false`, full rendering resumes
- Message pump runs every frame (even when idle) to service messages

**Immediate behavior**: App is responsive to focus change within 1 frame (~16-33ms)

### 5. What's the tick/message pump behavior during minimize?

**Both continue running**:

| Component | Minimized? | Notes |
|-----------|-----------|-------|
| WndProc | ✅ YES | Still processes Windows messages |
| PumpMessages() | ✅ YES | Called every frame to drain message queue |
| ProcessDeferredEvents() | ✅ YES | Handles focus/activation changes |
| World Tick | ❌ NO | Skipped (bIdleMode=true) |
| Physics | ❌ NO | Skipped |
| Rendering | ❌ NO | Skipped |
| Thread Sleep | ❌ NO | Implicit (no work = thread blocks) |

---

## Architectural Patterns

### Pattern A: Deferred Message Queue

```
WndProc receives WM_ACTIVATE
    ↓
if (GPumpingMessagesOutsideOfMainLoop) {
    DeferMessage() → add to DeferredMessages array
} else {
    ProcessDeferredMessage() immediately
}
    ↓
[Game thread processes at frame start]
ProcessDeferredEvents() → handles all queued messages
    ↓
Slate updates window state (focus, visibility, etc.)
```

**Why**: Prevents WndProc from blocking on game logic. Messages are thread-safe on the game thread.

### Pattern B: Focus-aware Throttling

```cpp
// Per-frame check (LaunchEngineLoop.cpp:5080-5087)
bool bIdleMode = false;
if (FApp::IsGame()
    && FPlatformProperties::SupportsWindowedMode()
    && CVarIdleWhenNotForeground->GetValueOnGameThread()
    && !FApp::HasFocus())  // ← Platform callback
{
    bIdleMode = true;
}

// Special cases:
if (bIdleMode && AnyWorldHasUnloadedLevels()) {
    bIdleMode = false;  // Stream in the background
}

return bIdleMode;
```

**Why**: Single gate for all throttling. Works for minimize, alt-tab, fullscreen loss, VR refocus.

### Pattern C: Conditional Game Logic

```cpp
void UGameEngine::Tick(float DeltaSeconds, bool bIdleMode) {
    // Media framework
    if (!bIdleMode) {
        MediaModule->TickPreEngine();
    }

    // World updates
    if (!bIdleMode) {
        for (Context in Worlds) {
            Context.Tick();
        }
    }

    // Rendering
    const bool bIsRenderingScene = !bIdleMode && ...;
    if (bIsRenderingScene) {
        GameViewport->Draw();
    }
}
```

**Why**: Single boolean gates all expensive subsystems. Clear, predictable, testable.

### Pattern D: Message Pump During Idle

```cpp
void FWindowsApplication::PumpMessages(const float TimeDelta) {
    while (PeekMessage(&Message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&Message);
        DispatchMessage(&Message);  // Routes to WndProc
    }
}
// Called EVERY frame, regardless of idle state
```

**Why**: Ensures focus changes are detected immediately, not on next "work" frame.

---

## State Queries vs. Decision Gates

### Query (Reactive, Window-Specific)
```cpp
bool IsMinimized() { return ::IsIconic(HWnd); }
bool IsVisible() { return bIsVisible; }

// Used by Slate for individual window rendering:
if (!Window->IsVisible() || Window->IsWindowMinimized()) {
    continue;  // Skip this window in render loop
}
```

### Gate (Proactive, Application-Wide)
```cpp
bool ShouldUseIdleMode() {
    return !FApp::HasFocus() && CVarIdleWhenNotForeground;
}

// Used by engine loop:
GEngine->Tick(DeltaSeconds, ShouldUseIdleMode());
```

**Lesson**: Use gates for broad throttling decisions. Use queries for fine-grained rendering checks.

---

## Console Variables

| Variable | Type | Default | Purpose |
|----------|------|---------|---------|
| `t.IdleWhenNotForeground` | int32 | 1 | Enable throttling when unfocused |
| `r.ScreenPercentage` | float | 100 | Dynamic resolution (helps on slow frames) |
| `t.MaxFPS` | float | 0 (unlimited) | Cap frame rate |
| `r.VSync` | int32 | 0 | Vsync mode |

For embedded apps (N-API):
- Override via `FEmbeddedCommunication::IsAwakeForTicking()`
- Allows extension to control whether engine should sleep

---

## DX12 Specific: Swapchain Minimization Handling

### What Happens

1. **User minimizes window**
   - `WM_SHOWWINDOW` sent with `SW_MINIMIZE`
   - Deferred to game thread
   - Slate marks window as not visible

2. **Rendering skipped**
   - `bIdleMode = true` (no focus)
   - `GameViewport->Draw()` not called
   - Swapchain never resized to 0x0

3. **Restore**
   - `WM_SHOWWINDOW` sent with `SW_RESTORE`
   - Focus returns
   - `bIdleMode = false`
   - Next frame: Full rendering resumes

### Why No DXGI_ERROR_DEVICE_REMOVED

By design, the rendering is **skipped entirely**, so there's no Present() call on a minimized swapchain. The DX12 backend never encounters the error condition.

If the error *did* occur (e.g., from a stale Present call):
- DX12 backend has device lost recovery logic
- Recreates resources, resubmits work
- Application remains stable

---

## Comparison: Minimize vs. VR vs. Embedded

### Standard Minimize
```cpp
GetForegroundWindow() != ThisProcess
→ !FApp::HasFocus()
→ bIdleMode = true
→ Skip rendering
```

### VR Mode
```cpp
if (FApp::UseVRFocus()) {
    return FApp::HasVRFocus();  // VR device focus, not OS window
}
```

### Embedded / N-API
```cpp
if (FEmbeddedCommunication::IsAwakeForTicking() == false) {
    bIdleMode = true;  // Parent (VSCode) controls when to tick
}
```

**Design**: Single abstraction point (`FApp::HasFocus()`) that platforms override.

---

## Implementation Notes for UzEngine

### What to Copy
1. Deferred message queue pattern (thread safety)
2. Per-frame focus polling (not event-driven)
3. Single `bIdleMode` gate for all throttling
4. Platform callback abstraction
5. Message pump runs every frame

### What NOT to Copy
1. Full Slate UI framework (UzEngine has simple viewport)
2. Complex component hierarchy (GameEngine owns everything)
3. UObject reflection system
4. Async loading / content streaming integration

### What to Adapt
- Focus callback: `uz::GetAppHasFocus()` → wraps `GetForegroundWindow()`
- Idle gate: `uz::EngineLoop::ShouldThrottle()` → same structure
- Deferred messages: Simpler queue for window state changes
- Conditional ticks: Check `!bThrottleRendering` before rendering

---

## References

**All from W:\UE_5.6_Source:**

| File | Lines | Purpose |
|------|-------|---------|
| Engine/Source/Runtime/ApplicationCore/Private/Windows/WindowsApplication.cpp | 2205-2463 | Message handlers |
| Engine/Source/Runtime/ApplicationCore/Private/Windows/WindowsApplication.cpp | 3561-3603 | Defer & process messages |
| Engine/Source/Runtime/ApplicationCore/Private/Windows/WindowsPlatformApplicationMisc.cpp | 34-100 | Focus detection |
| Engine/Source/Runtime/Core/Private/Misc/App.cpp | 369-391 | Focus query dispatch |
| Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp | 5078-5112 | Idle decision |
| Engine/Source/Runtime/Engine/Private/GameEngine.cpp | 1735-1990 | Tick with idle flag |
| Engine/Source/Runtime/ApplicationCore/Private/Windows/WindowsWindow.cpp | 891-900 | Window state queries |
| Engine/Source/Runtime/Slate/Private/Framework/Application/SlateApplication.cpp | 1077-1263 | Rendering visibility checks |

