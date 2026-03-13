#pragma once

/// UzEngine Class Registry
///
/// Singleton registry that maps class names to their `ClassInfo` descriptors.
/// Components and other reflected types auto-register here via the `UZ_COMPONENT` macro
/// before `main()` runs (static initialization).
///
/// # Thread Safety
/// Registration happens during static initialization (single-threaded).
/// After `main()` begins, the registry is read-only and safe for concurrent reads.
/// Do not register new classes after engine startup.

#include "class_info.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ClassRegistry — central type registry (singleton)
// ─────────────────────────────────────────────────────────────────────────────

/// Global registry of all reflected classes in the engine.
///
/// Classes register themselves via `UZ_COMPONENT` macro during static initialization.
/// At runtime, the registry provides name-based lookup for serialization, editor
/// inspection, and scripting bindings.
///
/// # Singleton Access
/// ```cpp
/// auto& reg = ClassRegistry::Get();
/// const ClassInfo* info = reg.FindClass("Enemy");
/// ```
class ClassRegistry {
public:
    /// Returns the global singleton instance.
    ///
    /// # Returns
    /// Reference to the single ClassRegistry. Guaranteed to exist for the
    /// lifetime of the process (Meyers singleton with static local).
    [[nodiscard]] static ClassRegistry& Get();

    /// Registers a class by name. Overwrites any previous registration with the same name.
    ///
    /// # Arguments
    /// * `type_name` - Unique class name (typically from `#ClassName` in UZ_COMPONENT).
    /// * `info` - Reference to the ClassInfo to store. The registry holds a pointer
    ///            to the original `ClassInfo`, which must have static lifetime.
    ///
    /// # Logging
    /// Logs `Property::Info` on successful registration.
    /// Logs `Property::Warning` if overwriting an existing registration.
    void Register(std::string_view type_name, ClassInfo& info);

    /// Looks up a class by name (mutable access).
    ///
    /// # Arguments
    /// * `type_name` - Class name to search for.
    ///
    /// # Returns
    /// Pointer to the `ClassInfo`, or `nullptr` if not found.
    [[nodiscard]] ClassInfo* FindClass(std::string_view type_name);

    /// Looks up a class by name (const access).
    [[nodiscard]] const ClassInfo* FindClass(std::string_view type_name) const;

    /// Checks whether a class name is registered.
    [[nodiscard]] bool IsRegistered(std::string_view type_name) const;

    /// Iterates over all registered classes, calling `fn` for each.
    ///
    /// # Arguments
    /// * `fn` - Callback receiving (name, ClassInfo&) for each registered class.
    ///          Iteration order is unspecified.
    void ForEachClass(const std::function<void(std::string_view name, const ClassInfo&)>& fn) const;

    /// Iterates over all registered classes that have `is_behavior == true`.
    ///
    /// # Arguments
    /// * `fn` - Callback receiving (name, ClassInfo&) for each behavior class.
    ///          Iteration order is unspecified.
    void ForEachBehavior(const std::function<void(std::string_view name, const ClassInfo&)>& fn) const;

    /// Returns the number of registered classes.
    [[nodiscard]] size_t ClassCount() const noexcept;

    // Non-copyable, non-movable singleton.
    ClassRegistry(const ClassRegistry&) = delete;
    ClassRegistry& operator=(const ClassRegistry&) = delete;

private:
    ClassRegistry() = default;

    /// Maps class name -> pointer to ClassInfo (static lifetime, owned by UZ_COMPONENT).
    std::unordered_map<std::string, ClassInfo*> registry_;
};

} // namespace uz
