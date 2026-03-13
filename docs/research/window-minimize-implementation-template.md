# UzEngine Window Minimize Implementation Template

Based on UE5 5.6 architectural analysis. Adapt these patterns for UzEngine C/C++ runtime.

---

## 1. Platform Focus Detection Callback

### Header: `runtime/core/platform/focus.h`

```cpp
/// Queries whether the application currently has input focus.
/// Returns true if this process is the foreground window on Windows.
/// Platform-specific; should be set during engine init via SetFocusCallback.
bool GetApplicationFocus();

/// Sets the platform-specific focus query function.
/// Called once at startup by platform layer (WindowsPlatformMisc).
/// Example: SetFocusCallback(&WindowsPlatformMisc::IsThisApplicationForeground)
typedef bool (*FocusCallbackFn)();
void SetFocusCallback(FocusCallbackFn Callback);
```

### Implementation: `runtime/core/platform/windows/focus.cpp`

```cpp
static FocusCallbackFn GFocusCallback = nullptr;

bool GetApplicationFocus() {
    if (!GFocusCallback) {
        return true;  // Conservative default: assume we have focus
    }
    return GFocusCallback();
}

void SetFocusCallback(FocusCallbackFn Callback) {
    GFocusCallback = Callback;
}

// Platform-specific: Windows
namespace WindowsPlatformMisc {
    bool IsThisApplicationForeground() {
        HWND ForegroundWindow = ::GetForegroundWindow();
        if (!ForegroundWindow) {
            return false;
        }

        DWORD ForegroundProcessId = 0;
        ::GetWindowThreadProcessId(ForegroundWindow, &ForegroundProcessId);

        return (ForegroundProcessId == ::GetCurrentProcessId());
    }
}

// Call from Engine::PreInit()
void InitializePlatformFocus() {
#ifdef _WIN32
    SetFocusCallback(&WindowsPlatformMisc::IsThisApplicationForeground);
#endif
    // Mac, Linux implementations here
}
```

---

## 2. Window State & Deferred Messages

### Header: `runtime/platform/windows/window.h`

```cpp
struct FWindowsWindow {
    HWND HWnd = nullptr;
    bool bIsVisible = false;
    float DPIScaleFactor = 1.0f;

    /// Platform API: Check if window is currently iconic (minimized).
    /// This is a reactive query; not used for main idle decision.
    bool IsMinimized() const {
        return ::IsIconic(HWnd);
    }

    /// Platform API: Check if window has been shown (vs. hidden).
    bool IsVisible() const {
        return bIsVisible;
    }

    /// Platform API: Check if window is the foreground window.
    bool IsForegroundWindow() const {
        return ::GetForegroundWindow() == HWnd;
    }
};

/// Deferred Windows message for async processing on game thread.
struct FDeferredWindowsMessage {
    TSharedPtr<FWindowsWindow> Window;
    HWND HWnd;
    uint32 Message;      // WM_*
    WPARAM WParam;
    LPARAM LParam;
    int32 MouseX = 0;    // For mouse messages
    int32 MouseY = 0;
};
```

### Implementation: `runtime/platform/windows/application.cpp`

