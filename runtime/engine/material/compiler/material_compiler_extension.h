#pragma once

/// Material compiler extension API — custom HLSL splice injection.
///
/// Phase 10.8.0. Extensions provide `(insertionPoint, hlslSnippet)` pairs
/// that the HLSL material translator splices during codegen. This enables:
///
/// - **WPO (World Position Offset)**: VS-stage vertex deformation.
/// - **Emissive augmentation**: PS-stage emissive colour injection.
/// - **Helper functions**: Shared utility code across splice points.
///
/// # Insertion points
///
/// | Point              | Stage | When                                  |
/// |--------------------|-------|---------------------------------------|
/// | VS_PreViewProj     | VS    | After world-space position, before VP |
/// | VS_PrevFrame       | VS    | Prev-frame position for motion vectors|
/// | PS_PostLighting     | PS    | After surface lighting, before output |
/// | FunctionScope      | Both  | Before main(), for helper functions   |
///
/// # Registration pattern
///
/// ```cpp
/// class WPOExtension : public IMaterialCompilerExtension {
/// public:
///     const char* Name() const noexcept override { return "WPO"; }
///     bool EmitHLSL(InjectionPoint point, std::string& out) const override {
///         if (point == InjectionPoint::VS_PreViewProj) {
///             out = "worldPos.xyz += EvaluateWPO(worldPos.xyz, time);";
///             return true;
///         }
///         return false;
///     }
/// };
/// ```
///
/// @ref UE5 FMaterialAttributes + FMaterialCompiler
/// @ref engine/vfx/modules/vfx_wpo.h (WPO splice emitters)

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Injection points
// ─────────────────────────────────────────────────────────────────────────────

/// Where in the generated shader the extension's HLSL is spliced.
enum class MaterialInjectionPoint : uint8_t {
    /// Vertex shader: after world-space position computation, before
    /// view-projection transform. WPO uses this to offset vertices.
    VS_PreViewProj = 0,

    /// Vertex shader: previous-frame position for motion vector output.
    /// WPO must also deform the prev-frame position for correct TAA/DLSS.
    VS_PrevFrame = 1,

    /// Pixel shader: after surface lighting, before final output.
    /// Used for emissive augmentation, debug visualisation, etc.
    PS_PostLighting = 2,

    /// Function scope: before `main()` in both VS and PS.
    /// Used for helper functions shared across splice points.
    FunctionScope = 3,

    Count
};

/// Returns a stable name for `point` (for logs / diagnostics).
[[nodiscard]] const char* MaterialInjectionPointName(MaterialInjectionPoint point) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Extension interface
// ─────────────────────────────────────────────────────────────────────────────

/// Base class for material compiler extensions. Each extension emits HLSL
/// snippets at one or more injection points.
class IMaterialCompilerExtension {
public:
    virtual ~IMaterialCompilerExtension() = default;

    /// Human-readable name for diagnostics (e.g. "WPO", "Emissive").
    [[nodiscard]] virtual const char* Name() const noexcept = 0;

    /// Emits HLSL for the given injection point.
    ///
    /// # Arguments
    /// - `point` — where in the shader to inject.
    /// - `out`   — receives the HLSL snippet. Left unchanged when the
    ///   extension has nothing to emit at this point.
    ///
    /// # Returns
    /// `true` when `out` was populated, `false` when this extension
    /// doesn't contribute at this point.
    [[nodiscard]] virtual bool EmitHLSL(MaterialInjectionPoint point,
                                         std::string& out) const = 0;

    /// Returns a PSO permutation tag string that the pipeline cache uses
    /// to separate variants with this extension from those without.
    /// Empty string means no PSO separation needed.
    ///
    /// Example: "WPO_ENABLED" → the PSO cache hashes this into the key
    /// so a deformed material gets a separate PSO from the undeformed one.
    [[nodiscard]] virtual std::string PSOPermutationTag() const { return ""; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Extension registry
// ─────────────────────────────────────────────────────────────────────────────

/// Registry of material compiler extensions. The translator queries this
/// during codegen to collect HLSL snippets at each injection point.
class MaterialCompilerExtensionRegistry {
public:
    MaterialCompilerExtensionRegistry() = default;
    ~MaterialCompilerExtensionRegistry() = default;

    MaterialCompilerExtensionRegistry(const MaterialCompilerExtensionRegistry&) = delete;
    MaterialCompilerExtensionRegistry& operator=(const MaterialCompilerExtensionRegistry&) = delete;

    /// Registers an extension. Takes ownership.
    void Register(std::unique_ptr<IMaterialCompilerExtension> ext);

    /// Returns the number of registered extensions.
    [[nodiscard]] uint32_t Count() const noexcept;

    /// Removes all extensions.
    void Clear();

    /// Collects all HLSL snippets at `point` from all registered extensions.
    /// Returns the concatenation (newline-separated). Empty when no extension
    /// contributes at this point.
    [[nodiscard]] std::string CollectHLSL(MaterialInjectionPoint point) const;

    /// Collects all non-empty PSO permutation tags from all extensions.
    [[nodiscard]] std::vector<std::string> CollectPermutationTags() const;

private:
    std::vector<std::unique_ptr<IMaterialCompilerExtension>> extensions_;
};

} // namespace uz
