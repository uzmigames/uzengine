#pragma once

/// VFX Advection Field DI — `IVfxDataInterface` implementation.
///
/// Phase 9.15.0. Wraps `VfxAdvectionFieldConfig` + the 8.21.0 HLSL snippet
/// and registers with the phase 10.7.0 `VfxDIRegistry` so emitters using
/// `data_interfaces: ["Advection"]` get the snippet spliced into the
/// composed update CS.
///
/// # Emitted fragments
///
/// 1. **CB fields**: `uint advectionFieldSrvIdx; float3 advectionWorldMin;
///    float3 advectionWorldMax; float advectionTightness;`
/// 2. **Helper functions**: the body returned by `VfxAdvectionEmitHLSLSnippet`
///    which defines `SampleAdvection(worldPos)`.
/// 3. **Call-site snippet**: `velocity = lerp(velocity,
///    SampleAdvection(position), advectionTightness);`
///
/// @ref engine/vfx/data_interfaces/vfx_advection_field_di.h (helpers)
/// @ref engine/vfx/modules/vfx_di_registry.h (base class)

#include "engine/vfx/data_interfaces/vfx_advection_field_di.h"
#include "engine/vfx/modules/vfx_di_registry.h"

#include <string>

namespace uz {

/// Concrete DI for the advection field.
class VfxAdvectionFieldDataInterface : public IVfxDataInterface {
public:
    const char* TypeName() const noexcept override { return "Advection"; }

    std::string EmitCBFields() const override {
        return
            "uint   advectionFieldSrvIdx;\n"
            "float3 advectionWorldMin;\n"
            "float3 advectionWorldMax;\n"
            "float  advectionTightness;\n";
    }

    std::string EmitHelperFunctions() const override {
        // Return the snippet the 8.21.0 helper emits.
        return std::string(VfxAdvectionEmitHLSLSnippet());
    }

    std::string EmitCallSite() const override {
        // The snippet's SampleAdvection returns float3; blend with current
        // velocity by tightness factor. Short-circuit when the SRV is unbound.
        return
            "if (advectionFieldSrvIdx != 0xFFFFFFFFu) {\n"
            "    float3 advected = SampleAdvection(position);\n"
            "    velocity = lerp(velocity, advected, advectionTightness);\n"
            "}\n";
    }
};

} // namespace uz