```cpp
class FWindowsApplication {
private:
    TArray<FDeferredWindowsMessage> DeferredMessages;
    bool bAllowedToDeferMessageProcessing = true;

public:
    /// Defer a Windows message to the game thread.
    /// Called from WndProc (the static callback from Windows API).
    void DeferMessage(TSharedPtr<FWindowsWindow>& NativeWindow, HWND HWnd,
                      uint32 Message, WPARAM WParam, LPARAM LParam) {
        // If we're already on the game thread (e.g., during startup),
        // process immediately. Otherwise, queue for later.
        if (GPumpingMessagesOutsideOfMainLoop && bAllowedToDeferMessageProcessing) {
            DeferredMessages.Add(FDeferredWindowsMessage{
                NativeWindow, HWnd, Message, WParam, LParam
            });
        } else {
            ProcessDeferredMessage(FDeferredWindowsMessage{
                NativeWindow, HWnd, Message, WParam, LParam
            });
        }
    }

    /// Process all deferred messages accumulated during the frame.
    void ProcessDeferredEvents(float TimeDelta) {
        // Copy and clear to handle re-entrance
        TArray<FDeferredWindowsMessage> EventsToProcess = DeferredMessages;
        DeferredMessages.Clear();

        for (const FDeferredWindowsMessage& Msg : EventsToProcess) {
            ProcessDeferredMessage(Msg);
        }
    }

    /// Process a single deferred message (on game thread).
    void ProcessDeferredMessage(const FDeferredWindowsMessage& Msg) {
        switch (Msg.Message) {
        case WM_ACTIVATE:
            OnWindowActivate(Msg.Window, Msg.WParam != WA_INACTIVE);
            break;
        case WM_SHOWWINDOW:
            OnWindowShow(Msg.Window, Msg.WParam == SW_SHOW);
            break;
        case WM_ACTIVATEAPP:
            OnApplicationActivate(Msg.WParam != 0);
            break;
        }
    }

    /// Pump Windows message queue.
    /// Called every frame, regardless of idle state.
    /// This ensures focus changes are detected quickly.
    void PumpMessages(float TimeDelta) {
        MSG Message;
        while (::PeekMessage(&Message, NULL, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&Message);
            ::DispatchMessage(&Message);  // Routes to WndProc -> DeferMessage
        }
    }
};

// Global WndProc callback (registered during window creation)
LRESULT CALLBACK WindowsWndProc(HWND HWnd, UINT Message, WPARAM WParam, LPARAM LParam) {
    // Dispatch to application's ProcessMessage
    FWindowsApplication* App = GetWindowsApplication();
    return App->ProcessMessage(HWnd, Message, WParam, LParam);
}

// Inside ProcessMessage (called by WndProc dispatcher)
int32 FWindowsApplication::ProcessMessage(HWND HWnd, uint32 Message, WPARAM WParam, LPARAM LParam) {
    // Find the native window associated with this HWND
    TSharedPtr<FWindowsWindow> NativeWindow = FindWindowByHWnd(HWnd);

    switch (Message) {
    case WM_ACTIVATE:
    case WM_SHOWWINDOW:
    case WM_ACTIVATEAPP:
        // CRITICAL: Defer these messages. Do NOT process on WndProc thread.
        DeferMessage(NativeWindow, HWnd, Message, WParam, LParam);
        return 0;

    case WM_PAINT:
        // Immediate handling OK for paint
        if (NativeWindow) {
            OnPaint(NativeWindow);
        }
        return 0;

    // ... other messages
    }

    return ::DefWindowProc(HWnd, Message, WParam, LParam);
}
```

---

## 3. Engine Loop Idle Mode Decision

### Header: `runtime/engine/engine_loop.h`

```cpp
class FEngineLoop {
private:
    /// Console variable: t.IdleWhenNotForeground
    /// Set to 0 to disable throttling (for debugging).
    /// Default: 1 (throttle when unfocused).
    static int32 CVarIdleWhenNotForeground;

public:
    /// Determines whether the engine should enter idle mode this frame.
    /// Checked every frame BEFORE tick.
    bool ShouldUseIdleMode() const;

    /// Main engine tick.
    void Tick(float DeltaSeconds);
};
```

### Implementation: `runtime/engine/engine_loop.cpp`

```cpp
int32 FEngineLoop::CVarIdleWhenNotForeground = 1;
static FAutoConsoleVariableRef CVarIdleRef(
    TEXT("t.IdleWhenNotForeground"),
    FEngineLoop::CVarIdleWhenNotForeground,
    TEXT("0 = Always run at full speed\n")
    TEXT("1 = Throttle rendering when application loses focus (default)")
);

bool FEngineLoop::ShouldUseIdleMode() const {
    // Gate 1: Is this a game (not editor/server)?
    if (!IsGameProcess()) {
        return false;
    }

    // Gate 2: Does the platform support windowed mode?
    if (!PlatformSupportsWindowed()) {
        return false;
    }

    // Gate 3: Is the console variable enabled?
    if (!CVarIdleWhenNotForeground) {
        return false;
    }

    // Gate 4: Does the application NOT have focus?
    if (GetApplicationFocus()) {
        return false;  // We have focus, don't idle
    }

    // Special case: Embedded contexts (e.g., N-API in VSCode)
    if (!IsAwakeForTicking()) {
        return true;  // Parent context says sleep
    }

    // All gates passed: Idle
    return true;
}

void FEngineLoop::Tick(float DeltaSeconds) {
    // Decision gate: Check focus every frame
    bool bIdleMode = ShouldUseIdleMode();

    // Process messages BEFORE checking idle
    // (This is how we detect focus changes)
    GWindowsApplication->PumpMessages(DeltaSeconds);
    GWindowsApplication->ProcessDeferredEvents(DeltaSeconds);

    // Update idle mode based on events
    bIdleMode = ShouldUseIdleMode();

    // --- IDLE MODE: Skip all game logic and rendering ---
    if (bIdleMode) {
        // Message pump is still running (for focus changes)
        // Nothing else to do; thread naturally idles
        return;
    }

    // --- NORMAL MODE: Full game loop ---

    // Update time
    double FrameStartTime = GetCurrentTime();

    // Update game logic
    if (GEngine) {
        GEngine->Tick(DeltaSeconds);
    }

    // Render
    if (GRenderer) {
        GRenderer->Render(DeltaSeconds);
    }

    // Present
    if (GViewport) {
        GViewport->Present();
    }

    // Frame timing
    double FrameEndTime = GetCurrentTime();
    float ActualDeltaTime = FrameEndTime - FrameStartTime;

    // Optional: Cap frame rate
    float MaxFrameRate = 0.0f;  // 0 = unlimited
    if (MaxFrameRate > 0.0f) {
        float TargetFrameTime = 1.0f / MaxFrameRate;
        if (ActualDeltaTime < TargetFrameTime) {
            float SleepTime = TargetFrameTime - ActualDeltaTime;
            // Note: No Sleep() call during normal rendering!
            // VSync naturally throttles the frame rate.
            // Only Sleep() if you want to yield the CPU more aggressively.
        }
    }
}
```

