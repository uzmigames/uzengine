#pragma once

/// WPO (World Position Offset) material compiler extension.
///
/// Phase 9.13.0. Concrete `IMaterialCompilerExtension` that wraps the
/// phase 8.18.0 `VfxWPO*` splice helpers and registers them with the
/// phase 10.8.0 extension API. Materials with an authored WPO expression
/// register this extension at compile time; the compiler then emits the
/// `EvaluateWPO` function at function scope and splices VS calls at
/// VS_PreViewProj (current frame) and VS_PrevFrame (motion vectors).
///
/// # Wiring pattern
///
/// ```cpp
/// MaterialCompilerExtensionRegistry registry;
/// if (material.wpo.IsEnabled()) {
///     registry.Register(std::make_unique<WPOMaterialExtension>(material.wpo));
/// }
/// // … compiler walks registry.CollectHLSL(point) at each injection point.
/// ```
///
/// @ref engine/material/compiler/material_compiler_extension.h (base API)
/// @ref engine/vfx/modules/vfx_wpo.h (splice helpers)
/// @ref docs/engine/25-vfx/35-world-position-offset.md

#include "engine/material/compiler/material_compiler_extension.h"
#include "engine/vfx/modules/vfx_wpo.h"

#include <string>

namespace uz {

/// Material compiler extension that injects WPO VS deformation.
class WPOMaterialExtension : public IMaterialCompilerExtension {
public:
    /// Constructs the extension with a WPO attribute set.
    ///
    /// When `attrs.IsEnabled()` is false, the extension still registers
    /// but emits no-op splices at every point (and no permutation tag),
    /// so the compiler can unconditionally register one extension per
    /// material.
    explicit WPOMaterialExtension(VfxWPOMaterialAttributes attrs)
        : attrs_(std::move(attrs)) {}

    const char* Name() const noexcept override { return "WPO"; }

    bool EmitHLSL(MaterialInjectionPoint point, std::string& out) const override;

    /// Returns `"WPO_ENABLED"` when WPO is active; empty otherwise.
    std::string PSOPermutationTag() const override {
        return attrs_.IsEnabled() ? std::string("WPO_ENABLED") : std::string{};
    }

    /// Returns the wrapped attributes (for tests / inspection).
    [[nodiscard]] const VfxWPOMaterialAttributes& Attributes() const noexcept {
        return attrs_;
    }

private:
    VfxWPOMaterialAttributes attrs_;
};

} // namespace uz
