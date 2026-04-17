#pragma once

/// VFX Data Interface registry — HLSL injection into composed shaders.
///
/// Phase 10.7.0. Data Interfaces (DIs) are GPU-side resources that VFX
/// modules can read during simulation. Unlike modules, which contribute
/// a per-particle update snippet, DIs contribute:
///
///   1. **CB fields** — constants injected into the composed CB header
///      (e.g. `float3 windDir; float windSpeed;`).
///   2. **Helper functions** — HLSL functions emitted before `main`
///      (e.g. `float3 SampleAdvection(float3 pos) { ... }`).
///   3. **Call-site snippet** — one-liner injected into the per-particle
///      dispatch loop (e.g. `velocity += SampleAdvection(position);`).
///
/// The compositor queries the DI registry when building a composed
/// shader and emits all three fragments for every DI referenced by
/// the emitter asset's `data_interfaces[]` array.
///
/// # Registration
///
/// ```cpp
/// VfxDIRegistry diReg;
/// diReg.Register("Advection", std::make_unique<VfxAdvectionDI>());
/// diReg.Register("SceneSDF", std::make_unique<VfxSceneSDFDI>());
/// ```
///
/// # Shader composition query
///
/// ```cpp
/// // Inside ModuleCompositor::ComposeUpdateShader:
/// for (const auto& diName : emitterAsset.dataInterfaces) {
///     if (auto* di = diReg.Find(diName)) {
///         header += di->EmitCBFields();
///         funcs  += di->EmitHelperFunctions();
///         body   += di->EmitCallSite();
///     }
/// }
/// ```
///
/// @ref UE5 UNiagaraDataInterface + FNiagaraScriptCompiler
/// @ref docs/engine/25-vfx/

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IVfxDataInterface — base class for DI HLSL emission
// ─────────────────────────────────────────────────────────────────────────────

/// Base class for VFX Data Interfaces. Each DI emits three HLSL fragments
/// that the module compositor splices into the composed compute shader.
class IVfxDataInterface {
public:
    virtual ~IVfxDataInterface() = default;

    /// Returns the DI's type name (e.g. "Advection", "SceneSDF").
    /// Used as the key in the registry and the `data_interfaces[]` array
    /// in emitter assets.
    [[nodiscard]] virtual const char* TypeName() const noexcept = 0;

    /// Emits HLSL constant-buffer field declarations to be spliced into
    /// the CB header block of the composed shader.
    ///
    /// Example return:
    /// ```hlsl
    /// float3 advectionDir;
    /// float  advectionStrength;
    /// ```
    [[nodiscard]] virtual std::string EmitCBFields() const = 0;

    /// Emits HLSL helper function definitions to be placed before `main`.
    ///
    /// Example return:
    /// ```hlsl
    /// float3 SampleAdvection(float3 pos) {
    ///     return advectionDir * advectionStrength;
    /// }
    /// ```
    [[nodiscard]] virtual std::string EmitHelperFunctions() const = 0;

    /// Emits the HLSL call-site snippet to be inserted inside the
    /// per-particle dispatch loop, after module snippets.
    ///
    /// Example return:
    /// ```hlsl
    /// velocity += SampleAdvection(position) * dt;
    /// ```
    [[nodiscard]] virtual std::string EmitCallSite() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// VfxDIRegistry — maps type names to DI instances
// ─────────────────────────────────────────────────────────────────────────────

/// Registry of VFX Data Interfaces. Scoped to a ModuleCompositor or
/// a VFX subsystem instance — not a global singleton.
class VfxDIRegistry {
public:
    VfxDIRegistry() = default;
    ~VfxDIRegistry() = default;

    VfxDIRegistry(const VfxDIRegistry&)            = delete;
    VfxDIRegistry& operator=(const VfxDIRegistry&) = delete;

    /// Registers a DI by its `TypeName()`. Overwrites if a DI with the
    /// same name was already registered. Takes ownership of the instance.
    void Register(std::unique_ptr<IVfxDataInterface> di);

    /// Looks up a DI by type name. Returns nullptr when not found.
    [[nodiscard]] IVfxDataInterface* Find(const std::string& typeName) const;

    /// Returns the number of registered DIs.
    [[nodiscard]] uint32_t Count() const noexcept;

    /// Removes all registered DIs.
    void Clear();

    /// Returns the names of all registered DIs (for diagnostics / tests).
    [[nodiscard]] std::vector<std::string> RegisteredNames() const;

    /// Emits the combined HLSL for all DIs referenced by an emitter's
    /// `data_interfaces[]` array. Convenience for the compositor.
    ///
    /// # Arguments
    /// - `diNames` — DI type names from the emitter asset.
    ///
    /// # Returns
    /// A struct with the three concatenated HLSL fragments.
    struct ComposedDIFragments {
        std::string cbFields;
        std::string helperFunctions;
        std::string callSite;
    };

    [[nodiscard]] ComposedDIFragments ComposeForEmitter(
        const std::vector<std::string>& diNames) const;

private:
    std::unordered_map<std::string, std::unique_ptr<IVfxDataInterface>> dis_;
};

} // namespace uz
