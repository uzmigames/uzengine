# Platform Module Reference

OS abstraction for window management and input.

> **Headers**: `platform/windows/platform_window.h`, `platform/windows/input.h`
> **Guide**: [Platform Abstraction](../../guides/platform/overview.md)

## PlatformWindow

Win32 HWND wrapper with message pump.

### WindowDesc

```cpp
struct WindowDesc {
    const char* title;    // Window title
    int width;            // Client area width
    int height;           // Client area height
};
```

### Methods

| Method | Description |
|--------|-------------|
| `Create(WindowDesc)` | Create Win32 window. Returns false on failure. |
| `Show()` | Make window visible |
| `Hide()` | Hide window |
| `PumpMessages()` | Process Win32 messages. Returns false on WM_QUIT. |
| `IsOpen()` | Window still alive? |
| `WasResized()` | True if resized since last check |
| `ClearResizedFlag()` | Reset resize flag |
| `GetWidth() / GetHeight()` | Current client area dimensions |
| `GetHWND()` | Raw HWND for swapchain creation |
| `ToggleFullscreen()` | Switch between windowed and fullscreen |

---

## InputSystem

Keyboard, mouse, and action mapping.

### Keyboard

| Method | Description |
|--------|-------------|
| `IsKeyDown(int vk)` | Key currently held |
| `IsKeyJustPressed(int vk)` | Key pressed this frame |
| `IsKeyJustReleased(int vk)` | Key released this frame |

### Mouse

| Method | Description |
|--------|-------------|
| `GetMousePosition()` | Current cursor position (Vec2) |
| `GetMouseDelta()` | Movement since last frame |
| `GetScrollDelta()` | Scroll wheel delta |
| `IsMouseButtonDown(MouseButton)` | Button held |

### Action Mapping

| Method | Description |
|--------|-------------|
| `RegisterAction(name, key)` | Bind a logical action to a physical input |
| `IsActionDown(name)` | Action currently active |
| `IsActionJustPressed(name)` | Action triggered this frame |
| `IsActionJustReleased(name)` | Action released this frame |

Multiple physical inputs can map to the same action for rebinding support.
