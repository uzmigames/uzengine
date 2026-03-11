# Platform Abstraction

UzEngine isolates OS-specific code in the `platform/` directory. Currently
Windows is the only implemented platform; Linux and macOS support is planned.

> **Headers**: `platform/windows/uz_platform_window.h`, `platform/windows/uz_input.h`
> **API Reference**: [Platform Reference](../../reference/platform/index.md)

## Window

`PlatformWindow` wraps Win32 HWND creation and message handling:

```cpp
WindowDesc desc;
desc.title = "My Window";
desc.width = 1280;
desc.height = 720;

PlatformWindow window;
window.Create(desc);
window.Show();

while (window.PumpMessages() && window.IsOpen()) {
    if (window.WasResized()) {
        // Handle resize
        window.ClearResizedFlag();
    }
    // Render frame...
}
```

Features:
- Resizable with `WM_SIZE` handling
- Fullscreen toggle
- Show/Hide
- Returns raw `HWND` for swapchain creation

## Input System

Category-based input with keyboard, mouse, and action mapping:

### Raw Input

```cpp
InputSystem input;

// Poll keyboard
if (input.IsKeyDown(VK_SPACE)) { /* ... */ }
if (input.IsKeyJustPressed(VK_ESCAPE)) { /* ... */ }

// Poll mouse
Vec2 mousePos = input.GetMousePosition();
float scrollDelta = input.GetScrollDelta();
if (input.IsMouseButtonDown(MouseButton::Left)) { /* ... */ }
```

### Action Mapping

Map logical actions to physical inputs for rebinding support:

```cpp
input.RegisterAction("Jump", VK_SPACE);
input.RegisterAction("Jump", GamepadButton::A);  // multiple bindings

if (input.IsActionDown("Jump")) {
    player.Jump();
}

if (input.IsActionJustPressed("Fire")) {
    weapon.Fire();
}
```

## Platform Guards in Build

The Zig build system automatically excludes platform-specific code on other targets:

```zig
if (is_windows) {
    runtime_mod.addCSourceFiles(.{
        .root = b.path("platform/windows"),
        .files = &.{ "uz_platform_window.cpp", "uz_input.cpp" },
    });
}
```

Cross-platform core (math, containers, config, logging) compiles everywhere.

## Related Pages

- [Build System](../build-system/zig-build.md) — Platform guards
- [First Window](../../getting-started/first-window.md) — Window + render loop walkthrough
