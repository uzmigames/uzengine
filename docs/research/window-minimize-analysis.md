# UE5 Window Minimization & Focus Loss Handling - Architecture Analysis

**Source**: W:\UE_5.6_Source (Unreal Engine 5.6)

## Overview

UE5 uses a **focus-aware throttling strategy** rather than explicit window state querying per-frame. The pattern involves:

1. **Detection**: Windows messages (WM_ACTIVATE, WM_SHOWWINDOW, WM_ACTIVATEAPP) are deferred to the game thread
2. **State Tracking**: `FApp::HasFocus()` callback (set by platform) reports foreground status
3. **Throttling Decision**: Made in `FEngineLoop::ShouldUseIdleMode()` at the frame boundary
4. **Rendering**: Idle mode skips world ticks, rendering, and sleeps the thread
5. **Resume**: Full-speed rendering resumes immediately when focus returns

---

## Pattern 1: Window State Detection

### Detection Mechanism

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\ApplicationCore\Private\Windows\WindowsApplication.cpp`

Windows messages are captured at the WndProc level and immediately deferred to the game thread:

```cpp
// Lines 2451-2463: WM_ACTIVATE and WM_ACTIVATEAPP are deferred, not processed synchronously
case WM_ACTIVATE:
    DeferMessage( CurrentNativeEventWindowPtr, hwnd, msg, wParam, lParam );
    break;

case WM_ACTIVATEAPP:
    // When window activation changes we are not in a modal size loop
    bInModalSizeLoop = false;
    DeferMessage( CurrentNativeEventWindowPtr, hwnd, msg, wParam, lParam );
    break;

case WM_SHOWWINDOW:
    DeferMessage( CurrentNativeEventWindowPtr, hwnd, msg, wParam, lParam );
    break;
```

**Key insight**: Messages are NOT processed in the WndProc. They are stored in `DeferredMessages` array and processed later via `ProcessDeferredEvents()` on the game thread.

### Message Deferring Queue

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\ApplicationCore\Private\Windows\WindowsApplication.cpp` (lines 3561-3572)

```cpp
void FWindowsApplication::DeferMessage( TSharedPtr<FWindowsWindow>& NativeWindow, HWND InHWnd,
    uint32 InMessage, WPARAM InWParam, LPARAM InLParam, int32 MouseX, int32 MouseY, uint32 RawInputFlags )
{
    if( GPumpingMessagesOutsideOfMainLoop && bAllowedToDeferMessageProcessing )
    {
        // During frame, accumulate messages
        DeferredMessages.Add( FDeferredWindowsMessage( NativeWindow, InHWnd, InMessage,
            InWParam, InLParam, MouseX, MouseY, RawInputFlags ) );
    }
    else
    {
        // When not in a game loop, process immediately
        ProcessDeferredMessage( FDeferredWindowsMessage( ... ) );
    }
}
```

The deferred messages are batched and processed at frame start in `ProcessDeferredEvents()`.

---

## Pattern 2: Focus State Reporting

### Platform-Specific Focus Callback

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\ApplicationCore\Private\Windows\WindowsPlatformApplicationMisc.cpp` (lines 34-100)

```cpp
void FWindowsPlatformApplicationMisc::PreInit()
{
    // Register the focus query function at engine startup
    FApp::SetHasFocusFunction(&FWindowsPlatformApplicationMisc::IsThisApplicationForeground);
}

bool FWindowsPlatformApplicationMisc::IsThisApplicationForeground()
{
    uint32 ForegroundProcess;
    ::GetWindowThreadProcessId(GetForegroundWindow(), (::DWORD *)&ForegroundProcess);
    return (ForegroundProcess == GetCurrentProcessId());
}
```

**Why not check window minimization directly?**
- UE5 treats "minimized" and "not foreground" as equivalent for throttling purposes
- The key check is: *Is the application process the foreground process?*
- This is simpler than checking per-window state and handles all scenarios (minimize, alt-tab, other window on top, etc.)

### Focus Query at Frame Boundary

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Private\Misc\App.cpp` (lines 369-391)

