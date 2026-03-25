#pragma once

/// Console Variable (CVar) System
///
/// Provides a global registry of named float/int/bool variables for
/// runtime-configurable engine parameters. CVars are identified by
/// dot-separated names (e.g., "r.ScreenPercentage", "r.Shadow.MaxResolution").
///
/// Simplified console variable manager: no command callbacks, no thread
///         safety guarantees, no auto-registration macros. Render-thread only.
///
/// # Usage
/// ```cpp
/// // Register at startup
/// auto* cv = uz::CVarRegistry::Get().Register("r.ScreenPercentage", 100.f,
///                                              "Render resolution scale", 10.f, 200.f);
///
/// // Read each frame (poll, no callbacks)
/// float pct = uz::CVarRegistry::Get().GetFloat("r.ScreenPercentage");
///
/// // Set from console/Lua
/// uz::CVarRegistry::Get().Set("r.ScreenPercentage", 67.f);
/// ```

#include <cfloat>
#include <string>
#include <unordered_map>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CVar — A single console variable
// ─────────────────────────────────────────────────────────────────────────────

/// A single console variable storing a float value.
///
/// Booleans and integers are stored as float and retrieved via typed accessors.
/// The value is always clamped to [minValue, maxValue] on write.
struct CVar {
    std::string name;          ///< Dot-separated name (e.g., "r.ScreenPercentage")
    std::string description;   ///< Help text shown in console/debug UI
    float       value;         ///< Current value (ints/bools stored as float)
    float       defaultValue;  ///< Value used by Reset()
    float       minValue;      ///< Minimum allowed value (inclusive)
    float       maxValue;      ///< Maximum allowed value (inclusive)
};

// ─────────────────────────────────────────────────────────────────────────────
// CVarRegistry — Global name→CVar map
// ─────────────────────────────────────────────────────────────────────────────

/// Global CVar registry — singleton accessed via CVarRegistry::Get().
///
/// Simplified console variable manager: no command callbacks, no thread
///         safety, no auto-registration macros. Just a name-to-value map.
///
/// NOT thread-safe. All access must occur on the render/game thread.
/// Callers poll values each frame; there is no callback/notification system.
class CVarRegistry {
public:
    /// Returns the process-wide singleton instance.
    ///
    /// The instance is created on first call (Meyer's singleton).
    static CVarRegistry& Get();

    /// Registers a new CVar with the given name and default value.
    ///
    /// If a CVar with the same name already exists, the existing CVar is
    /// returned unchanged (idempotent registration).
    ///
    /// @param name           Dot-separated identifier (e.g., "r.Shadow.Quality")
    /// @param defaultValue   Initial and reset value
    /// @param description    Human-readable help text
    /// @param minValue       Minimum clamp bound (inclusive)
    /// @param maxValue       Maximum clamp bound (inclusive)
    /// @returns Pointer to the registered (or pre-existing) CVar. Never null.
    ///          The pointer remains valid until the registry is destroyed.
    CVar* Register(const std::string& name, float defaultValue,
                   const std::string& description = "",
                   float minValue = -FLT_MAX, float maxValue = FLT_MAX);

    /// Finds a CVar by exact name match.
    ///
    /// @returns Pointer to the CVar, or nullptr if no CVar with that name exists.
    [[nodiscard]] CVar*       Find(const std::string& name);
    /// Const overload — returns a const pointer, or nullptr if not found.
    [[nodiscard]] const CVar* Find(const std::string& name) const;

    /// Reads a CVar's value as float.
    ///
    /// @param name             CVar name to look up
    /// @param defaultIfMissing Value returned if the CVar is not registered
    [[nodiscard]] float GetFloat(const std::string& name, float defaultIfMissing = 0.f) const;

    /// Reads a CVar's value as int (truncated via static_cast).
    ///
    /// @param name             CVar name to look up
    /// @param defaultIfMissing Value returned if the CVar is not registered
    [[nodiscard]] int GetInt(const std::string& name, int defaultIfMissing = 0) const;

    /// Reads a CVar's value as bool (true if value >= 0.5f).
    ///
    /// @param name             CVar name to look up
    /// @param defaultIfMissing Value returned if the CVar is not registered
    [[nodiscard]] bool GetBool(const std::string& name, bool defaultIfMissing = false) const;

    /// Sets a CVar's value, clamping to its [minValue, maxValue] range.
    ///
    /// No-op if no CVar with the given name is registered.
    void Set(const std::string& name, float value);

    /// Resets a CVar to its default value.
    ///
    /// No-op if no CVar with the given name is registered.
    void Reset(const std::string& name);

    /// Resets every registered CVar to its default value.
    void ResetAll();

    /// Returns the internal map for enumeration (debug UI, serialization).
    ///
    /// The returned reference is valid as long as the registry exists.
    /// Do not modify the map through this reference.
    [[nodiscard]] const std::unordered_map<std::string, CVar>& GetAll() const;

private:
    CVarRegistry() = default;
    ~CVarRegistry() = default;

    CVarRegistry(const CVarRegistry&) = delete;
    CVarRegistry& operator=(const CVarRegistry&) = delete;
    CVarRegistry(CVarRegistry&&) = delete;
    CVarRegistry& operator=(CVarRegistry&&) = delete;

    std::unordered_map<std::string, CVar> m_cvars;
};

} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// Auto-Registration Macros (UE5-style decentralized CVar declaration)
// ─────────────────────────────────────────────────────────────────────────────
//
// Declare static CVars in .cpp files — they self-register at static init time.
// Each subsystem owns its CVars. No central RegisterAll() needed.
//
// Usage (in a .cpp file, file scope):
//   static auto* cv_BloomThreshold = UZ_CVAR_FLOAT("r.Bloom.Threshold", -1.f,
//       "Bloom luminance threshold", -1.f, 100.f);
//
//   // In the pass update:
//   settings.threshold = cv_BloomThreshold->value;
//
// NEVER declare in .h files — causes duplicate registration across TUs.

/// Registers a float CVar and returns a stable CVar* pointer.
/// Safe to call at static init time (CVarRegistry uses Meyer's singleton).
#define UZ_CVAR_FLOAT(name, defaultVal, desc, minVal, maxVal) \
    uz::CVarRegistry::Get().Register(name, defaultVal, desc, minVal, maxVal)

/// Registers a bool CVar (stored as float: 0.0=false, 1.0=true).
#define UZ_CVAR_BOOL(name, defaultVal, desc) \
    uz::CVarRegistry::Get().Register(name, (defaultVal) ? 1.f : 0.f, desc, 0.f, 1.f)

/// Registers an int CVar (stored as float, clamped to [min, max]).
#define UZ_CVAR_INT(name, defaultVal, desc, minVal, maxVal) \
    uz::CVarRegistry::Get().Register(name, static_cast<float>(defaultVal), desc, \
                                     static_cast<float>(minVal), static_cast<float>(maxVal))
