/// Unit tests for the shader parameter system: metadata, CB layout, macros,
/// HLSL generation, and binder logic.
///
/// Tests validate:
/// 1. Metadata correctly separates scalar vs texture vs sampler members.
/// 2. CB layout matches expected size (256-byte aligned).
/// 3. PerFrameParams metadata produces correct member descriptors.
/// 4. Macro-generated structs produce correct metadata.
/// 5. HLSL generation produces valid output.
/// 6. ShadowDataParams and LightingParams metadata correctness.

#include "shader_parameters.h"
#include "shader_parameter_macros.h"
#include "shader_parameter_hlsl.h"
#include "per_frame_params.h"
#include "shadow_data_params.h"
#include "lighting_params.h"
#include "constant_buffers.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <string>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// AlignCBSize
// ─────────────────────────────────────────────────────────────────────────────

TEST(AlignCBSize, ZeroReturnsZero) {
    EXPECT_EQ(AlignCBSize(0), 0u);
}

TEST(AlignCBSize, ExactMultipleUnchanged) {
    EXPECT_EQ(AlignCBSize(256), 256u);
    EXPECT_EQ(AlignCBSize(512), 512u);
    EXPECT_EQ(AlignCBSize(1024), 1024u);
}

TEST(AlignCBSize, RoundsUp) {
    EXPECT_EQ(AlignCBSize(1), 256u);
    EXPECT_EQ(AlignCBSize(128), 256u);
    EXPECT_EQ(AlignCBSize(255), 256u);
    EXPECT_EQ(AlignCBSize(257), 512u);
    EXPECT_EQ(AlignCBSize(500), 512u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderParamMember — type classification
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderParamMember, ScalarMemberHasZeroResourceSlot) {
    ShaderParamMember m{"test", ShaderParamType::Scalar, 0, 64, 0};
    EXPECT_EQ(m.type, ShaderParamType::Scalar);
    EXPECT_EQ(m.size, 64u);
    EXPECT_EQ(m.resourceSlot, 0u);
}

TEST(ShaderParamMember, TextureMemberHasZeroSize) {
    ShaderParamMember m{"tex", ShaderParamType::Texture2D, 128, 0, 5};
    EXPECT_EQ(m.type, ShaderParamType::Texture2D);
    EXPECT_EQ(m.size, 0u);
    EXPECT_EQ(m.resourceSlot, 5u);
}

TEST(ShaderParamMember, TextureCubeMemberHasZeroSize) {
    ShaderParamMember m{"cube", ShaderParamType::TextureCube, 136, 0, 6};
    EXPECT_EQ(m.type, ShaderParamType::TextureCube);
    EXPECT_EQ(m.size, 0u);
    EXPECT_EQ(m.resourceSlot, 6u);
}

TEST(ShaderParamMember, SamplerMemberHasZeroSize) {
    ShaderParamMember m{"samp", ShaderParamType::Sampler, 144, 0, 0};
    EXPECT_EQ(m.type, ShaderParamType::Sampler);
    EXPECT_EQ(m.size, 0u);
}

TEST(ShaderParamMember, UAVMemberHasZeroSize) {
    ShaderParamMember m{"uav", ShaderParamType::UAV, 152, 0, 0};
    EXPECT_EQ(m.type, ShaderParamType::UAV);
    EXPECT_EQ(m.size, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderParameterMetadata — separation of scalar vs resource
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderParameterMetadata, SeparatesScalarAndTexture) {
    static const ShaderParamMember kMembers[] = {
        {"viewProj",   ShaderParamType::Scalar,      0,   64, 0},
        {"cameraPos",  ShaderParamType::Scalar,      64,  16, 0},
        {"envCubemap", ShaderParamType::TextureCube, 256,  0, 5},
        {"brdfLut",    ShaderParamType::Texture2D,  264,  0, 7},
    };

    ShaderParameterMetadata meta{};
    meta.members      = kMembers;
    meta.memberCount  = 4;
    meta.cbSize       = 256;
    meta.textureCount = 2;
    meta.samplerCount = 0;
    meta.uavCount     = 0;

    // Count scalars and textures from metadata
    uint32_t scalarCount = 0;
    uint32_t texCount = 0;
    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        if (meta.members[i].type == ShaderParamType::Scalar) {
            ++scalarCount;
        } else if (meta.members[i].type == ShaderParamType::Texture2D ||
                   meta.members[i].type == ShaderParamType::TextureCube) {
            ++texCount;
        }
    }

    EXPECT_EQ(scalarCount, 2u);
    EXPECT_EQ(texCount, 2u);
    EXPECT_EQ(meta.textureCount, texCount);
}

TEST(ShaderParameterMetadata, CBSizeIs256Aligned) {
    ShaderParameterMetadata meta{};
    meta.cbSize = AlignCBSize(100);
    EXPECT_EQ(meta.cbSize, 256u);
    EXPECT_EQ(meta.cbSize % 256, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameParams — metadata correctness
// ─────────────────────────────────────────────────────────────────────────────

TEST(PerFrameParams, MetadataCBSizeIs1024) {
    const auto& meta = PerFrameParams::GetMetadata();
    EXPECT_EQ(meta.cbSize, 1024u);
    EXPECT_EQ(meta.cbSize % 256, 0u);
}

TEST(PerFrameParams, MetadataTextureCount) {
    const auto& meta = PerFrameParams::GetMetadata();
    EXPECT_EQ(meta.textureCount, 3u);
}

TEST(PerFrameParams, MetadataSamplerAndUAVCounts) {
    const auto& meta = PerFrameParams::GetMetadata();
    EXPECT_EQ(meta.samplerCount, 0u);
    EXPECT_EQ(meta.uavCount, 0u);
}

TEST(PerFrameParams, MetadataHasCorrectMemberCount) {
    const auto& meta = PerFrameParams::GetMetadata();
    // 21 scalar members + 3 resource members = 24
    EXPECT_EQ(meta.memberCount, 24u);
}

TEST(PerFrameParams, ScalarSectionByteIdenticalToPerFrameCB) {
    // Zero-init both to eliminate default differences (e.g., contactShadowLength),
    // then set identical values and compare the full scalar section.
    PerFrameParams params;
    PerFrameCB cb;
    std::memset(&params, 0, sizeof(PerFrameParams));
    std::memset(&cb, 0, sizeof(PerFrameCB));

    // Fill with known values
    params.cameraPos = {1.0f, 2.0f, 3.0f, 0.0f};
    params.timeParams = {100.0f, 0.016f, 6000.0f, 0.0f};
    params.exposure = 2.5f;
    params.skyLightIntensity = 0.8f;

    cb.cameraPos = {1.0f, 2.0f, 3.0f, 0.0f};
    cb.timeParams = {100.0f, 0.016f, 6000.0f, 0.0f};
    cb.exposure = 2.5f;
    cb.skyLightIntensity = 0.8f;

    // First 768 bytes (scalar section) should be identical
    EXPECT_EQ(std::memcmp(&params, &cb, sizeof(PerFrameCB)), 0);
}

TEST(PerFrameParams, ResourceHandlesAtCorrectOffset) {
    EXPECT_EQ(offsetof(PerFrameParams, irradianceCubemap), 1024u);
    EXPECT_EQ(offsetof(PerFrameParams, prefilteredSpecular), 1032u);
    EXPECT_EQ(offsetof(PerFrameParams, brdfLut), 1040u);
}

TEST(PerFrameParams, ResourceMembersHaveCorrectSlots) {
    const auto& meta = PerFrameParams::GetMetadata();

    // Find resource members and verify their register slots
    bool foundIrradiance = false;
    bool foundPrefiltered = false;
    bool foundBrdfLut = false;

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (std::string(m.name) == "irradianceCubemap") {
            EXPECT_EQ(m.type, ShaderParamType::TextureCube);
            EXPECT_EQ(m.resourceSlot, 5u); // t5
            EXPECT_EQ(m.size, 0u);
            foundIrradiance = true;
        } else if (std::string(m.name) == "prefilteredSpecular") {
            EXPECT_EQ(m.type, ShaderParamType::TextureCube);
            EXPECT_EQ(m.resourceSlot, 6u); // t6
            EXPECT_EQ(m.size, 0u);
            foundPrefiltered = true;
        } else if (std::string(m.name) == "brdfLut") {
            EXPECT_EQ(m.type, ShaderParamType::Texture2D);
            EXPECT_EQ(m.resourceSlot, 7u); // t7
            EXPECT_EQ(m.size, 0u);
            foundBrdfLut = true;
        }
    }

    EXPECT_TRUE(foundIrradiance);
    EXPECT_TRUE(foundPrefiltered);
    EXPECT_TRUE(foundBrdfLut);
}

TEST(PerFrameParams, ScalarMembersHaveCorrectOffsets) {
    const auto& meta = PerFrameParams::GetMetadata();

    // Verify first few scalar members have correct offsets
    bool foundViewProj = false;
    bool foundCameraPos = false;
    bool foundExposure = false;

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (m.type != ShaderParamType::Scalar) continue;

        if (std::string(m.name) == "viewProj") {
            EXPECT_EQ(m.offset, static_cast<uint32_t>(offsetof(PerFrameParams, viewProj)));
            EXPECT_EQ(m.size, sizeof(Mat4));
            foundViewProj = true;
        } else if (std::string(m.name) == "cameraPos") {
            EXPECT_EQ(m.offset, static_cast<uint32_t>(offsetof(PerFrameParams, cameraPos)));
            EXPECT_EQ(m.size, sizeof(Vec4));
            foundCameraPos = true;
        } else if (std::string(m.name) == "exposure") {
            EXPECT_EQ(m.offset, static_cast<uint32_t>(offsetof(PerFrameParams, exposure)));
            EXPECT_EQ(m.size, sizeof(float));
            foundExposure = true;
        }
    }

    EXPECT_TRUE(foundViewProj);
    EXPECT_TRUE(foundCameraPos);
    EXPECT_TRUE(foundExposure);
}

TEST(PerFrameParams, AllScalarMembersBeforeCBBoundary) {
    const auto& meta = PerFrameParams::GetMetadata();

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (m.type == ShaderParamType::Scalar) {
            EXPECT_LT(m.offset, meta.cbSize)
                << "Scalar member '" << m.name << "' at offset " << m.offset
                << " is beyond CB size " << meta.cbSize;
        }
    }
}

