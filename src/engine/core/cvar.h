#pragma once

// CVar (Console Variable) core types.
//
// CVarEntry stores a named, typed, flagged variable with a default and current
// value. CVar<T> is a lightweight typed accessor that points into a CVarEntry
// owned by a CVarRegistry.

#include <cassert>
#include <cstdint>
#include <string>
#include <variant>

namespace uz {

// ============================================================================
// Flags — bitmask for CVar behavior modifiers.
// ============================================================================

enum CVarFlags : uint32_t {
    CVAR_NONE     = 0,
    CVAR_READONLY = 1 << 0,  // Cannot be changed at runtime via set()
    CVAR_CHEAT    = 1 << 1,  // Requires cheat mode to modify
    CVAR_SAVED    = 1 << 2,  // Persisted to config file
};

// ============================================================================
// CVarType — runtime type tag for the stored value.
// ============================================================================

enum class CVarType : uint8_t {
    BOOL,
    INT,
    FLOAT,
    STRING,
};

// ============================================================================
// CVarValue — the variant holding one of the supported types.
// ============================================================================

using CVarValue = std::variant<bool, int32_t, float, std::string>;

// ============================================================================
// CVarEntry — full descriptor for a single console variable.
// ============================================================================

struct CVarEntry {
    std::string name;
    std::string description;
    uint32_t    flags        = CVAR_NONE;
    CVarType    type         = CVarType::BOOL;
    CVarValue   default_value;
    CVarValue   current_value;
};

// ============================================================================
// CVar<T> — typed, lightweight handle into a CVarEntry.
//
// Does not own the entry — the CVarRegistry owns all entries. The CVar<T> is
// only valid as long as the registry (and the entry it points to) is alive.
// ============================================================================

template<typename T>
class CVar {
    CVarEntry* entry_ = nullptr;

public:
    CVar() noexcept = default;

    explicit CVar(CVarEntry* entry) noexcept : entry_(entry) {}

    /// Read the current value. Asserts that the entry exists and holds type T.
    [[nodiscard]] T get() const {
        assert(entry_ && "CVar::get() called on null entry");
        return std::get<T>(entry_->current_value);
    }

    /// Write a new current value. Returns false if CVAR_READONLY.
    [[nodiscard]] bool set(const T& value) {
        assert(entry_ && "CVar::set() called on null entry");
        if (entry_->flags & CVAR_READONLY) {
            return false;
        }
        entry_->current_value = value;
        return true;
    }

    /// Access the underlying entry (for inspection / debugging).
    [[nodiscard]] const CVarEntry* entry() const noexcept { return entry_; }

    /// True if this CVar points to a valid entry.
    [[nodiscard]] bool is_valid() const noexcept { return entry_ != nullptr; }
};

} // namespace uz
