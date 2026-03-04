#pragma once

// ModuleManager — explicit module startup/shutdown with reverse-order teardown.
//
// All engine subsystems register as modules. StartupAll() initializes modules
// by priority (descending), and ShutdownAll() tears them down in strict reverse
// order (LIFO). If a module's startup() throws, all previously started modules
// are automatically shut down in reverse order.
//
// Thread-safety: NOT thread-safe. Register/startup/shutdown must be called from
// the same thread (typically main). Module code itself may create internal threads.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string_view>
#include <vector>

namespace uz {

// ============================================================================
// ModuleState — lifecycle phase of a module
// ============================================================================

enum class ModuleState : uint8_t {
    Registered,   // Added to manager, not yet started
    Starting,     // startup() is executing
    Running,      // startup() completed successfully
    ShuttingDown, // shutdown() is executing
    Stopped,      // shutdown() completed (or never started)
};

// ============================================================================
// ModuleInterface — base class for all engine modules
// ============================================================================

struct ModuleInterface {
    virtual ~ModuleInterface() = default;

    /// Called during engine initialization, in priority order (highest first).
    virtual void startup() = 0;

    /// Called during engine shutdown, in REVERSE startup order.
    /// Must not throw. Log errors and continue.
    virtual void shutdown() = 0;

    /// Human-readable module name, used for logging and debug.
    [[nodiscard]] virtual const char* name() const = 0;

    /// Startup priority. Higher values start earlier. Default is 0.
    /// Example: RHI = 1000, Scene = 500, Editor = 100.
    [[nodiscard]] virtual int priority() const { return 0; }
};

// ============================================================================
// ModuleManager — manages lifecycle of all registered modules
// ============================================================================

class ModuleManager {
public:
    ModuleManager() = default;
    ~ModuleManager();

    // Non-copyable, non-movable (singleton-like ownership semantics).
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    /// Register a module. Takes a non-owning pointer — caller owns the module
    /// lifetime and must ensure it outlives the manager (or shutdown is called
    /// before destruction). Modules must be registered before startup_all().
    void register_module(ModuleInterface* mod);

    /// Start all registered modules in priority order (descending).
    /// If any module's startup() throws, all previously started modules are
    /// automatically shut down in reverse order, and the exception is re-thrown.
    /// Returns false if no modules were registered.
    bool startup_all();

    /// Shut down all running modules in reverse startup order.
    /// Catches exceptions from shutdown() — logs and continues.
    void shutdown_all();

    /// Returns the current state of a module by name.
    /// Returns ModuleState::Stopped if the module is not found.
    [[nodiscard]] ModuleState module_state(std::string_view name) const;

    /// Returns the number of registered modules.
    [[nodiscard]] size_t module_count() const { return entries_.size(); }

    /// Returns the number of currently running modules.
    [[nodiscard]] size_t running_count() const;

    /// Returns true if startup_all() has been called and shutdown_all() has not.
    [[nodiscard]] bool is_started() const { return started_; }

    /// Returns the names of all modules in startup order (after startup_all).
    [[nodiscard]] std::vector<const char*> startup_order() const;

private:
    struct Entry {
        ModuleInterface* mod   = nullptr;
        ModuleState      state = ModuleState::Registered;
    };

    std::vector<Entry> entries_;
    bool               started_  = false;
    bool               sorted_   = false;
};

} // namespace uz
