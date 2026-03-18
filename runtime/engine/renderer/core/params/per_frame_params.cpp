/// UzEngine Per-Frame Params — Metadata definition for ShaderParameterBinder.

#include "per_frame_params.h"

#include <cstddef>

namespace uz {

/// SM5 IBL texture register slots — t5, t6, t7 (after G-Buffer t0-t4).
static constexpr uint32_t kIBLIrradianceSlot  = 5;
static constexpr uint32_t kIBLPrefilteredSlot  = 6;
static constexpr uint32_t kIBLBrdfLutSlot      = 7;

const ShaderParameterMetadata& PerFrameParams::GetMetadata()
{
    // Member descriptors — scalars first (copied to CB), then resources.
    // Only scalar members that the binder needs to validate are listed.
    // For memcpy-based CB upload, the binder copies cbSize bytes starting
    // from offset 0 regardless of individual scalar member descriptors.
    // Resource members are the important part — they drive texture binding.
    static const ShaderParamMember kMembers[] = {
        // ── Scalar members (informational — the binder copies cbSize bytes bulk) ──
        {"viewProj",             ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, viewProj)),             sizeof(Mat4),     0},
        {"cameraPos",            ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, cameraPos)),            sizeof(Vec4),     0},
        {"timeParams",           ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, timeParams)),           sizeof(Vec4),     0},
        {"lightDir",             ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, lightDir)),             sizeof(Vec4),     0},
        {"lightColor",           ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, lightColor)),           sizeof(Vec4),     0},
        {"ambientColor",         ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, ambientColor)),         sizeof(Vec4),     0},
        {"invViewProj",          ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, invViewProj)),          sizeof(Mat4),     0},
        {"nearPlane",            ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, nearPlane)),            sizeof(float),    0},
        {"farPlane",             ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, farPlane)),             sizeof(float),    0},
        {"irradianceSrvIndex",   ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, irradianceSrvIndex)),   sizeof(uint32_t), 0},
        {"prefilteredSrvIndex",  ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, prefilteredSrvIndex)),  sizeof(uint32_t), 0},
        {"brdfLutSrvIndex",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, brdfLutSrvIndex)),      sizeof(uint32_t), 0},
        {"prefilteredMipLevels", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, prefilteredMipLevels)), sizeof(uint32_t), 0},
        {"skyLightIntensity",    ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, skyLightIntensity)),    sizeof(float),    0},
        {"skyLightColorR",       ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, skyLightColorR)),       sizeof(float),    0},
        {"skyLightColorG",       ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, skyLightColorG)),       sizeof(float),    0},
        {"skyLightColorB",       ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, skyLightColorB)),       sizeof(float),    0},
        {"skyLightMinAmbient",   ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, skyLightMinAmbient)),   sizeof(float),    0},
        {"exposure",             ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, exposure)),             sizeof(float),    0},
        {"skyIrradianceSH",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, skyIrradianceSH)),      sizeof(Vec4) * 7, 0},
        {"shadowDebugMode",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(PerFrameParams, shadowDebugMode)),      sizeof(uint32_t), 0},

        // ── Resource members (drive texture binding on SM5) ──
        {"irradianceCubemap",    ShaderParamType::TextureCube, static_cast<uint32_t>(offsetof(PerFrameParams, irradianceCubemap)),    0, kIBLIrradianceSlot},
        {"prefilteredSpecular",  ShaderParamType::TextureCube, static_cast<uint32_t>(offsetof(PerFrameParams, prefilteredSpecular)),  0, kIBLPrefilteredSlot},
        {"brdfLut",              ShaderParamType::Texture2D,   static_cast<uint32_t>(offsetof(PerFrameParams, brdfLut)),              0, kIBLBrdfLutSlot},
    };

    static const ShaderParameterMetadata kMetadata = {
        kMembers,
        sizeof(kMembers) / sizeof(kMembers[0]),
        512,  // cbSize — matches sizeof(PerFrameCB), 256-byte aligned
        3,    // textureCount (irradiance + prefiltered + brdfLut)
        0,    // samplerCount (uses static samplers in root sig)
        0,    // uavCount
    };

    return kMetadata;
}

} // namespace uz
