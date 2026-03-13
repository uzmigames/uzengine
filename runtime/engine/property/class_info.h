#pragma once

/// UzEngine Class Info
///
/// Describes a reflected class: its name, instance size, list of reflected properties,
/// and an optional placement-new factory function. `ClassInfo` is the unit of
/// registration in the `ClassRegistry`.
///
/// # Ownership
/// `ClassInfo` owns its property list (`std::vector<PropertyInfo>`) and name
/// (`std::string`). It is movable but not copyable to prevent accidental duplication
/// of large property lists.

#include "property_info.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ClassInfo — metadata for a reflected class
// ─────────────────────────────────────────────────────────────────────────────

/// Metadata descriptor for a reflected class registered in the property system.
///
/// Holds the class name, byte size, a flat list of `PropertyInfo` descriptors,
/// and an optional factory function for placement-new construction.
///
/// # Thread Safety
/// Immutable after registration. Multiple threads may read concurrently.
struct ClassInfo {
    /// Fully-qualified class name (e.g., "Enemy", "HealthComponent").
    std::string name;

    /// Flat list of reflected properties belonging to this class.
    std::vector<PropertyInfo> properties;

    /// Placement-new factory: constructs a default instance at the given address.
    ///
    /// # Arguments
    /// * `placement` - Pre-allocated memory of at least `instance_size` bytes.
    ///
    /// # Returns
    /// Pointer to the constructed object (same as `placement`).
    /// `nullptr` if no factory is registered.
    using FactoryFn = void* (*)(void* placement);
    FactoryFn factory = nullptr;

    /// Size of one instance in bytes (from `sizeof(T)`).
    size_t instance_size = 0;

    /// Whether this class is a behavior (tickable component with scripted lifecycle).
    ///
    /// Set to `true` by the `UZ_BEHAVIOR` macro. Regular components registered
    /// with `UZ_COMPONENT` leave this as `false`. Used by `ClassRegistry::ForEachBehavior`
    /// to filter behavior classes from the full registry.
    bool is_behavior = false;

    /// Searches for a property by name (linear scan).
    ///
    /// # Arguments
    /// * `prop_name` - Name to search for (case-sensitive, exact match).
    ///
    /// # Returns
    /// Pointer to the matching `PropertyInfo`, or `nullptr` if not found.
    [[nodiscard]] const PropertyInfo* FindProperty(std::string_view prop_name) const;

    /// Iterates over all properties, calling `fn` for each one.
    ///
    /// # Arguments
    /// * `fn` - Callback invoked once per property. Must not modify the vector.
    void ForEachProperty(const std::function<void(const PropertyInfo&)>& fn) const;

    /// Returns the number of reflected properties.
    [[nodiscard]] size_t PropertyCount() const noexcept;

    /// Convenience method to add a typed property to the list.
    ///
    /// # Template Parameters
    /// * `T` - C++ type of the property (used for `GetPropertyType<T>()` and `sizeof`).
    ///
    /// # Arguments
    /// * `prop_name` - Property name (must be a string literal).
    /// * `byte_offset` - Result of `offsetof(OwnerClass, field)`.
    /// * `desc` - Human-readable description.
    /// * `cat` - Editor category grouping (defaults to empty).
    /// * `flags` - Behavioral flags (defaults to `PropertyFlags::None`).
    template <typename T>
    void AddProperty(const char* prop_name, size_t byte_offset, const char* desc = "",
                     const char* cat = "", PropertyFlags flags = PropertyFlags::None);
};

} // namespace uz
