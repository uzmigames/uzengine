#pragma once

/// VFX Module Registry -- factory for creating VFX modules by name.
///
/// Stores factory functions keyed by module name. The VFXSystem owns a single
/// ModuleRegistry instance and uses it to instantiate modules when loading
/// particle effects from assets.
///
/// # Design
/// NOT a singleton -- constructed and owned by VFXSystem. This allows tests
/// to create isolated registries without global state pollution.
///
/// # Thread Safety
/// Not thread-safe. All registration and creation happens on the main thread
/// during system initialization or asset loading.

#include "vfx_module.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// ModuleRegistry -- VFX module factory registry
// ---------------------------------------------------------------------------

/// Factory registry for creating VFX modules by name.
///
/// Modules register themselves (typically during engine init) via `Register<T>()`.
/// At runtime, `Create()` instantiates a module by its registered name. This
/// decouples asset serialization from concrete module types.
///
/// # Examples
/// ```cpp
/// ModuleRegistry registry;
/// registry.Register<GravityModule>("Gravity");
/// registry.Register<DragModule>("Drag");
///
/// auto gravity = registry.Create("Gravity");  // unique_ptr<IVFXModule>
/// ```
class ModuleRegistry {
public:
    /// Factory function signature: returns a new module instance.
    using FactoryFn = std::function<std::unique_ptr<IVFXModule>()>;

    ModuleRegistry() = default;
    ~ModuleRegistry() = default;

    // Non-copyable (owns factory map), movable.
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;
    ModuleRegistry(ModuleRegistry&&) noexcept = default;
    ModuleRegistry& operator=(ModuleRegistry&&) noexcept = default;

    /// Registers a VFX module type under the given name.
    ///
    /// `T` must derive from `IVFXModule` and be default-constructible.
    /// Duplicate registrations log a warning and overwrite the previous factory.
    ///
    /// # Parameters
    /// - `name`: Unique module identifier (e.g., "Gravity"). Must not be null.
    ///
    /// # Side Effects
    /// Logs `VFX/Info` on successful registration, `VFX/Warning` on duplicate.
    template <typename T>
    void Register(const char* name);

    /// Creates a VFX module instance by name.
    ///
    /// # Parameters
    /// - `name`: Registered module name. Must not be null.
    ///
    /// # Returns
    /// - Owning pointer to a new module instance on success.
    /// - `nullptr` if no module is registered under `name`.
    ///
    /// # Side Effects
    /// Logs `VFX/Error` if the name is not found.
    [[nodiscard]] std::unique_ptr<IVFXModule> Create(const char* name) const;

    /// Returns all registered module names in unspecified order.
    ///
    /// # Returns
    /// Vector of module name strings. Empty if nothing is registered.
    [[nodiscard]] std::vector<std::string> GetRegisteredNames() const;

    /// Checks whether a module is registered under the given name.
    ///
    /// # Parameters
    /// - `name`: Module name to look up. Must not be null.
    ///
    /// # Returns
    /// `true` if a factory exists for `name`, `false` otherwise.
    [[nodiscard]] bool IsRegistered(const char* name) const;

private:
    /// Internal factory registration with logging (called by Register<T>).
    /// Separated to keep log.h out of the header.
    void RegisterFactory(const char* name, FactoryFn factory);

    /// Module factory map: name -> factory function.
    std::unordered_map<std::string, FactoryFn> factories_;
};

// ---------------------------------------------------------------------------
// Template implementation (must be in header)
// ---------------------------------------------------------------------------

template <typename T>
void ModuleRegistry::Register(const char* name) {
    static_assert(std::is_base_of_v<IVFXModule, T>,
                  "T must derive from IVFXModule");
    static_assert(std::is_default_constructible_v<T>,
                  "T must be default-constructible");

    // Delegate to .cpp for logging (avoids pulling log.h into the header).
    auto factory = []() -> std::unique_ptr<IVFXModule> {
        return std::make_unique<T>();
    };

    RegisterFactory(name, std::move(factory));
}

} // namespace uz
