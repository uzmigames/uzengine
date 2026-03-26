#include "cvar.h"

/// Console variable (CVar) registry implementation.
///
/// Manages registration, lookup, and mutation of floating-point configuration variables
/// with min/max bounds enforcement and default value tracking.

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CVarRegistry — Singleton
// ─────────────────────────────────────────────────────────────────────────────

CVarRegistry& CVarRegistry::Get() {
    static CVarRegistry instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

CVar* CVarRegistry::Register(const std::string& name, float defaultValue,
                             const std::string& description,
                             float minValue, float maxValue) {
    // Idempotent: if already registered, return the existing CVar unchanged.
    auto it = m_cvars.find(name);
    if (it != m_cvars.end()) {
        return &it->second;
    }

    // Clamp the default value to the declared range so invariants hold
    // from the moment of registration.
    const float clampedDefault = std::clamp(defaultValue, minValue, maxValue);

    auto [inserted, success] = m_cvars.emplace(
        name,
        CVar{name, description, clampedDefault, clampedDefault, minValue, maxValue}
    );

    return &inserted->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lookup
// ─────────────────────────────────────────────────────────────────────────────

CVar* CVarRegistry::Find(const std::string& name) {
    auto it = m_cvars.find(name);
    if (it == m_cvars.end()) {
        return nullptr;
    }
    return &it->second;
}

const CVar* CVarRegistry::Find(const std::string& name) const {
    auto it = m_cvars.find(name);
    if (it == m_cvars.end()) {
        return nullptr;
    }
    return &it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// Typed getters
// ─────────────────────────────────────────────────────────────────────────────

float CVarRegistry::GetFloat(const std::string& name, float defaultIfMissing) const {
    const CVar* cv = Find(name);
    if (!cv) {
        return defaultIfMissing;
    }
    return cv->value;
}

int CVarRegistry::GetInt(const std::string& name, int defaultIfMissing) const {
    const CVar* cv = Find(name);
    if (!cv) {
        return defaultIfMissing;
    }
    return static_cast<int>(cv->value);
}

bool CVarRegistry::GetBool(const std::string& name, bool defaultIfMissing) const {
    const CVar* cv = Find(name);
    if (!cv) {
        return defaultIfMissing;
    }
    return cv->value >= 0.5f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mutation
// ─────────────────────────────────────────────────────────────────────────────

void CVarRegistry::Set(const std::string& name, float value) {
    CVar* cv = Find(name);
    if (!cv) {
        return;
    }
    cv->value = std::clamp(value, cv->minValue, cv->maxValue);
}

void CVarRegistry::Reset(const std::string& name) {
    CVar* cv = Find(name);
    if (!cv) {
        return;
    }
    cv->value = cv->defaultValue;
}

void CVarRegistry::ResetAll() {
    for (auto& [key, cv] : m_cvars) {
        cv.value = cv.defaultValue;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Enumeration
// ─────────────────────────────────────────────────────────────────────────────

const std::unordered_map<std::string, CVar>& CVarRegistry::GetAll() const {
    return m_cvars;
}

} // namespace uz