---

## 4. Game Engine Conditional Tick

### Header: `runtime/engine/game_engine.h`

```cpp
class UGameEngine {
public:
    /// Main game engine tick.
    /// @param DeltaSeconds Time elapsed since last frame
    /// @param bIdleMode If true, skip expensive updates (rendering, physics, etc.)
    void Tick(float DeltaSeconds, bool bIdleMode);
};
```

### Implementation: `runtime/engine/game_engine.cpp`

```cpp
void UGameEngine::Tick(float DeltaSeconds, bool bIdleMode) {
    UZ_PROFILE_SCOPE("UGameEngine::Tick");

    // Early exit if in idle mode
    if (bIdleMode) {
        // Note: Physics, rendering, world ticks are NOT run.
        // The message pump is still running (in EngineLoop::Tick),
        // so focus changes will be detected.
        return;
    }

    // --- Normal tick path (when NOT idle) ---

    // 1. Input processing
    if (InputSystem) {
        InputSystem->Tick(DeltaSeconds);
    }

    // 2. World updates
    for (FWorldContext& WorldContext : WorldList) {
        if (WorldContext.World && WorldContext.World->ShouldTick()) {
            WorldContext.World->Tick(DeltaSeconds);
        }
    }

    // 3. Physics
    if (PhysicsEngine) {
        PhysicsEngine->Tick(DeltaSeconds);
    }

    // 4. Rendering (viewport preparation)
    if (GameViewport) {
        GameViewport->Tick(DeltaSeconds);
    }

    // 5. Asset streaming
    if (StreamingManager) {
        StreamingManager->Update(DeltaSeconds);
    }

    UZ_LOG(Engine, Verbose, "Frame time: {:.2f}ms", DeltaSeconds * 1000.0f);
}
```

---

## 5. Rendering System Integration

### Header: `runtime/renderer/viewport.h`

```cpp
class FViewport {
public:
    /// Render the scene to the backbuffer.
    /// Skipped entirely when window is minimized (bIdleMode = true).
    void Render(float DeltaSeconds);

    /// Present the backbuffer to the screen.
    /// Only called when NOT in idle mode.
    void Present();
};
```

### Implementation: `runtime/renderer/viewport.cpp`

```cpp
void FViewport::Render(float DeltaSeconds) {
    UZ_PROFILE_SCOPE("Viewport::Render");

    // Sanity check: Should never be called in idle mode
    // (The engine loop should skip us entirely)
    if (GEngineLoop->ShouldUseIdleMode()) {
        UZ_LOG(Render, Warning, "Render called while in idle mode!");
        return;
    }

    // Check window state (reactive query, for Slate-like fine-grained control)
    if (!WindowHandle->IsVisible()) {
        UZ_LOG(Render, Verbose, "Window not visible, skipping render");
        return;
    }

    if (WindowHandle->IsMinimized()) {
        UZ_LOG(Render, Verbose, "Window minimized, skipping render");
        return;
    }

    // Get backbuffer dimensions
    int32 Width = WindowHandle->GetWidth();
    int32 Height = WindowHandle->GetHeight();

    if (Width <= 0 || Height <= 0) {
        UZ_LOG(Render, Warning, "Invalid viewport dimensions: {}x{}", Width, Height);
        return;
    }

    // Render to backbuffer
    RHIContext->BeginDrawingViewport(this);
    {
        // Clear
        RHIContext->Clear(FLinearColor::Black);

        // Render scene (using render graph, deferred rendering, etc.)
        if (SceneRenderer) {
            SceneRenderer->Render();
        }
    }
    RHIContext->EndDrawingViewport(this, true);  // true = present
}

void FViewport::Present() {
    UZ_PROFILE_SCOPE("Viewport::Present");

    // This should only be called when NOT idle
    if (GEngineLoop->ShouldUseIdleMode()) {
        UZ_LOG(Render, Warning, "Present called while in idle mode!");
        return;
    }

    // Swapchain present
    if (Swapchain) {
        HRESULT HR = Swapchain->Present(0, 0);
        if (FAILED(HR)) {
            if (HR == DXGI_ERROR_DEVICE_REMOVED) {
                UZ_LOG(Render, Error, "Device removed (likely minimized window)");
                // Trigger device lost recovery
                RecoverFromDeviceLost();
            } else {
                UZ_LOG(Render, Error, "Present failed: {}", HR);
            }
        }
    }
}
```