```cpp
bool FApp::HasFocus()
{
    if (FApp::IsBenchmarking())
        return true;  // Always render in benchmark mode

    if (FApp::UseVRFocus())
        return FApp::HasVRFocus();  // VR has special handling

    bool bHasFocus = true;  // Conservative default

    // Call the platform function (SetHasFocusFunction registered it)
    return HasFocusFunction ? HasFocusFunction() : bHasFocus;
}
```

---

## Pattern 3: Idle Mode Decision & Frame Throttling

### The Idle Mode Gate

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Launch\Private\LaunchEngineLoop.cpp` (lines 5078-5112)

```cpp
bool FEngineLoop::ShouldUseIdleMode() const
{
    bool bIdleMode = false;

    // CHECK 1: Is this a game? (not editor, not dedicated server)
    // CHECK 2: Does the platform support windowed mode?
    // CHECK 3: Is the cvar t.IdleWhenNotForeground enabled? (default: ON)
    // CHECK 4: Does the app NOT have focus?
    if (FApp::IsGame()
        && FPlatformProperties::SupportsWindowedMode()
        && CVarIdleWhenNotForeground->GetValueOnGameThread()
        && !FApp::HasFocus())  // <-- The critical check
    {
        bIdleMode = true;
    }

    // Special case: Embedded apps can signal "not awake for ticking"
#if BUILD_EMBEDDED_APP
    if (FEmbeddedCommunication::IsAwakeForTicking() == false)
        bIdleMode = true;
#endif

    // BUT: If any world has unloaded levels, don't idle (need to stream)
    if (bIdleMode)
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (!Context.World()->AreAlwaysLoadedLevelsLoaded())
            {
                bIdleMode = false;
                break;
            }
        }
    }

    return bIdleMode;
}
```

**Called from**: Line 5557 in main loop
```cpp
bIdleMode = ShouldUseIdleMode();
```

### What Changes When `bIdleMode = true`

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\GameEngine.cpp` (lines 1735-1990)

When `Tick(DeltaSeconds, true)` is called:

1. **World Ticks Skipped** (line 1803):
   ```cpp
   if (!bIdleMode)
   {
       // Only tick media framework if NOT in idle mode
   }
   ```

2. **Rendering Skipped** (line 1960, 1989):
   ```cpp
   if (GameViewport != NULL && !bIdleMode)
   {
       // Only update viewport and render
   }

   const bool bIsRenderingScene = !bIdleMode && !IsRunningDedicatedServer()
       && !IsRunningCommandlet() && FEmbeddedCommunication::IsAwakeForRendering();
   ```

3. **State Updates Skipped** (line 1870):
   ```cpp
   if (!bIdleMode)
   {
       // Only do detailed level streaming work
   }
   ```

---

## Pattern 4: Window Minimize State Queries

### Direct Window Minimization Check

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\ApplicationCore\Private\Windows\WindowsWindow.cpp` (lines 891-900)

```cpp
bool FWindowsWindow::IsMinimized() const
{
    return !!::IsIconic(HWnd);  // Windows API: Is the window iconic (minimized)?
}

bool FWindowsWindow::IsVisible() const
{
    return bIsVisible;  // Tracks ShowWindow(SW_SHOW) vs Hide()
}
```

These are **reactive queries**, used for specific rendering decisions, not the main idle mode gate.

### Slate Layer Usage

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Slate\Private\Framework\Application\SlateApplication.cpp`

Slate checks window state to decide if rendering can proceed:

```cpp
// Line 1077: Skip rendering entirely if window is not visible or minimized
if ( !Window->IsVisible() || Window->IsWindowMinimized())
    continue;  // Don't render this window

// Line 1159: Only render to screen if window is visible and NOT minimized
if (bRenderOffScreen || (WindowToDraw->IsVisible() && (!WindowToDraw->IsWindowMinimized() || FApp::UseVRFocus())) )
{
    // Draw window
}

// Line 1263: Prepass visibility check
const bool bIsWindowVisible = WindowToPrepass->IsVisible() && !WindowToPrepass->IsWindowMinimized();
```

