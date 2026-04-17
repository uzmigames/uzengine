#pragma once

/// VFX Scene SDF DI — `IVfxDataInterface` implementation.
///
/// Phase 9.16.0. Wraps `VfxSceneSDFAtlasDesc` + the 8.22.0 HLSL snippet and
/// registers with the phase 10.7.0 `VfxDIRegistry` so emitters using
/// `data_interfaces: ["SceneSDF"]` get the snippet spliced into the
/// composed update CS.
///
/// # Emitted fragments
///
/// 1. **CB fields**: `uint sdfAtlasSrvIdx; float3 sdfWorldMin;
///    float3 sdfWorldMax; uint3 sdfRes;`
/// 2. **Helper functions**: the body returned by `VfxSceneSDFEmitHLSLSnippet`
///    which defines `SampleSDF(worldPos)` + `VfxSceneSDFResolveCollision`.
/// 3. **Call-site snippet**: `if (SampleSDF(position) < 0.0f) {
///    VfxSceneSDFResolveCollision(position, velocity); }` — particles that
///    end up inside scene geometry get pushed back along the gradient.
///
/// @ref engine/vfx/data_interfaces/vfx_scene_sdf_di.h (helpers)
/// @ref engine/vfx/modules/vfx_di_registry.h (base class)

#include "engine/vfx/data_interfaces/vfx_scene_sdf_di.h"
#include "engine/vfx/modules/vfx_di_registry.h"

#include <string>

namespace uz {

/// Concrete DI for the scene SDF atlas.
class VfxSceneSDFDataInterface : public IVfxDataInterface {
public:
    const char* TypeName() const noexcept override { return "SceneSDF"; }

    std::string EmitCBFields() const override {
        return
            "uint   sdfAtlasSrvIdx;\n"
            "float3 sdfWorldMin;\n"
            "float3 sdfWorldMax;\n"
            "uint3  sdfRes;\n";
    }

    std::string EmitHelperFunctions() const override {
        return std::string(VfxSceneSDFEmitHLSLSnippet());
    }

    std::string EmitCallSite() const override {
        return
            "if (sdfAtlasSrvIdx != 0xFFFFFFFFu) {\n"
            "    float sdfDist = SampleSDF(position);\n"
            "    if (sdfDist < 0.0f) {\n"
            "        VfxSceneSDFResolveCollision(position, velocity);\n"
            "    }\n"
            "}\n";
    }
};

} // namespace uz
