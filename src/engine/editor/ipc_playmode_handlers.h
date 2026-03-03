#pragma once

// ipc_playmode_handlers.h -- Register play-mode IPC handlers into an IpcRegistry.
//
// Handlers:
//   playmode.status  -> { ok, mode, scene, paused }
//   playmode.start   -> { ok, port }  (editor mode only — extension spawns play process)
//   playmode.stop    -> { ok }        (play mode only — triggers graceful exit)
//   engine.pause     -> { ok }
//   engine.resume    -> { ok }
//   engine.setPaused -> { ok }
//
// Call playmode_register_handlers() once at startup after determining the
// engine mode. The handlers capture the mode and shared paused state.

#include "common/ipc_registry.h"
#include "engine/core/engine_mode.h"

#include <atomic>
#include <functional>

namespace uz {

/// Shared pause state for play mode. Accessible from the main loop and IPC
/// handlers. Default is false (not paused).
///
/// This is an atomic bool so the IPC handler thread can safely write it
/// while the game loop thread reads it.
struct PlayModeState {
    std::atomic<bool> paused{false};
    std::string       scene_path;
};

/// Callback type for the stop handler. In production this calls exit(0);
/// in tests this is replaced with a mock/recorder.
using StopCallback = std::function<void()>;

/// Register all playmode.* and engine.pause/resume/setPaused handlers.
///
/// @param registry  The IPC registry to add handlers to.
/// @param mode      The engine mode (Editor or Play) — affects handler behavior.
/// @param state     Shared mutable state (paused flag, scene path).
///                  Must remain valid for the lifetime of the registry.
/// @param on_stop   Callback invoked when playmode.stop is received in Play mode.
///                  Defaults to exit(0) if nullptr.
void playmode_register_handlers(IpcRegistry& registry,
                                 EngineMode mode,
                                 PlayModeState* state,
                                 StopCallback on_stop = nullptr);

} // namespace uz
