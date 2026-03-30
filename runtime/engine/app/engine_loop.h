#pragma once

/// UzEngine Engine Loop
///
/// Orchestrates the full engine lifecycle: CLI parsing, module registration,
/// subsystem initialization, the main frame loop, and ordered shutdown.
///
/// # Lifecycle
/// 1. `Run()` parses CLI args (or accepts a pre-built `EngineConfig`).
/// 2. `Initialize()` runs all `UZ_REGISTER_MODULE` callbacks, then `InitAll()`.
/// 3. `MainLoop()` drives `TimeManager::BeginFrame()`, fixed-step physics ticks,
///    and variable-rate update ticks until exit is requested or `maxFrames` is reached.
/// 4. `Shutdown()` tears down all subsystems in reverse init order.
///
/// # Thread Safety
/// All public methods must be called from the main thread.

#include "subsystem_registry.h"
#include "time_manager.h"
#include "module_registry.h"

#include <memory>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// EngineConfig — runtime configuration parsed from CLI or set programmatically
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the engine loop, parsed from CLI arguments and/or set in code.
///
/// All fields have safe defaults. The engine runs in windowed, unlimited-frame mode
/// unless overridden via CLI flags or direct assignment.
struct EngineConfig {
    bool headless = false;             ///< Run without window or GPU if true.
    int screenshotFrame = -1;          ///< Frame number to capture a screenshot (-1 = disabled).
    std::string screenshotPath;        ///< File path to save the screenshot.
    std::string configPath;            ///< Override path for the engine config file.
    int maxFrames = 0;                 ///< Maximum frames to run before auto-exit (0 = unlimited).
    int shadowDebugMode = 0;           ///< Shadow/buffer debug visualization (0=off, 1=buffer grid). See shadow_debug.hlsli.
    bool diagMode = false;             ///< --diag: run diagnostics, output JSON to stdout.
    bool pixelDebugEnabled = false;    ///< --pixel-debug: enable GPU printf for a pixel.
    uint32_t pixelDebugX = 0;          ///< Debug pixel X coordinate.
    uint32_t pixelDebugY = 0;          ///< Debug pixel Y coordinate.
};

// ─────────────────────────────────────────────────────────────────────────────
// ParseCommandLine — CLI argument parser
// ─────────────────────────────────────────────────────────────────────────────

/// Parses command-line arguments into an `EngineConfig`.
///
/// # Supported Flags
/// | Flag                          | Effect                                              |
/// |-------------------------------|-----------------------------------------------------|
/// | `--headless`                  | Sets `headless = true`                              |
/// | `--screenshot <path>`         | Sets `screenshotPath` to `<path>`                   |
/// | `--screenshot-frame <N>`      | Sets `screenshotFrame` to `<N>`                     |
/// | `--config <path>`             | Sets `configPath` to `<path>`                       |
/// | `--frames <N>`                | Sets `maxFrames` to `<N>`                           |
///
/// Unknown flags are silently ignored. `argv[0]` (program name) is skipped.
///
/// # Arguments
/// * `argc` - Argument count (from `main`).
/// * `argv` - Argument values (from `main`). Must not be null.
///
/// # Returns
/// A populated `EngineConfig` with defaults for any unspecified flags.
[[nodiscard]] EngineConfig ParseCommandLine(int argc, char* argv[]);

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop — main application driver
// ─────────────────────────────────────────────────────────────────────────────

/// Main engine loop orchestrating initialization, per-frame ticking, and shutdown.
///
/// Owns a `SubsystemRegistry` and a `TimeManager`. External code registers subsystems
/// either directly via `RegisterSubsystem()` or automatically via `UZ_REGISTER_MODULE`.
///
/// # Usage
/// ```cpp
/// int main(int argc, char* argv[]) {
///     uz::EngineLoop loop;
///     loop.RegisterSubsystem(std::make_unique<MySubsystem>());
///     return loop.Run(argc, argv);
/// }
/// ```
class EngineLoop {
public:
    /// Constructs the engine loop and creates the internal `TimeManager` subsystem.
    EngineLoop();

    /// Destroys the engine loop. Does NOT call `Shutdown()` — the caller must
    /// ensure `Run()` completed or `Shutdown()` was called explicitly.
    ~EngineLoop();

    /// Non-copyable.
    EngineLoop(const EngineLoop&) = delete;
    EngineLoop& operator=(const EngineLoop&) = delete;

    /// Non-movable (subsystem raw pointers would dangle).
    EngineLoop(EngineLoop&&) = delete;
    EngineLoop& operator=(EngineLoop&&) = delete;

    /// Registers a subsystem before calling `Run()`.
    ///
    /// Subsystems registered here participate in the full lifecycle (init, tick, shutdown).
    /// Must be called before `Run()`.
    ///
    /// # Arguments
    /// * `subsystem` - Unique ownership of the subsystem. Must not be null.
    void RegisterSubsystem(std::unique_ptr<ISubsystem> subsystem);

    /// Main entry point. Parses CLI args, initializes, runs the loop, and shuts down.
    ///
    /// # Arguments
    /// * `argc` - Argument count from `main`.
    /// * `argv` - Argument values from `main`.
    ///
    /// # Returns
    /// - `0` on clean exit.
    /// - `1` if initialization failed.
    [[nodiscard]] int Run(int argc, char* argv[]);

    /// Main entry point with a pre-built config (skips CLI parsing).
    ///
    /// # Arguments
    /// * `config` - Engine configuration to use.
    ///
    /// # Returns
    /// - `0` on clean exit.
    /// - `1` if initialization failed.
    [[nodiscard]] int Run(const EngineConfig& config);

    /// Requests the loop to stop after the current frame completes.
    ///
    /// Can be called from a subsystem's `Tick()`, a signal handler, or any code
    /// running on the main thread. Takes effect before the next frame begins.
    void RequestExit();

    /// Returns a mutable reference to the subsystem registry.
    ///
    /// # Returns
    /// The registry managing all subsystem lifecycles.
    [[nodiscard]] SubsystemRegistry& GetRegistry() { return registry_; }

    /// Returns a mutable reference to the time manager.
    ///
    /// # Returns
    /// The time manager driving frame timing and fixed-step accumulation.
    /// Only valid after construction (always available).
    [[nodiscard]] TimeManager& GetTimeManager() { return *timeManager_; }

    /// Returns the current engine configuration.
    ///
    /// # Returns
    /// A const reference to the active config. Only meaningful after `Run()` is called.
    [[nodiscard]] const EngineConfig& GetConfig() const { return config_; }

    /// Whether the engine is running in headless mode (no window or GPU).
    ///
    /// # Returns
    /// `true` if `config_.headless` is set.
    [[nodiscard]] bool IsHeadless() const { return config_.headless; }

private:
    SubsystemRegistry registry_;
    TimeManager* timeManager_ = nullptr;  ///< Non-owning pointer; owned by registry_.
    EngineConfig config_;
    bool exitRequested_ = false;

    /// Runs module registrars and initializes all subsystems.
    ///
    /// # Returns
    /// `true` if all subsystems initialized successfully.
    [[nodiscard]] bool Initialize();

    /// Executes the main frame loop until exit is requested or maxFrames is reached.
    void MainLoop();

    /// Shuts down all subsystems in reverse init order.
    void Shutdown();

    /// Executes a single frame: fixed-step physics ticks + variable-rate update.
    void RunOneFrame();
};

} // namespace uz
