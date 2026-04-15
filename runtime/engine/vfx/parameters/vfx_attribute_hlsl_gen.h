#pragma once

/// HLSL CB layout generator for `VfxAttributeStore`.
///
/// Given a scope's attribute set, produces a deterministic HLSL `cbuffer`
/// declaration with typed accessor functions so modules can read attributes
/// via stable `Emitter_loopCount()` / `System_intensity()` / etc. stubs.
///
/// The generated layout matches `VfxAttributeStore::PackScopeToBuffer` byte
/// for byte: one `float4` per attribute in insertion order. Scalars occupy
/// `.x`, Vec3 occupies `.xyz`, Vec4 occupies `.xyzw`.
///
/// # Example output (scope = System, two attributes)
/// ```hlsl
/// cbuffer VfxSystemAttributes : register(b8) {
///     float4 _system_intensity;   // was: float
///     float4 _system_windDir;     // was: Vec3
/// };
/// float  System_intensity() { return _system_intensity.x; }
/// float3 System_windDir()   { return _system_windDir.xyz; }
/// ```
///
/// The typed accessor shape is fixed (returns float / float3 / float4 based
/// on the original Set* call). The store currently does not record type
/// metadata per attribute — Phase 7.2.0 will extend this with a type tag
/// once the parameter-store refactor lands. Until then the generator emits
/// `float4` accessors by convention.
///
/// @ref UE5 NiagaraShaderParametersMetadata.cpp — equivalent Niagara layout
/// generator. This implementation is intentionally stripped down to the
/// minimum needed for 7.1.0's reference port of `emitterDuration`,
/// `loopCount`, and `vfxTotalTime` into the System scope.

#include "vfx_attribute_store.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace uz {

/// Configuration for a single generated CB block.
struct VfxAttributeHlslGenConfig {
    /// Scope whose attributes populate the CB.
    VfxAttributeScope scope = VfxAttributeScope::System;

    /// HLSL `register(bN)` slot. Defaults follow the convention:
    ///   - Particles: b9 (read-only in update CS; most data comes from SoA)
    ///   - Emitter:   b10
    ///   - System:    b8
    /// Callers can override if a specific pass needs a different slot.
    uint32_t registerSlot = 0xFFFFFFFFu;

    /// Optional CB struct name override. If empty, a default is synthesised
    /// as `Vfx<Scope>Attributes` (e.g. `VfxSystemAttributes`).
    std::string structName;
};

/// Resolves the default `register(bN)` slot for a scope when the caller
/// does not explicitly set one.
[[nodiscard]] uint32_t VfxAttributeDefaultRegisterSlot(VfxAttributeScope scope);

/// Generates an HLSL `cbuffer` declaration + float4 accessors covering all
/// attributes currently set in `store[scope]`. Accessor names follow the
/// canonical `<Scope>_<name>()` form.
///
/// The output is deterministic across runs for a given scope-insertion
/// order so cache keys hashed on the generated text stay stable.
///
/// # Parameters
/// - `store`: store to enumerate.
/// - `cfg`: scope + register slot + optional struct name.
///
/// # Returns
/// HLSL source text. Empty if the scope has no attributes set.
[[nodiscard]] std::string GenerateVfxAttributeCbuffer(
    const VfxAttributeStore& store,
    const VfxAttributeHlslGenConfig& cfg);

} // namespace uz