---

## Pattern 5: Swapchain & Minimize Resilience

### Present Failure Handling

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Private\RHICommandList.cpp` (lines 1906-1920)

```cpp
void FRHICommandListImmediate::EndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync)
{
    if (IsImmediate())
    {
        // Call RHI backend directly
        GetContext().RHIEndDrawingViewport(Viewport, bPresent, bLockToVsync);
    }
    else
    {
        // Enqueue command
        ALLOC_COMMAND(FRHICommandEndDrawingViewport)(Viewport, bPresent, bLockToVsync);
    }
}
```

The DX12 backend handles `DXGI_ERROR_DEVICE_REMOVED` from minimized windows gracefully (lost device recovery).

---

## Pattern 6: Message Pump During Idle

### Active Message Processing

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\ApplicationCore\Private\Windows\WindowsApplication.cpp` (lines 3574-3603)

```cpp
void FWindowsApplication::PumpMessages( const float TimeDelta )
{
    MSG Message;

    // Standard Windows message handling - always pumped
    while(PeekMessage(&Message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessage(&Message);  // Routes to WndProc -> DeferMessage
    }
}

void FWindowsApplication::ProcessDeferredEvents( const float TimeDelta )
{
    TArray<FDeferredWindowsMessage> EventsToProcess( DeferredMessages );
    DeferredMessages.Empty();

    for( int32 MessageIndex = 0; MessageIndex < EventsToProcess.Num(); ++MessageIndex )
    {
        ProcessDeferredMessage( EventsToProcess[MessageIndex] );
    }
    // ...
}
```

**Key**: Message pump runs **every frame**, even when idle. This ensures:
- Focus changes are detected quickly
- Window events (move, resize) are processed
- The app can wake up when focus returns

---

## Pattern 7: Sleep During Idle

### CPU Yield When Unfocused

**Context**: When `bIdleMode = true`, the game thread doesn't render. The natural sleep happens because:

1. **No rendering work** → RHI has nothing to do
2. **Game logic skipped** → No world ticks, no physics, no gameplay
3. **Threading blocks** → Thread waits on synchronization primitives (event objects, mutexes)

This is implicit: UE5 doesn't call `Sleep()` directly. Instead, the lack of work naturally yields the CPU.

**Note**: The console variable `t.IdleWhenNotForeground` controls this behavior:
- Default: `1` (ON) — idle when unfocused
- Can be set to `0` to disable throttling

---

## Pattern 8: Resume Full-Speed Rendering

### Immediate Wake-Up Mechanism

The focus state is checked **every frame** (line 5557 in LaunchEngineLoop.cpp):

```cpp
bIdleMode = ShouldUseIdleMode();
```

When focus returns:
1. `GetForegroundWindow()` changes
2. `FApp::HasFocus()` returns `true` on the next check
3. `bIdleMode` becomes `false`
4. **Next frame**: Full rendering resumes
5. **Latency**: 1 frame (16-33ms at typical frame rates)

No explicit "wake" event needed — the frame loop polls focus continuously.

---

## Anti-Patterns & Lessons

### What UzEngine Should NOT Do

1. **Synchronous window state checks in WndProc**
   - UE5 defers ALL messages to the game thread
   - Never block the WndProc with game logic
   - ❌ BAD: `GetForegroundWindow()` in WndProc
   - ✅ GOOD: Queue the activation event, check focus on game thread

2. **Sleeping with a fixed duration**
   - UE5 doesn't call `Sleep(16)` when idle
   - Instead, the lack of rendering work naturally blocks the thread
   - ❌ BAD: `Sleep(16)` every idle frame (poor responsiveness)
   - ✅ GOOD: Skip rendering, let OS scheduler idle the thread

3. **Checking `IsMinimized()` every frame for the main idle decision**
   - Too reactive; doesn't account for full-screen exclusive mode, VR focus, etc.
   - ❌ BAD: `if (Window->IsMinimized()) { IdleMode = true; }`
   - ✅ GOOD: `if (!App->HasFocus()) { IdleMode = true; }`

