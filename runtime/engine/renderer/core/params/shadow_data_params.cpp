/// UzEngine Shadow Data Params — Metadata definition for ShaderParameterBinder.

#include "shadow_data_params.h"

#include <cstddef>

namespace uz {

/// SM5 shadow texture register slots.
/// Only shadowAtlas (t3) and localShadowAtlas (t11) have non-bindless register
/// declarations in deferred_lighting_ps.hlsl. Static shadow atlas and DFS output
/// are bindless-only (accessed via CB scalar indices + ResourceDescriptorHeap).
static constexpr uint32_t kShadowAtlasSlot        = 3;
static constexpr uint32_t kLocalShadowAtlasSlot   = 11;

const ShaderParameterMetadata& ShadowDataParams::GetMetadata()
{
    static const ShaderParamMember kMembers[] = {
        // ── Scalar members (bulk-copied via cbSize) ──
        {"cascadeViewProj",       ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, cascadeViewProj)),       sizeof(Mat4) * 8,   0},
        {"cascadeSplits",         ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, cascadeSplits)),         sizeof(float) * 8,  0},
        {"cascadeFadeStarts",     ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, cascadeFadeStarts)),     sizeof(float) * 8,  0},
        {"cascadeDepthBias",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, cascadeDepthBias)),      sizeof(float) * 8,  0},
        {"cascadeNormalBias",     ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, cascadeNormalBias)),      sizeof(float) * 8,  0},
        {"shadowAtlasIndex",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, shadowAtlasIndex)),      sizeof(uint32_t),   0},
        {"cascadeCount",          ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, cascadeCount)),          sizeof(uint32_t),   0},
        {"shadowMapSize",         ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, shadowMapSize)),         sizeof(float),      0},
        {"receiverBias",          ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, receiverBias)),          sizeof(float),      0},
        {"shadowFadeStart",       ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, shadowFadeStart)),       sizeof(float),      0},
        {"shadowFadeEnd",         ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, shadowFadeEnd)),         sizeof(float),      0},
        {"filterMethod",          ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, filterMethod)),          sizeof(uint32_t),   0},
        {"tanLightSourceAngle",   ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, tanLightSourceAngle)),   sizeof(float),      0},
        {"maxSoftKernelSize",     ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, maxSoftKernelSize)),     sizeof(float),      0},
        {"atlasColumns",          ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, atlasColumns)),          sizeof(uint32_t),   0},
        {"staticShadowAtlasIndex", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, staticShadowAtlasIndex)), sizeof(uint32_t), 0},
        {"useShadowCache",        ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, useShadowCache)),        sizeof(uint32_t),   0},
        {"dfsEnabled",            ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, dfsEnabled)),            sizeof(uint32_t),   0},
        {"dfsOutputSrvIndex",     ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, dfsOutputSrvIndex)),     sizeof(uint32_t),   0},
        {"localShadowAtlasIndex", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, localShadowAtlasIndex)), sizeof(uint32_t), 0},
        {"localShadowCount",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, localShadowCount)),      sizeof(uint32_t),   0},
        {"softPCSS",              ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, softPCSS)),              sizeof(uint32_t),   0},
        {"pcssMinPenumbraTexels", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, pcssMinPenumbraTexels)), sizeof(float),      0},
        {"pcssMaxPenumbraTexels", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, pcssMaxPenumbraTexels)), sizeof(float),      0},
        {"cascadePcssDepthToTexels", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, cascadePcssDepthToTexels)), sizeof(float) * 8, 0},
        {"localSoftPCSS",          ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, localSoftPCSS)),          sizeof(uint32_t), 0},
        {"localPcssSourceRadius", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(ShadowDataParams, localPcssSourceRadius)), sizeof(float), 0},

        // ── Resource members (drive texture binding on SM5) ──
        // Only shadowAtlas (t3) and localShadowAtlas (t11) are bound as SM5
        // fragment textures. Static shadow atlas and DFS output are accessed
        // exclusively via bindless ResourceDescriptorHeap[index] in the shader,
        // so they have no non-bindless register slot. Binding them at t8/t9 was
        // incorrect — those slots are transmittanceLutTex and gtaoTexture in
        // deferred_lighting_ps.hlsl.
        {"shadowAtlas",           ShaderParamType::Texture2D, static_cast<uint32_t>(offsetof(ShadowDataParams, shadowAtlas)),        0, kShadowAtlasSlot},
        {"localShadowAtlas",      ShaderParamType::Texture2D, static_cast<uint32_t>(offsetof(ShadowDataParams, localShadowAtlas)),   0, kLocalShadowAtlasSlot},
    };

    static const ShaderParameterMetadata kMetadata = {
        kMembers,
        sizeof(kMembers) / sizeof(kMembers[0]),
        2304,  // cbSize — matches sizeof(ShadowDataCB), 256-byte aligned
        2,    // textureCount (shadowAtlas + localShadowAtlas; static shadow & DFS are bindless-only)
        0,    // samplerCount
        0,    // uavCount
    };

    return kMetadata;
}

} // namespace uz