TEST(PerFrameParams, AllResourceMembersAfterCBBoundary) {
    const auto& meta = PerFrameParams::GetMetadata();

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (m.type != ShaderParamType::Scalar) {
            EXPECT_GE(m.offset, meta.cbSize)
                << "Resource member '" << m.name << "' at offset " << m.offset
                << " is inside CB region (cbSize=" << meta.cbSize << ")";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataCB — layout validation (shader parameter context)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderParamShadowCB, SizeIs2304Bytes) {
    EXPECT_EQ(sizeof(ShadowDataCB), 2304u);
    EXPECT_EQ(sizeof(ShadowDataCB) % 256, 0u);
}

TEST(ShaderParamShadowCB, ShadowAtlasFieldIsAtExpectedOffset) {
    // ShadowDataCB shadow atlas index should be at a known offset
    // for future shader parameter migration
    EXPECT_EQ(offsetof(ShadowDataCB, shadowAtlasIndex), 640u);
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeCount), 644u);
}

TEST(ShaderParamShadowCB, HasSrvIndexFields) {
    ShadowDataCB cb{};
    EXPECT_EQ(cb.staticShadowAtlasIndex, UINT32_MAX);
    EXPECT_EQ(cb.dfsOutputSrvIndex, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// LightingCB — layout validation (shader parameter context)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderParamLightingCB, SizeIs256Multiple) {
    EXPECT_EQ(sizeof(LightingCB) % 256, 0u);
}

TEST(ShaderParamLightingCB, PointLightDataSize) {
    EXPECT_EQ(sizeof(PointLightData), 64u);
}

TEST(ShaderParamLightingCB, MaxPointLights) {
    EXPECT_EQ(kMaxPointLights, 64u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Macro-generated struct — test that macros produce valid C++ + metadata
// ─────────────────────────────────────────────────────────────────────────────

// A small test struct declared with the macro system.
// This lives in the test file to validate the macros work in isolation.
BEGIN_SHADER_PARAMETER_STRUCT(TestMacroParams)
    SHADER_PARAMETER(float, brightness)
    SHADER_PARAMETER(float, contrast)
    SHADER_PARAMETER(uint32_t, flags)
    SHADER_PARAMETER(float, _pad0)
    SHADER_PARAMETER(uz::Vec4, tintColor)
    SHADER_PARAMETER_ARRAY(float, _pad1, 56)  // pad scalar section to 256 bytes
SHADER_PARAMETER_SCALAR_END()
    SHADER_PARAMETER_TEXTURE(Texture2D, colorTexture, 0)
    SHADER_PARAMETER_TEXTURE(TextureCube, envMap, 5)
    SHADER_PARAMETER_SAMPLER(linearSampler, 0)
END_SHADER_PARAMETER_STRUCT()

// Metadata implementation for the test struct
BEGIN_SHADER_PARAMETER_METADATA(TestMacroParams)
    METADATA_SCALAR(TestMacroParams, brightness, float)
    METADATA_SCALAR(TestMacroParams, contrast, float)
    METADATA_SCALAR(TestMacroParams, flags, uint32_t)
    METADATA_SCALAR(TestMacroParams, tintColor, uz::Vec4)
    METADATA_TEXTURE2D(TestMacroParams, colorTexture, 0)
    METADATA_TEXTURE_CUBE(TestMacroParams, envMap, 5)
    METADATA_SAMPLER(TestMacroParams, linearSampler, 0)
END_SHADER_PARAMETER_METADATA(TestMacroParams, 256, 2, 1, 0)

TEST(ShaderParamMacros, StructIs16ByteAligned) {
    EXPECT_EQ(alignof(TestMacroParams) % 16, 0u);
}

TEST(ShaderParamMacros, MetadataCBSizeIs256) {
    const auto& meta = TestMacroParams::GetMetadata();
    EXPECT_EQ(meta.cbSize, 256u);
    EXPECT_EQ(meta.cbSize % 256, 0u);
}

TEST(ShaderParamMacros, MetadataTextureCount) {
    const auto& meta = TestMacroParams::GetMetadata();
    EXPECT_EQ(meta.textureCount, 2u);
}

TEST(ShaderParamMacros, MetadataSamplerCount) {
    const auto& meta = TestMacroParams::GetMetadata();
    EXPECT_EQ(meta.samplerCount, 1u);
}

TEST(ShaderParamMacros, MetadataUAVCount) {
    const auto& meta = TestMacroParams::GetMetadata();
    EXPECT_EQ(meta.uavCount, 0u);
}

TEST(ShaderParamMacros, MetadataMemberCount) {
    const auto& meta = TestMacroParams::GetMetadata();
    // 4 scalars + 2 textures + 1 sampler = 7
    EXPECT_EQ(meta.memberCount, 7u);
}

TEST(ShaderParamMacros, ScalarMembersBeforeCBBoundary) {
    const auto& meta = TestMacroParams::GetMetadata();
    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        if (meta.members[i].type == ShaderParamType::Scalar) {
            EXPECT_LT(meta.members[i].offset, meta.cbSize)
                << "Scalar '" << meta.members[i].name << "' at offset "
                << meta.members[i].offset << " exceeds cbSize " << meta.cbSize;
        }
    }
}

TEST(ShaderParamMacros, ResourceMembersAfterCBBoundary) {
    const auto& meta = TestMacroParams::GetMetadata();
    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        if (meta.members[i].type != ShaderParamType::Scalar) {
            EXPECT_GE(meta.members[i].offset, meta.cbSize)
                << "Resource '" << meta.members[i].name << "' at offset "
                << meta.members[i].offset << " is inside CB region";
        }
    }
}

TEST(ShaderParamMacros, TextureSlotMapping) {
    const auto& meta = TestMacroParams::GetMetadata();
    bool foundColor = false;
    bool foundEnv = false;

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (std::string(m.name) == "colorTexture") {
            EXPECT_EQ(m.type, ShaderParamType::Texture2D);
            EXPECT_EQ(m.resourceSlot, 0u);
            foundColor = true;
        } else if (std::string(m.name) == "envMap") {
            EXPECT_EQ(m.type, ShaderParamType::TextureCube);
            EXPECT_EQ(m.resourceSlot, 5u);
            foundEnv = true;
        }
    }

    EXPECT_TRUE(foundColor);
    EXPECT_TRUE(foundEnv);
}