4. **Not checking focus in embedded contexts**
   - UE5 has special handling for embedded apps via `FEmbeddedCommunication`
   - Supports N-API, Electron, webviews with external frame control
   - ❌ BAD: Hardcoded `GetForegroundWindow()` for all platforms
   - ✅ GOOD: Abstract the focus callback

5. **Rendering a 0x0 swapchain**
   - When window is minimized, don't call Present
   - DX12 returns `DXGI_ERROR_DEVICE_REMOVED` on minimized swapchain
   - ❌ BAD: Always call Present, even if window is 0x0
   - ✅ GOOD: Skip rendering entirely (implicit via idle mode)

---

## Summary: Architectural Pattern for UzEngine

### High-Level Flow

```
┌─ Frame Start
│
├─ PumpMessages()           → WndProc defers WM_ACTIVATE, etc.
├─ ProcessDeferredEvents()  → Slate processes activation messages
├─ ShouldUseIdleMode()      → FApp::HasFocus() check
│   ├─ if (ForegroundProcess == ThisProcess)  → Has focus
│   ├─ else  → No focus, set bIdleMode = true
│   └─ return bIdleMode
│
├─ if (!bIdleMode)
│   ├─ Tick world, render scene, present
│   └─ Full-speed game loop
│ else
│   ├─ Skip all game logic
│   ├─ Skip rendering
│   └─ Thread naturally idles (no work = no CPU)
│
└─ End Frame
```

### Data Structures Required

1. **Window state**:
   - `bool bIsVisible` — Last ShowWindow state
   - `HWND HWnd` — Window handle

2. **Focus tracking**:
   - `bool (*HasFocusFunction)()` — Platform callback
   - Set at startup via `SetHasFocusFunction()`

3. **Deferred message queue**:
   - `TArray<FDeferredWindowsMessage> DeferredMessages`
   - Processed once per frame before idle check

4. **Console variables**:
   - `t.IdleWhenNotForeground` — Enable/disable throttling
   - `t.UseFixedTimeStep` — Alternative: use fixed delta time when idle

---

## Implementation Checklist for UzEngine

- [ ] Implement `uz::GetAppForegroundStatus()` calling `GetForegroundWindow()` + `GetCurrentProcessId()`
- [ ] Create `uz::SetFocusCallback()` to abstract platform-specific focus detection
- [ ] Add deferred message queue in window system (don't process WM_ACTIVATE in WndProc)
- [ ] Implement `FEngineLoop::ShouldThrottle()` checked every frame (not just on message)
- [ ] Skip rendering when `bThrottleRendering = true`
- [ ] PumpMessages() runs every frame (even when idle) to detect focus changes
- [ ] No explicit `Sleep()` — rely on lack of rendering work to idle thread
- [ ] For N-API: hook `IsAwakeForTicking()` from `FEmbeddedCommunication` pattern
- [ ] Test: Alt-tab minimizes viewport, then resumes smoothly
- [ ] Test: 0x0 swapchain (Windows minimized) doesn't crash Present()

---

## File Reference Summary

| File | Purpose |
|------|---------|
| `WindowsApplication.cpp:2205-2463` | WM_SHOWWINDOW, WM_ACTIVATE, WM_ACTIVATEAPP handlers |
| `WindowsApplication.cpp:3561-3603` | DeferMessage(), ProcessDeferredEvents() |
| `WindowsApplication.cpp:3574-3584` | PumpMessages() — message loop |
| `WindowsPlatformApplicationMisc.cpp:34-100` | IsThisApplicationForeground() implementation |
| `App.cpp:369-391` | HasFocus() dispatch to platform callback |
| `LaunchEngineLoop.cpp:5078-5112` | ShouldUseIdleMode() decision gate |
| `GameEngine.cpp:1735-1990` | Tick(DeltaSeconds, bIdleMode) — conditional logic |
| `WindowsWindow.cpp:891-900` | IsMinimized(), IsVisible() queries |
| `SlateApplication.cpp:1077-1263` | Window visibility checks for rendering |

