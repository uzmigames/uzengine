#pragma once

/// Scoped VFX attribute store (Niagara-parity namespaced attributes).
///
/// Provides three isolated parameter scopes — `Particles`, `Emitter`, and
/// `System` — each backed by its own `VFXParameterStore`. Modules bind to
/// attributes at the right scope so the same HLSL snippet composes across
/// per-particle, per-emitter, and system-wide values.
///
/// This is the foundation for:
///   - phase7_7.2.0 (system-level parameter store): writes to the `System`
///     scope get flushed into system CBs each frame.
///   - phase7_7.3.0 (event pipeline): receivers resolve binding references
///     against the appropriate scope.
///   - phase7_7.12.0 (particle-read DI): cross-emitter reads look up values
///     by `(emitter, scope, name)` triples.
///
/// # Scopes
///
/// | Scope        | Lifetime              | Typical usage                    |
/// |--------------|-----------------------|----------------------------------|
/// | `Particles`  | Per-particle per-tick | Position, velocity, color ramps  |
/// | `Emitter`    | Per-emitter per-frame | LoopCount, emissionRate, spread  |
/// | `System`     | Per-system per-frame  | Global intensity, team color     |
///
/// # Example
/// ```cpp
/// VfxAttributeStore store;
/// store.SetFloat(VfxAttributeScope::System, "intensity", 2.0f);
/// float g = store.GetFloat(VfxAttributeScope::System, "intensity", 1.0f);
/// ```
///
/// @ref UE5 NiagaraDataSet.h — FNiagaraVariable + namespace enum
/// @ref UE5 NiagaraParameterStore.h — FNiagaraParameterStore

#include "vfx_parameter_store.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VfxAttributeScope — Niagara-parity namespace enum.
// ─────────────────────────────────────────────────────────────────────────────

/// Scoped namespace for VFX attributes. Identical layout and semantics to
/// Niagara's `ENiagaraParameterScopeInfoID` for the subset relevant to
/// runtime parameter binding.
enum class VfxAttributeScope : uint8_t {
    /// Per-particle attribute (e.g. `Particles.Position`, `Particles.Velocity`).
    /// Stored in SoA buffers; the store here only carries defaults/initial values.
    Particles = 0,

    /// Per-emitter attribute (e.g. `Emitter.LoopCount`, `Emitter.SpawnRate`).
    /// One value per emitter instance; flushed into the emitter CB each frame.
    Emitter = 1,

    /// Per-system attribute (e.g. `System.Age`, `System.Intensity`).
    /// Shared across all emitters of one `VfxSystemInstance`; flushed into the
    /// system CB each frame.
    System = 2,
};

/// Number of scopes (used internally for fixed-size arrays).
inline constexpr std::size_t kVfxAttributeScopeCount = 3;

/// Returns a stable string representation of the scope (`"Particles"`,
/// `"Emitter"`, `"System"`). Used for logging and JSON serialisation.
[[nodiscard]] std::string_view VfxAttributeScopeToString(VfxAttributeScope scope);

// ─────────────────────────────────────────────────────────────────────────────
// VfxAttributeKey — (scope, name) tuple identifying a single attribute.
// ─────────────────────────────────────────────────────────────────────────────

/// Fully qualified attribute identifier. Equality compares both scope and
/// name, so `Particles.Position` and `Emitter.Position` are distinct keys.
struct VfxAttributeKey {
    VfxAttributeScope scope = VfxAttributeScope::Particles;
    std::string       name;

    [[nodiscard]] bool operator==(const VfxAttributeKey& other) const {
        return scope == other.scope && name == other.name;
    }
    [[nodiscard]] bool operator!=(const VfxAttributeKey& other) const {
        return !(*this == other);
    }

    /// Returns the canonical string form `"<Scope>.<name>"` used in JSON and
    /// HLSL accessor naming (e.g. `"System.intensity"`).
    [[nodiscard]] std::string ToString() const;

    /// Parses a canonical `"<Scope>.<name>"` string into a key. Returns `true`
    /// on success; `false` if the prefix is unknown or the dot is missing.
    static bool FromString(std::string_view canonical, VfxAttributeKey& outKey);
};

// ─────────────────────────────────────────────────────────────────────────────
// VfxAttributeStore — three-scope attribute container.
// ─────────────────────────────────────────────────────────────────────────────