TEST(ShaderParamMacros, SamplerSlotMapping) {
    const auto& meta = TestMacroParams::GetMetadata();
    bool found = false;

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (std::string(m.name) == "linearSampler") {
            EXPECT_EQ(m.type, ShaderParamType::Sampler);
            EXPECT_EQ(m.resourceSlot, 0u);
            found = true;
        }
    }

    EXPECT_TRUE(found);
}

TEST(ShaderParamMacros, ScalarOffsetMatchesStructLayout) {
    const auto& meta = TestMacroParams::GetMetadata();

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (std::string(m.name) == "brightness") {
            EXPECT_EQ(m.offset, static_cast<uint32_t>(offsetof(TestMacroParams, brightness)));
            EXPECT_EQ(m.size, sizeof(float));
        } else if (std::string(m.name) == "tintColor") {
            EXPECT_EQ(m.offset, static_cast<uint32_t>(offsetof(TestMacroParams, tintColor)));
            EXPECT_EQ(m.size, sizeof(uz::Vec4));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL Generation
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderParamHLSL, GeneratesValidCbuffer) {
    const auto& meta = TestMacroParams::GetMetadata();
    std::string hlsl = GenerateHLSL(meta, "TestCB", 1);

    // Must contain cbuffer header
    EXPECT_NE(hlsl.find("cbuffer TestCB : register(b1)"), std::string::npos)
        << "Missing cbuffer header in:\n" << hlsl;

    // Must contain scalar members
    EXPECT_NE(hlsl.find("brightness"), std::string::npos);
    EXPECT_NE(hlsl.find("contrast"), std::string::npos);
    EXPECT_NE(hlsl.find("flags"), std::string::npos);
    EXPECT_NE(hlsl.find("tintColor"), std::string::npos);

    // Must contain closing brace
    EXPECT_NE(hlsl.find("};"), std::string::npos);
}

TEST(ShaderParamHLSL, GeneratesTextureDeclarations) {
    const auto& meta = TestMacroParams::GetMetadata();
    std::string hlsl = GenerateHLSL(meta, "TestCB", 1);

    EXPECT_NE(hlsl.find("Texture2D colorTexture : register(t0)"), std::string::npos)
        << "Missing Texture2D in:\n" << hlsl;
    EXPECT_NE(hlsl.find("TextureCube envMap : register(t5)"), std::string::npos)
        << "Missing TextureCube in:\n" << hlsl;
}

TEST(ShaderParamHLSL, GeneratesSamplerDeclarations) {
    const auto& meta = TestMacroParams::GetMetadata();
    std::string hlsl = GenerateHLSL(meta, "TestCB", 1);

    EXPECT_NE(hlsl.find("SamplerState linearSampler : register(s0)"), std::string::npos)
        << "Missing SamplerState in:\n" << hlsl;
}

TEST(ShaderParamHLSL, PerFrameParamsHLSL) {
    const auto& meta = PerFrameParams::GetMetadata();
    std::string hlsl = GenerateHLSL(meta, "PerFrameCB", 1);

    EXPECT_NE(hlsl.find("cbuffer PerFrameCB : register(b1)"), std::string::npos);
    EXPECT_NE(hlsl.find("viewProj"), std::string::npos);
    EXPECT_NE(hlsl.find("TextureCube irradianceCubemap : register(t5)"), std::string::npos);
    EXPECT_NE(hlsl.find("Texture2D brdfLut : register(t7)"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataParams — metadata correctness
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowDataParams, MetadataCBSizeIs2304) {
    const auto& meta = ShadowDataParams::GetMetadata();
    EXPECT_EQ(meta.cbSize, 2304u);
    EXPECT_EQ(meta.cbSize % 256, 0u);
}

TEST(ShadowDataParams, MetadataTextureCount) {
    const auto& meta = ShadowDataParams::GetMetadata();
    // Only shadowAtlas (t3) and localShadowAtlas (t11) are SM5 bound textures.
    // staticShadowAtlas and dfsOutputTexture are bindless-only.
    EXPECT_EQ(meta.textureCount, 2u);
}

TEST(ShadowDataParams, MetadataSamplerAndUAVCounts) {
    const auto& meta = ShadowDataParams::GetMetadata();
    EXPECT_EQ(meta.samplerCount, 0u);
    EXPECT_EQ(meta.uavCount, 0u);
}

TEST(ShadowDataParams, ScalarSectionByteIdenticalToShadowDataCB) {
    // Zero-initialize both structs via memset so uninitialized fields are identical
    ShadowDataParams params;
    ShadowDataCB cb;
    std::memset(&params, 0, sizeof(params));
    std::memset(&cb, 0, sizeof(cb));

    // Set matching values in both
    params.cascadeCount = 4;
    params.shadowMapSize = 2048.0f;
    params.receiverBias = 0.9f;
    params.shadowAtlasIndex = 42;
    params.filterMethod = 1;
    params.staticShadowAtlasIndex = UINT32_MAX;
    params.dfsOutputSrvIndex = UINT32_MAX;

    cb.cascadeCount = 4;
    cb.shadowMapSize = 2048.0f;
    cb.receiverBias = 0.9f;
    cb.shadowAtlasIndex = 42;
    cb.filterMethod = 1;
    cb.staticShadowAtlasIndex = UINT32_MAX;
    cb.dfsOutputSrvIndex = UINT32_MAX;

    // First 768 bytes should be identical
    EXPECT_EQ(std::memcmp(&params, &cb, sizeof(ShadowDataCB)), 0);
}

TEST(ShadowDataParams, ResourceHandlesAtCorrectOffset) {
    EXPECT_EQ(offsetof(ShadowDataParams, shadowAtlas), 2304u);
    EXPECT_EQ(offsetof(ShadowDataParams, staticShadowAtlas), 2312u);
    EXPECT_EQ(offsetof(ShadowDataParams, dfsOutputTexture), 2320u);
}

TEST(ShadowDataParams, ResourceMembersHaveCorrectSlots) {
    const auto& meta = ShadowDataParams::GetMetadata();

    bool foundAtlas = false;
    bool foundLocalAtlas = false;

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (std::string(m.name) == "shadowAtlas") {
            EXPECT_EQ(m.type, ShaderParamType::Texture2D);
            EXPECT_EQ(m.resourceSlot, 3u);
            foundAtlas = true;
        } else if (std::string(m.name) == "localShadowAtlas") {
            EXPECT_EQ(m.type, ShaderParamType::Texture2D);
            EXPECT_EQ(m.resourceSlot, 11u);
            foundLocalAtlas = true;
        }
    }

    EXPECT_TRUE(foundAtlas);
    EXPECT_TRUE(foundLocalAtlas);
}

TEST(ShadowDataParams, AllScalarMembersBeforeCBBoundary) {
    const auto& meta = ShadowDataParams::GetMetadata();
    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (m.type == ShaderParamType::Scalar) {
            EXPECT_LT(m.offset, meta.cbSize)
                << "Scalar '" << m.name << "' at offset " << m.offset
                << " exceeds CB size " << meta.cbSize;
        }
    }
}

TEST(ShadowDataParams, AllResourceMembersAfterCBBoundary) {
    const auto& meta = ShadowDataParams::GetMetadata();
    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (m.type != ShaderParamType::Scalar) {
            EXPECT_GE(m.offset, meta.cbSize)
                << "Resource '" << m.name << "' at offset " << m.offset
                << " is inside CB region (cbSize=" << meta.cbSize << ")";
        }
    }
}

