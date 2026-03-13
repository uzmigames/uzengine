#pragma once

/// UzEngine Built-in Subsystem Wrappers
///
/// Thin adapter classes that wrap existing engine free functions and classes
/// into the `ISubsystem` interface, enabling them to participate in the
/// `SubsystemRegistry` lifecycle (ordered init, shutdown, ticking).
///
/// # Subsystems defined here
/// - `LoggingSubsystem` — wraps `uz::LogInit()` / `uz::LogShutdown()`
/// - `ConfigSubsystem` — owns a `uz::Config` instance with engine defaults
/// - `PathsSubsystem` — ordering anchor for `uz::GetExeDir()` (no-op init)

#include "subsystem.h"
#include "subsystem_registry.h"

#include "../config/config.h"
#include "../logging/log.h"
#include "../paths/paths.h"
#include "../profiling/profiler.h"
#include "../assertions/engine_assert.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LoggingSubsystem — wraps uz::LogInit / uz::LogShutdown
// ─────────────────────────────────────────────────────────────────────────────

/// Subsystem adapter for the engine logging system.
///
/// Calls `uz::LogInit()` during initialization and `uz::LogShutdown()` during
/// teardown. Must be the first subsystem initialized — all other subsystems
/// depend on logging being available.
///
/// # Init Phase
/// `PreInit` — logging is a prerequisite for everything else.
///
/// # Dependencies
/// None (root subsystem).
///
/// # Tick
/// Does not tick (`TickGroup::None`).
class LoggingSubsystem final : public ISubsystem {
public:
    /// Returns `"Logging"` — the canonical name used by dependents.
    [[nodiscard]] const char* GetName() const override { return "Logging"; }

    /// Returns `InitPhase::PreInit`.
    [[nodiscard]] InitPhase GetInitPhase() const override { return InitPhase::PreInit; }

    /// Initializes the spdlog-based logging backend.
    ///
    /// # Returns
    /// - `true` always. LogInit sets up console + file sinks and cannot fail
    ///   in a way that is recoverable.
    ///
    /// # Side Effects
    /// Creates/opens `uzengine.log` in the working directory.
    [[nodiscard]] bool Init() override {
        UZ_PROFILE_SCOPE();
        LogInit();
        UZ_LOG(Lifecycle, Info, "LoggingSubsystem initialized");
        return true;
    }

    /// Flushes and tears down the logging backend.
    ///
    /// After this call, all `UZ_LOG` calls become no-ops.
    void Shutdown() override {
        UZ_LOG(Lifecycle, Info, "LoggingSubsystem shutting down");
        LogShutdown();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PathsSubsystem — ordering anchor for uz::GetExeDir()
// ─────────────────────────────────────────────────────────────────────────────

/// Subsystem adapter for the engine path resolution system.
///
/// `uz::GetExeDir()` and `uz::ResolveEngineResource()` are stateless utility
/// functions that cache their result on first call. This subsystem exists as
/// an ordering anchor so that other subsystems can declare a dependency on
/// `"Paths"` to express that they need path resolution available.
///
/// # Init Phase
/// `PreInit` — path resolution is needed before GPU or file I/O subsystems.
///
/// # Dependencies
/// `"Logging"` — so that any path resolution errors can be logged.
///
/// # Tick
/// Does not tick (`TickGroup::None`).
class PathsSubsystem final : public ISubsystem {
public:
    /// Returns `"Paths"` — the canonical name used by dependents.
    [[nodiscard]] const char* GetName() const override { return "Paths"; }

    /// Returns `InitPhase::PreInit`.
    [[nodiscard]] InitPhase GetInitPhase() const override { return InitPhase::PreInit; }

    /// Returns `{"Logging"}`.
    [[nodiscard]] std::vector<const char*> GetDependencies() const override { return {"Logging"}; }

    /// Eagerly resolves and caches the executable directory.
    ///
    /// # Returns
    /// - `true` if `GetExeDir()` returned a non-empty path.
    /// - `false` if path resolution failed (platform API error).
    ///
    /// # Side Effects
    /// Logs the resolved exe directory at `Info` level.
    [[nodiscard]] bool Init() override {
        UZ_PROFILE_SCOPE();
        const auto exeDir = GetExeDir();
        if (exeDir.empty()) {
            UZ_LOG(Lifecycle, Error, "PathsSubsystem: failed to resolve executable directory");
            return false;
        }
        UZ_LOG(Lifecycle, Info, "PathsSubsystem initialized: exeDir='{}'", exeDir.string());
        return true;
    }

    /// No-op — path utilities are stateless and require no teardown.
    void Shutdown() override {
        UZ_LOG(Lifecycle, Info, "PathsSubsystem shutting down");
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ConfigSubsystem — owns and initializes uz::Config
// ─────────────────────────────────────────────────────────────────────────────

/// Subsystem adapter for the engine configuration system.
///
/// Owns a `uz::Config` instance, populates it with engine defaults on init,
/// and exposes it for other subsystems to query. The config instance lifetime
/// is tied to this subsystem — it is valid between `Init()` and `Shutdown()`.
///
/// # Init Phase
/// `PreInit` — configuration is needed before GPU or gameplay subsystems.
///
/// # Dependencies
/// `"Logging"` — so config parsing errors can be logged.
///
/// # Tick
/// Does not tick (`TickGroup::None`).
///
/// # Access
/// After initialization, call `GetConfig()` to retrieve the config instance.
/// The returned reference is valid until `Shutdown()` is called.
class ConfigSubsystem final : public ISubsystem {
public:
    /// Returns `"Config"` — the canonical name used by dependents.
    [[nodiscard]] const char* GetName() const override { return "Config"; }

    /// Returns `InitPhase::PreInit`.
    [[nodiscard]] InitPhase GetInitPhase() const override { return InitPhase::PreInit; }

    /// Returns `{"Logging"}`.
    [[nodiscard]] std::vector<const char*> GetDependencies() const override { return {"Logging"}; }

    /// Populates the config with engine defaults.
    ///
    /// # Returns
    /// - `true` always. Default population cannot fail.
    ///
    /// # Side Effects
    /// Calls `ConfigSetEngineDefaults()` to populate built-in values.
    /// Does NOT load a .uzproject file — that is the caller's responsibility
    /// after init, via `GetConfig().LoadFromFile()`.
    [[nodiscard]] bool Init() override {
        UZ_PROFILE_SCOPE();
        ConfigSetEngineDefaults(m_config);
        UZ_LOG(Lifecycle, Info, "ConfigSubsystem initialized with engine defaults");
        return true;
    }

    /// No-op — Config has no external resources to release.
    void Shutdown() override {
        UZ_LOG(Lifecycle, Info, "ConfigSubsystem shutting down");
    }

    /// Returns a mutable reference to the owned Config instance.
    ///
    /// # Returns
    /// Reference to the config. Valid between `Init()` and `Shutdown()`.
    ///
    /// # Thread Safety
    /// Safe for concurrent reads after `Init()`. Writes (LoadFromFile, ApplyCLIOverrides)
    /// must be serialized by the caller.
    [[nodiscard]] Config& GetConfig() noexcept { return m_config; }

    /// Returns a const reference to the owned Config instance.
    [[nodiscard]] const Config& GetConfig() const noexcept { return m_config; }

private:
    Config m_config; ///< Owned configuration instance with cascading merge.
};

} // namespace uz