---

## 6. Test Cases

### Unit Tests: `tests/core/test_window_focus.cpp`

```cpp
#include <gtest/gtest.h>
#include "platform/focus.h"
#include "engine/engine_loop.h"

class WindowFocusTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Mock focus callback
        SetFocusCallback([]() { return GAppHasFocus; });
    }

    bool GAppHasFocus = true;
};

TEST_F(WindowFocusTest, IdleModeEntersWhenFocusLost) {
    FEngineLoop Loop;

    // Initially has focus
    GAppHasFocus = true;
    EXPECT_FALSE(Loop.ShouldUseIdleMode());

    // Loses focus
    GAppHasFocus = false;
    EXPECT_TRUE(Loop.ShouldUseIdleMode());
}

TEST_F(WindowFocusTest, IdleModeCanBeDisabledViaCvar) {
    FEngineLoop Loop;
    GAppHasFocus = false;

    // CVar enabled (default)
    Loop.SetIdleWhenNotForeground(1);
    EXPECT_TRUE(Loop.ShouldUseIdleMode());

    // CVar disabled
    Loop.SetIdleWhenNotForeground(0);
    EXPECT_FALSE(Loop.ShouldUseIdleMode());
}

TEST_F(WindowFocusTest, MessagePumpRunsEveryFrame) {
    FWindowsApplication App;
    int32 PumpCount = 0;

    // Mock message pump
    App.SetMessagePumpCallback([&PumpCount]() { ++PumpCount; });

    // Even when idle, pump should run
    GAppHasFocus = false;  // Idle mode ON
    App.Tick();
    int32 IdlePumpCount = PumpCount;

    GAppHasFocus = true;   // Normal mode
    App.Tick();
    int32 NormalPumpCount = PumpCount - IdlePumpCount;

    EXPECT_GT(IdlePumpCount, 0) << "Message pump should run even in idle mode";
    EXPECT_EQ(IdlePumpCount, NormalPumpCount) << "Pump frequency should be the same";
}

TEST_F(WindowFocusTest, ResumeFullSpeedWhenFocusReturns) {
    UGameEngine Engine;
    int32 WorldTickCount = 0;

    // Mock world tick
    Engine.SetWorldTickCallback([&WorldTickCount]() { ++WorldTickCount; });

    // Start in idle mode
    GAppHasFocus = false;
    Engine.Tick(0.016f, true);  // bIdleMode = true
    EXPECT_EQ(0, WorldTickCount) << "World should not tick in idle mode";

    // Regain focus
    GAppHasFocus = true;
    Engine.Tick(0.016f, false);  // bIdleMode = false
    EXPECT_EQ(1, WorldTickCount) << "World should tick immediately when focus returns";
}
```

---

## 7. Integration Checklist

For UzEngine integration:

- [ ] `SetFocusCallback()` called in `Engine::PreInit()`
- [ ] `GetApplicationFocus()` checks foreground process ID on Windows
- [ ] `DeferMessage()` queues `WM_ACTIVATE`, `WM_SHOWWINDOW`, `WM_ACTIVATEAPP`
- [ ] `ProcessDeferredEvents()` called at frame start (before tick)
- [ ] `PumpMessages()` called every frame (even when idle)
- [ ] `ShouldUseIdleMode()` gated by focus + console variable
- [ ] `Engine::Tick()` respects `bIdleMode` parameter
- [ ] Rendering skipped when `bIdleMode = true`
- [ ] `Present()` not called on minimized window (implicit, via skip render)
- [ ] No explicit `Sleep()` calls; rely on VSync / lack of work
- [ ] Unit tests for focus/idle transitions
- [ ] Integration test: Alt-tab to minimize, verify rendering stops, resume works

---

## Notes for N-API Integration

If using N-API in VSCode:

1. Add `FEmbeddedCommunication::IsAwakeForTicking()` check in `ShouldUseIdleMode()`
2. Parent (VSCode) can call `uz_engine_set_awake_for_ticking(bool)` to control rendering
3. Even when throttled, message pump keeps running for responsive focus changes
4. Frame streaming (if used) should also respect idle mode (encoder not called)

