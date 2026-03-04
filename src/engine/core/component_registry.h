#pragma once

// ComponentRegistry — decoupled type registration for engine components (XE-2).
//
// Components self-register via DECLARE_COMPONENT() at static init time. The
// registry maps StringId → factory function, enabling O(1) creation by name
// without type switches. This supports data-driven workflows where component
// types are resolved from scene files at load time.
//
// Thread-safety: Registration happens during static init (single-threaded).
// After engine startup, the registry is read-only and safe for concurrent
// access from any thread.

#include "engine/core/string_id.h"

#include <cassert>
#include <cstdio>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace uz {

// Forward declaration — components inherit from IComponent (defined in
// component_interface.h). The registry only needs the opaque pointer.
struct IComponent;

// ============================================================================
// ComponentFactory — function pointer that creates a component instance
// ============================================================================

using ComponentFactory = std::function<IComponent*()>;

// ============================================================================
// ComponentTypeInfo — metadata stored per registered component type
// ============================================================================

struct ComponentTypeInfo {
    StringId         type_id;       // Hashed type name
    const char*      type_name;     // Human-readable name (non-owning)
    ComponentFactory factory;       // Creates a new instance (caller owns)
};

// ============================================================================
// ComponentRegistry — singleton registry of all component types
// ============================================================================

class ComponentRegistry {
public:
    /// Get the global singleton instance.
    [[nodiscard]] static ComponentRegistry& instance();

    /// Register a component type with its factory. Duplicate registration
    /// (same StringId) logs a warning and overwrites the previous entry.
    void register_type(StringId type_id, const char* type_name, ComponentFactory factory);

    /// Create a new component instance by type name hash.
    /// Returns nullptr if the type is not registered.
    [[nodiscard]] IComponent* create(StringId type_id) const;

    /// Look up type info by StringId. Returns nullptr if not found.
    [[nodiscard]] const ComponentTypeInfo* find(StringId type_id) const;

    /// Returns the number of registered component types.
    [[nodiscard]] size_t type_count() const { return types_.size(); }

    /// Returns all registered type names (for debug / editor UI).
    [[nodiscard]] std::vector<const char*> enumerate_types() const;

    /// Check if a type is registered.
    [[nodiscard]] bool has_type(StringId type_id) const;

    /// Clear all registered types (primarily for testing).
    void clear();

private:
    ComponentRegistry() = default;

    std::unordered_map<uint32_t, ComponentTypeInfo> types_;
};

// ============================================================================
// DECLARE_COMPONENT — self-registration macro
// ============================================================================

/// Use in the .cpp file of each component to auto-register it.
/// Example:
///   DECLARE_COMPONENT(MeshComponent)
///
/// Requires the component class to have:
///   - A default constructor
///   - A static constexpr member: static constexpr const char* kTypeName = "MeshComponent";
///
#define DECLARE_COMPONENT(Type)                                                    \
    static bool uz_auto_register_##Type = [] {                                     \
        ::uz::ComponentRegistry::instance().register_type(                         \
            ::uz::string_id(Type::kTypeName),                                      \
            Type::kTypeName,                                                       \
            [] () -> ::uz::IComponent* { return new Type(); }                      \
        );                                                                         \
        return true;                                                               \
    }()

} // namespace uz
