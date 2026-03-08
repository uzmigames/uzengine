#pragma once

// CVarRegistry — non-singleton registry that owns all CVarEntry instances.
//
// Designed for testability: create one instance per test, or one per engine.
// No global state, no static initializers.

#include "engine/core/cvar.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class CVarRegistry {
    // Entries are stored in an unordered_map keyed by name. Pointers into
    // this map remain stable because unordered_map does not invalidate
    // references/pointers on insert (only on rehash, but we never erase).
    std::unordered_map<std::string, CVarEntry> entries_;

public:
    CVarRegistry() = default;

    // Non-copyable, non-movable — pointers to entries must remain stable.
    CVarRegistry(const CVarRegistry&) = delete;
    CVarRegistry& operator=(const CVarRegistry&) = delete;
    CVarRegistry(CVarRegistry&&) = delete;
    CVarRegistry& operator=(CVarRegistry&&) = delete;

    // -- Registration -------------------------------------------------------

    /// Register a new CVar. Returns a pointer to the entry on success,
    /// or nullptr if a CVar with the same name already exists.
    CVarEntry* register_cvar(const std::string& name,
                             const std::string& description,
                             CVarValue default_value,
                             uint32_t flags = CVAR_NONE);

    // -- Typed access -------------------------------------------------------

    /// Get the current value of a CVar by name. Asserts on wrong type or
    /// missing name.
    template<typename T>
    [[nodiscard]] T get(const std::string& name) const {
        auto it = entries_.find(name);
        assert(it != entries_.end() && "CVarRegistry::get() — name not found");
        return std::get<T>(it->second.current_value);
    }

    /// Set the current value of a CVar by name. Returns false if the CVar
    /// is CVAR_READONLY or does not exist.
    template<typename T>
    bool set(const std::string& name, const T& value) {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            return false;
        }
        if (it->second.flags & CVAR_READONLY) {
            return false;
        }
        it->second.current_value = value;
        return true;
    }

    // -- Lookup -------------------------------------------------------------

    /// Find a CVar by name. Returns nullptr if not found.
    [[nodiscard]] const CVarEntry* find_by_name(const std::string& name) const;

    /// Non-const overload for internal use (e.g., CVar<T> creation).
    [[nodiscard]] CVarEntry* find_by_name_mut(const std::string& name);

    /// Return all registered CVars (unordered).
    [[nodiscard]] std::vector<const CVarEntry*> get_all() const;
};

} // namespace uz