TEST(ShadowDataParams, HLSLGenerationContainsShadowAtlas) {
    const auto& meta = ShadowDataParams::GetMetadata();
    std::string hlsl = GenerateHLSL(meta, "ShadowDataCB", 4);

    EXPECT_NE(hlsl.find("cbuffer ShadowDataCB : register(b4)"), std::string::npos);
    EXPECT_NE(hlsl.find("Texture2D shadowAtlas : register(t3)"), std::string::npos);
    EXPECT_NE(hlsl.find("Texture2D localShadowAtlas : register(t11)"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// LightingParams — metadata correctness
// ─────────────────────────────────────────────────────────────────────────────

TEST(LightingParams, MetadataCBSizeIs5632) {
    const auto& meta = LightingParams::GetMetadata();
    EXPECT_EQ(meta.cbSize, 5632u);
    EXPECT_EQ(meta.cbSize % 256, 0u);
}

TEST(LightingParams, MetadataHasNoResources) {
    const auto& meta = LightingParams::GetMetadata();
    EXPECT_EQ(meta.textureCount, 0u);
    EXPECT_EQ(meta.samplerCount, 0u);
    EXPECT_EQ(meta.uavCount, 0u);
}

TEST(LightingParams, MetadataMemberCount) {
    const auto& meta = LightingParams::GetMetadata();
    // 4 scalar members: pointLights + pointLightCount + spotLightCount + spotLights
    EXPECT_EQ(meta.memberCount, 4u);
}

TEST(LightingParams, SizeMatchesLightingCB) {
    EXPECT_EQ(sizeof(LightingParams), sizeof(LightingCB));
}

TEST(LightingParams, ScalarSectionByteIdenticalToLightingCB) {
    LightingParams params{};
    LightingCB cb{};

    params.pointLightCount = 3;
    params.pointLights[0].position = {1.0f, 2.0f, 3.0f};
    params.pointLights[0].radius = 500.0f;
    params.pointLights[0].color = {1.0f, 0.8f, 0.6f};

    cb.pointLightCount = 3;
    cb.pointLights[0].position = {1.0f, 2.0f, 3.0f};
    cb.pointLights[0].radius = 500.0f;
    cb.pointLights[0].color = {1.0f, 0.8f, 0.6f};

    EXPECT_EQ(std::memcmp(&params, &cb, sizeof(LightingCB)), 0);
}

TEST(LightingParams, AllScalarMembersBeforeCBBoundary) {
    const auto& meta = LightingParams::GetMetadata();
    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const auto& m = meta.members[i];
        if (m.type == ShaderParamType::Scalar) {
            EXPECT_LT(m.offset, meta.cbSize)
                << "Scalar '" << m.name << "' at offset " << m.offset
                << " exceeds CB size " << meta.cbSize;
        }
    }
}
