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
///    (`SampleSceneSDF` + `GradientSceneSDF`) followed by the DI-level
///    helpers the call site uses: `SampleSDF(worldPos)` and
///    `VfxSceneSDFResolveCollision(position, velocity)`. The latter is the
///    HLSL mirror of the CPU reference `uz::VfxSceneSDFResolveCollision`
///    (push-out along the normalised gradient, `(1 + e)` normal-velocity
///    reflection, world-Z fallback for a degenerate gradient). The gradient
///    uses a one-voxel central difference derived from the atlas CB fields.
/// 3. **Call-site snippet**: `if (SampleSDF(position) < 0.0f) {
///    VfxSceneSDFResolveCollision(position, velocity); }` — particles that
///    end up inside scene geometry get pushed back along the gradient
///    (collision radius 0, matching the `< 0` gate; restitution 0, i.e. the
///    inward normal velocity is removed rather than bounced).
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
        // The 8.22.0 snippet only declares SampleSceneSDF / GradientSceneSDF;
        // the call site below needs SampleSDF + VfxSceneSDFResolveCollision,
        // so they are defined here or the composed CS would not compile.
        return std::string(VfxSceneSDFEmitHLSLSnippet()) +
            "\n"
            "float SampleSDF(float3 worldPos) {\n"
            "    return SampleSceneSDF(worldPos);\n"
            "}\n"
            "\n"
            "// Smallest voxel edge of the atlas; same 1e-6 floor as SampleSceneSDF.\n"
            "float VfxSceneSDFVoxelEpsilon() {\n"
            "    const float3 voxel = (sdfWorldMax - sdfWorldMin) /\n"
            "                         max(float3(sdfRes), float3(1.0f, 1.0f, 1.0f));\n"
            "    return max(min(voxel.x, min(voxel.y, voxel.z)), 1e-6f);\n"
            "}\n"
            "\n"
            "// Mirror of uz::VfxSceneSDFResolveCollision (vfx_scene_sdf_di.cpp).\n"
            "bool VfxSceneSDFResolveCollisionEx(inout float3 position, inout float3 velocity,\n"
            "                                   float distance, float3 gradient,\n"
            "                                   float radius, float restitution) {\n"
            "    if (distance >= radius) return false;\n"
            "    const float  gLen = length(gradient);\n"
            "    const float3 n = (gLen < 1e-6f) ? float3(0.0f, 0.0f, 1.0f)\n"
            "                                    : gradient / gLen;\n"
            "    position += n * (radius - distance);\n"
            "    const float vDotN = dot(velocity, n);\n"
            "    if (vDotN < 0.0f) {\n"
            "        velocity -= (1.0f + saturate(restitution)) * vDotN * n;\n"
            "    }\n"
            "    return true;\n"
            "}\n"
            "\n"
            "void VfxSceneSDFResolveCollision(inout float3 position, inout float3 velocity) {\n"
            "    const float  distance = SampleSDF(position);\n"
            "    const float3 gradient = GradientSceneSDF(position, VfxSceneSDFVoxelEpsilon());\n"
            "    VfxSceneSDFResolveCollisionEx(position, velocity, distance, gradient,\n"
            "                                  /*radius=*/0.0f, /*restitution=*/0.0f);\n"
            "}\n";
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
