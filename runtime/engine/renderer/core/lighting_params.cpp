/// UzEngine Lighting Params — Metadata definition for ShaderParameterBinder.

#include "lighting_params.h"

#include <cstddef>

namespace uz {

const ShaderParameterMetadata& LightingParams::GetMetadata()
{
    static const ShaderParamMember kMembers[] = {
        // ── Scalar members ──
        {"pointLights",     ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(LightingParams, pointLights)),     sizeof(PointLightData) * kMaxPointLights, 0},
        {"pointLightCount", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(LightingParams, pointLightCount)), sizeof(uint32_t),                         0},
    };

    static const ShaderParameterMetadata kMetadata = {
        kMembers,
        sizeof(kMembers) / sizeof(kMembers[0]),
        3328,  // cbSize — matches sizeof(LightingCB), 256-byte aligned
        0,     // textureCount
        0,     // samplerCount
        0,     // uavCount
    };

    return kMetadata;
}

} // namespace uz
