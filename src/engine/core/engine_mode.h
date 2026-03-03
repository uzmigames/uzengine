#pragma once

// engine_mode.h -- Engine mode enum and CLI argument parsing.
//
// Defines the two engine modes (Editor, Play) and provides inline parse
// functions for command-line arguments. Header-only -- no .cpp needed.
//
// Usage:
//   int main(int argc, char** argv) {
//       auto args = uz::parse_launch_args(argc, argv);
//       // args.mode == EngineMode::Editor or EngineMode::Play
//       // args.port == 9002 (editor default) or 9003 (play default)
//   }

#include <cstdint>
#include <cstring>
#include <string>

namespace uz {

// ============================================================================
// EngineMode enum
// ============================================================================

/// The two top-level engine modes. Editor mode runs the scene editor with
/// no physics/scripts/audio. Play mode runs the full game loop.
enum class EngineMode {
    Editor,  // Scene editing, IPC on port 9002 (default)
    Play,    // Game runtime, IPC on port 9003 (default)
};

// ============================================================================
// EngineLaunchArgs
// ============================================================================

/// Parsed command-line arguments for engine startup.
struct EngineLaunchArgs {
    EngineMode  mode         = EngineMode::Editor;
    std::string project_path;   // --project=<path>
    std::string scene_path;     // --scene=<path>
    int         port         = 9002;   // --port=<n>, default depends on mode
};

// ============================================================================
// parse_engine_mode  (simple mode-only parse)
// ============================================================================

/// Parses argc/argv for --editor or --play flags. Returns the mode.
/// If neither flag is present, defaults to Editor.
/// If both flags are present, the last one wins.
inline EngineMode parse_engine_mode(int argc, char** argv) {
    EngineMode mode = EngineMode::Editor;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--editor") == 0) {
            mode = EngineMode::Editor;
        } else if (std::strcmp(argv[i], "--play") == 0) {
            mode = EngineMode::Play;
        }
    }
    return mode;
}

// ============================================================================
// parse_launch_args  (full argument parse)
// ============================================================================

/// Parses all supported launch arguments from argc/argv.
///
/// Supported flags:
///   --editor          Set mode to Editor (default)
///   --play            Set mode to Play
///   --project=<path>  Set project root directory
///   --scene=<path>    Set scene file to load
///   --port=<n>        Override WebSocket/IPC port
///
/// Default port is 9002 for Editor mode, 9003 for Play mode.
/// An explicit --port=<n> overrides the mode-based default.
inline EngineLaunchArgs parse_launch_args(int argc, char** argv) {
    EngineLaunchArgs args;
    bool port_explicit = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (std::strcmp(arg, "--editor") == 0) {
            args.mode = EngineMode::Editor;
        } else if (std::strcmp(arg, "--play") == 0) {
            args.mode = EngineMode::Play;
        } else if (std::strncmp(arg, "--project=", 10) == 0) {
            args.project_path = arg + 10;
        } else if (std::strncmp(arg, "--scene=", 8) == 0) {
            args.scene_path = arg + 8;
        } else if (std::strncmp(arg, "--port=", 7) == 0) {
            args.port = std::atoi(arg + 7);
            port_explicit = true;
        }
    }

    // Apply mode-based default port if user did not specify --port=.
    if (!port_explicit) {
        args.port = (args.mode == EngineMode::Play) ? 9003 : 9002;
    }

    return args;
}

} // namespace uz
