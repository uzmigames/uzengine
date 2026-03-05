// ---------------------------------------------------------------------------
// win32_app.h -- Win32 window creation and stdio setup for the engine.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

/// Redirect stdout/stderr to inherited pipe handles (for GUI apps spawned by
/// Node.js cp.spawn()).
void setupStdio();

/// Register the window class, detect headless mode, and create the HWND.
/// Returns nullptr on failure.
HWND createEngineWindow(HINSTANCE hInstance, int nCmdShow);
