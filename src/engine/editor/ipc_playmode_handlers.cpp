// ipc_playmode_handlers.cpp -- IPC handler implementations for play mode.
//
// Registers handlers for playmode.status, playmode.start, playmode.stop,
// engine.pause, engine.resume, and engine.setPaused.

#include "engine/editor/ipc_playmode_handlers.h"

#include <cstdlib>

namespace uz {

// ============================================================================
// Internal helpers
// ============================================================================

static nlohmann::json make_ok() {
    return {{"ok", true}};
}

static nlohmann::json make_error(const char* msg) {
    return {{"ok", false}, {"error", msg}};
}

// ============================================================================
// playmode_register_handlers
// ============================================================================

void playmode_register_handlers(IpcRegistry& registry,
                                 EngineMode mode,
                                 PlayModeState* state,
                                 StopCallback on_stop) {

    // ----------------------------------------------------------------
    // playmode.status — returns current mode, scene, and pause state
    // ----------------------------------------------------------------
    registry.add("playmode.status",
        [mode, state](const nlohmann::json& /*args*/) -> nlohmann::json {
            if (!state) {
                return make_error("no state");
            }
            return {
                {"ok",     true},
                {"mode",   mode == EngineMode::Play ? "play" : "editor"},
                {"scene",  state->scene_path},
                {"paused", state->paused.load()}
            };
        });

    // ----------------------------------------------------------------
    // playmode.start — editor mode: returns the play-mode port.
    //                  play mode: error (already in play mode).
    // ----------------------------------------------------------------
    registry.add("playmode.start",
        [mode](const nlohmann::json& /*args*/) -> nlohmann::json {
            if (mode == EngineMode::Play) {
                return make_error("already in play mode");
            }
            // Editor mode: the extension will spawn a new render.exe --play process.
            // Return the port the play process should use.
            return {{"ok", true}, {"port", 9003}};
        });

    // ----------------------------------------------------------------
    // playmode.stop — play mode: triggers graceful exit.
    //                 editor mode: error (not in play mode).
    // ----------------------------------------------------------------
    registry.add("playmode.stop",
        [mode, on_stop](const nlohmann::json& /*args*/) -> nlohmann::json {
            if (mode == EngineMode::Editor) {
                return make_error("not in play mode");
            }
            // Play mode: trigger graceful shutdown.
            auto result = make_ok();

            if (on_stop) {
                on_stop();
            } else {
                std::exit(0);
            }

            return result;
        });

    // ----------------------------------------------------------------
    // engine.pause — sets paused = true
    // ----------------------------------------------------------------
    registry.add("engine.pause",
        [state](const nlohmann::json& /*args*/) -> nlohmann::json {
            if (!state) {
                return make_error("no state");
            }
            state->paused.store(true);
            return make_ok();
        });

    // ----------------------------------------------------------------
    // engine.resume — sets paused = false
    // ----------------------------------------------------------------
    registry.add("engine.resume",
        [state](const nlohmann::json& /*args*/) -> nlohmann::json {
            if (!state) {
                return make_error("no state");
            }
            state->paused.store(false);
            return make_ok();
        });

    // ----------------------------------------------------------------
    // engine.setPaused — sets paused to the given bool value
    // ----------------------------------------------------------------
    registry.add("engine.setPaused",
        [state](const nlohmann::json& args) -> nlohmann::json {
            if (!state) {
                return make_error("no state");
            }
            if (!args.contains("paused") || !args["paused"].is_boolean()) {
                return make_error("missing or invalid 'paused' (boolean required)");
            }
            state->paused.store(args["paused"].get<bool>());
            return make_ok();
        });
}

} // namespace uz
