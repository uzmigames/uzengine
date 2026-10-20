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
        {"spotLightCount",  ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(LightingParams, spotLightCount)),  sizeof(uint32_t),                         0},
        {"spotLights",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(LightingParams, spotLights)),      sizeof(SpotLightData) * kMaxSpotLights,   0},
        // The cluster (block B.7) and rect area light (phase3a 3.33.0) fields are not registered: the
        // lighting CB is uploaded through the legacy allocation path, and LightingParams.MetadataMemberCount pins 4.
    };

    static const ShaderParameterMetadata kMetadata = {
        kMembers,
        sizeof(kMembers) / sizeof(kMembers[0]),
        sizeof(LightingCB),  // cbSize — matches sizeof(LightingCB), 256-byte aligned
        0,     // textureCount
        0,     // samplerCount
        0,     // uavCount
    };

    return kMetadata;
}

} // namespace uz