/// Container owning one `VFXParameterStore` per scope. Provides scope-qualified
/// Set/Get and produces deterministic CB layouts per scope (for 7.2.0 system
/// CB + future per-emitter / per-particle layouts).
///
/// # Thread safety
/// Not thread-safe. All access must occur on the main thread or be externally
/// synchronised.
///
/// # Ownership
/// Designed to be owned by `VfxSystemInstance` (one store per instance) or by
/// `ParticleEmitter` (one store per emitter) depending on the intended scope
/// breadth. Callers can mix: a system-level store carries `System` attributes,
/// each emitter carries its own `Emitter` + `Particles` defaults.
class VfxAttributeStore {
public:
    // ── Typed setters (pass-through to underlying scope store) ──────────────

    void SetFloat(VfxAttributeScope scope, const std::string& name, float value);
    void SetVec3 (VfxAttributeScope scope, const std::string& name, const Vec3& value);
    void SetVec4 (VfxAttributeScope scope, const std::string& name, const Vec4& value);

    // ── Typed getters ──────────────────────────────────────────────────────

    [[nodiscard]] float GetFloat(VfxAttributeScope scope, const std::string& name,
                                 float defaultValue = 0.0f) const;
    [[nodiscard]] Vec3  GetVec3 (VfxAttributeScope scope, const std::string& name,
                                 const Vec3& defaultValue = {}) const;
    [[nodiscard]] Vec4  GetVec4 (VfxAttributeScope scope, const std::string& name,
                                 const Vec4& defaultValue = {}) const;

    // ── Presence + enumeration ─────────────────────────────────────────────

    [[nodiscard]] bool   Has(VfxAttributeScope scope, const std::string& name) const;
    [[nodiscard]] bool   Remove(VfxAttributeScope scope, const std::string& name);
    [[nodiscard]] size_t GetCount(VfxAttributeScope scope) const noexcept;
    [[nodiscard]] std::vector<std::string> GetNames(VfxAttributeScope scope) const;

    /// Clears a single scope. Other scopes are untouched.
    void Clear(VfxAttributeScope scope);

    /// Clears every scope.
    void ClearAll();

    // ── CB layout + upload ─────────────────────────────────────────────────

    /// Returns the upload-buffer size in bytes for the given scope.
    /// Every attribute occupies 16 bytes (one Vec4).
    [[nodiscard]] uint32_t GetUploadSize(VfxAttributeScope scope) const noexcept;

    /// Packs a single scope's attributes into a contiguous buffer for GPU
    /// upload. The layout matches `VFXParameterStore::PackToBuffer` (insertion
    /// order, one Vec4 per attribute) so scope CBs are deterministic across
    /// runs as long as the insertion order is stable.
    ///
    /// # Returns
    /// Number of bytes written, or 0 if the buffer is too small.
    uint32_t PackScopeToBuffer(VfxAttributeScope scope,
                               void* outBuffer,
                               uint32_t bufferSize) const;

    // ── Key-qualified convenience API ──────────────────────────────────────

    /// Typed set taking a full `VfxAttributeKey`. Equivalent to
    /// `SetFloat(key.scope, key.name, value)`.
    void SetFloat(const VfxAttributeKey& key, float value) {
        SetFloat(key.scope, key.name, value);
    }
    void SetVec3(const VfxAttributeKey& key, const Vec3& value) {
        SetVec3(key.scope, key.name, value);
    }
    void SetVec4(const VfxAttributeKey& key, const Vec4& value) {
        SetVec4(key.scope, key.name, value);
    }

    /// Typed get taking a full `VfxAttributeKey`.
    [[nodiscard]] float GetFloat(const VfxAttributeKey& key,
                                 float defaultValue = 0.0f) const {
        return GetFloat(key.scope, key.name, defaultValue);
    }
    [[nodiscard]] Vec3 GetVec3(const VfxAttributeKey& key,
                               const Vec3& defaultValue = {}) const {
        return GetVec3(key.scope, key.name, defaultValue);
    }
    [[nodiscard]] Vec4 GetVec4(const VfxAttributeKey& key,
                               const Vec4& defaultValue = {}) const {
        return GetVec4(key.scope, key.name, defaultValue);
    }

private:
    [[nodiscard]] VFXParameterStore& StoreFor(VfxAttributeScope scope);
    [[nodiscard]] const VFXParameterStore& StoreFor(VfxAttributeScope scope) const;

    /// One parameter store per scope. Index order matches `VfxAttributeScope`.
    std::array<VFXParameterStore, kVfxAttributeScopeCount> scopes_;
};

} // namespace uz
