/// UzEngine Shadow Pass Params — Metadata definitions for ShaderParameterBinder.

#include "shadow_pass_params.h"
#include "constant_buffers.h"

#include <cstddef>

// Verify ShadowPerObjectParams scalar section is byte-identical to PerObjectCB.
static_assert(sizeof(uz::ShadowPerObjectParams) == sizeof(uz::PerObjectCB),
              "ShadowPerObjectParams must match PerObjectCB size");
static_assert(offsetof(uz::ShadowPerObjectParams, model) == offsetof(uz::PerObjectCB, model),
              "model offset mismatch between ShadowPerObjectParams and PerObjectCB");

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCascadeParams metadata
// ─────────────────────────────────────────────────────────────────────────────

const ShaderParameterMetadata& ShadowCascadeParams::GetMetadata()
{
    static const ShaderParamMember kMembers[] = {
        {"viewProj", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowCascadeParams, viewProj)), sizeof(Mat4), 0},
    };

    static const ShaderParameterMetadata kMetadata = {
        kMembers,
        sizeof(kMembers) / sizeof(kMembers[0]),
        256,  // cbSize — padded to 256-byte alignment
        0,    // textureCount (depth-only, no textures)
        0,    // samplerCount
        0,    // uavCount
    };

    return kMetadata;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowPerObjectParams metadata
// ─────────────────────────────────────────────────────────────────────────────

const ShaderParameterMetadata& ShadowPerObjectParams::GetMetadata()
{
    static const ShaderParamMember kMembers[] = {
        {"albedoIndex",    ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowPerObjectParams, albedoIndex)),    sizeof(uint32_t), 0},
        {"normalIndex",    ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowPerObjectParams, normalIndex)),    sizeof(uint32_t), 0},
        {"roughnessIndex", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowPerObjectParams, roughnessIndex)), sizeof(uint32_t), 0},
        {"metallicIndex",  ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowPerObjectParams, metallicIndex)),  sizeof(uint32_t), 0},
        {"aoIndex",        ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowPerObjectParams, aoIndex)),        sizeof(uint32_t), 0},
        {"model",          ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowPerObjectParams, model)),          sizeof(Mat4),     0},
        {"normalMatrix",   ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowPerObjectParams, normalMatrix)),   sizeof(Mat4),     0},
    };

    static const ShaderParameterMetadata kMetadata = {
        kMembers,
        sizeof(kMembers) / sizeof(kMembers[0]),
        256,  // cbSize — matches sizeof(PerObjectCB)
        0,    // textureCount (depth-only, no textures)
        0,    // samplerCount
        0,    // uavCount
    };

    return kMetadata;
}

} // namespace uz
