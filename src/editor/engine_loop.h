// ---------------------------------------------------------------------------
// engine_loop.h -- Main render/message loop for the engine.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

namespace uz {
    class Renderer;
    class IpcPoller;
    struct SceneParams;
    struct EngineMetrics;
    class Timer;
}

/// Run the Win32 message pump + render loop.  Returns the app exit code.
int runMainLoop(HWND hwnd,
                uz::Renderer& renderer,
                uz::Timer& timer,
                uz::SceneParams& sceneParams,
                uz::EngineMetrics& metrics,
                uz::IpcPoller& ipcPoller);
