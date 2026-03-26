/// UzEngine GBuffer Per-Object Params — Metadata definition for ShaderParameterBinder.

#include "gbuffer_per_object_params.h"
#include "constant_buffers.h"

#include <cstddef>

// Verify byte identity between GBufferPerObjectParams scalar section and PerObjectCB.
static_assert(sizeof(uz::PerObjectCB) == 256,
              "PerObjectCB size must be 256 bytes");
static_assert(offsetof(uz::GBufferPerObjectParams, albedoIndex) == offsetof(uz::PerObjectCB, albedoIndex),
              "albedoIndex offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, emissiveIndex) == offsetof(uz::PerObjectCB, emissiveIndex),
              "emissiveIndex offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, model) == offsetof(uz::PerObjectCB, model),
              "model offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, normalMatrix) == offsetof(uz::PerObjectCB, normalMatrix),
              "normalMatrix offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, emissiveColorR) == offsetof(uz::PerObjectCB, emissiveColorR),
              "emissiveColorR offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, baseColorTintR) == offsetof(uz::PerObjectCB, baseColorTintR),
              "baseColorTintR offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, roughnessMultiplier) == offsetof(uz::PerObjectCB, roughnessMultiplier),
              "roughnessMultiplier offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, normalIntensity) == offsetof(uz::PerObjectCB, normalIntensity),
              "normalIntensity offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, uvScaleX) == offsetof(uz::PerObjectCB, uvScaleX),
              "uvScaleX offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, aoIntensity) == offsetof(uz::PerObjectCB, aoIntensity),
              "aoIntensity offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, opacity) == offsetof(uz::PerObjectCB, opacity),
              "opacity offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, alphaClipThreshold) == offsetof(uz::PerObjectCB, alphaClipThreshold),
              "alphaClipThreshold offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, opacitySrvIndex) == offsetof(uz::PerObjectCB, opacitySrvIndex),
              "opacitySrvIndex offset mismatch");
static_assert(offsetof(uz::GBufferPerObjectParams, materialFlags) == offsetof(uz::PerObjectCB, materialFlags),
              "materialFlags offset mismatch");

namespace uz {

/// SM5 material texture register slots — t0 through t5.
static constexpr uint32_t kAlbedoSlot    = 0;
static constexpr uint32_t kNormalSlot    = 1;
static constexpr uint32_t kRoughnessSlot = 2;
static constexpr uint32_t kMetallicSlot  = 3;
static constexpr uint32_t kAoSlot        = 4;
static constexpr uint32_t kEmissiveSlot  = 5;

const ShaderParameterMetadata& GBufferPerObjectParams::GetMetadata()
{
    static const ShaderParamMember kMembers[] = {
        // ── Scalar members (bulk-copied via cbSize) ──
        {"albedoIndex",    ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, albedoIndex)),    sizeof(uint32_t), 0},
        {"normalIndex",    ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, normalIndex)),    sizeof(uint32_t), 0},
        {"roughnessIndex", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, roughnessIndex)), sizeof(uint32_t), 0},
        {"metallicIndex",  ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, metallicIndex)),  sizeof(uint32_t), 0},
        {"aoIndex",        ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, aoIndex)),        sizeof(uint32_t), 0},
        {"emissiveIndex",  ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, emissiveIndex)),  sizeof(uint32_t), 0},
        {"model",          ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, model)),          sizeof(Mat4),     0},
        {"normalMatrix",   ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, normalMatrix)),   sizeof(Mat4),     0},
        {"emissiveColorR",   ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, emissiveColorR)),   sizeof(float), 0},
        {"emissiveColorG",   ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, emissiveColorG)),   sizeof(float), 0},
        {"emissiveColorB",   ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, emissiveColorB)),   sizeof(float), 0},
        {"emissiveIntensity", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, emissiveIntensity)), sizeof(float), 0},

        // ── Material parameters (offset 176) ──
        {"baseColorTintR",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, baseColorTintR)),      sizeof(float), 0},
        {"baseColorTintG",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, baseColorTintG)),      sizeof(float), 0},
        {"baseColorTintB",      ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, baseColorTintB)),      sizeof(float), 0},
        {"roughnessMultiplier", ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, roughnessMultiplier)), sizeof(float), 0},
        {"metallicMultiplier",  ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, metallicMultiplier)),  sizeof(float), 0},
        {"normalIntensity",     ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, normalIntensity)),     sizeof(float), 0},
        {"uvScaleX",            ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, uvScaleX)),            sizeof(float), 0},
        {"uvScaleY",            ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, uvScaleY)),            sizeof(float), 0},
        {"uvOffsetX",           ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, uvOffsetX)),           sizeof(float), 0},
        {"uvOffsetY",           ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, uvOffsetY)),           sizeof(float), 0},
        {"aoIntensity",         ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, aoIntensity)),         sizeof(float), 0},
        {"opacity",             ShaderParamType::Scalar, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, opacity)),             sizeof(float), 0},

        // ── Resource members (drive texture binding on SM5) ──
        {"albedoTexture",    ShaderParamType::Texture2D, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, albedoTexture)),    0, kAlbedoSlot},
        {"normalTexture",    ShaderParamType::Texture2D, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, normalTexture)),    0, kNormalSlot},
        {"roughnessTexture", ShaderParamType::Texture2D, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, roughnessTexture)), 0, kRoughnessSlot},
        {"metallicTexture",  ShaderParamType::Texture2D, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, metallicTexture)),  0, kMetallicSlot},
        {"aoTexture",        ShaderParamType::Texture2D, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, aoTexture)),        0, kAoSlot},
        {"emissiveTexture",  ShaderParamType::Texture2D, static_cast<uint32_t>(offsetof(GBufferPerObjectParams, emissiveTexture)),  0, kEmissiveSlot},
    };

    static const ShaderParameterMetadata kMetadata = {
        kMembers,
        sizeof(kMembers) / sizeof(kMembers[0]),
        256,  // cbSize — matches sizeof(PerObjectCB), 256-byte aligned
        6,    // textureCount (albedo + normal + roughness + metallic + ao + emissive)
        0,    // samplerCount (uses static samplers in root sig)
        0,    // uavCount
    };

    return kMetadata;
}

} // namespace uz
