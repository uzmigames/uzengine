/// Tests for the embedded engine shader registry.
///
/// Validates that uzembed-generated headers are correctly included and that
/// the EngineShaders registry provides non-empty source and correct filenames
/// for every engine shader enum value.

#include <gtest/gtest.h>
#include "engine_shaders.h"

#include <cstring>
#include <string>

using uz::EngineShader;
using uz::EngineShaders;

// ─────────────────────────────────────────────────────────────────────────────
// GetSource — all shaders return non-empty HLSL source
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineShaders, OverlayVSSourceIsNonEmpty) {
    const char* src = EngineShaders::GetSource(EngineShader::OverlayVS);
    ASSERT_NE(src, nullptr);
    EXPECT_GT(strlen(src), 0u);
}

TEST(EngineShaders, OverlayTextPSSourceIsNonEmpty) {
    const char* src = EngineShaders::GetSource(EngineShader::OverlayTextPS);
    ASSERT_NE(src, nullptr);
    EXPECT_GT(strlen(src), 0u);
}

TEST(EngineShaders, OverlayRectPSSourceIsNonEmpty) {
    const char* src = EngineShaders::GetSource(EngineShader::OverlayRectPS);
    ASSERT_NE(src, nullptr);
    EXPECT_GT(strlen(src), 0u);
}

TEST(EngineShaders, GridVSSourceIsNonEmpty) {
    const char* src = EngineShaders::GetSource(EngineShader::GridVS);
    ASSERT_NE(src, nullptr);
    EXPECT_GT(strlen(src), 0u);
}

TEST(EngineShaders, GridPSSourceIsNonEmpty) {
    const char* src = EngineShaders::GetSource(EngineShader::GridPS);
    ASSERT_NE(src, nullptr);
    EXPECT_GT(strlen(src), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetSource — content contains expected HLSL keywords
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineShaders, OverlayVSContainsMainFunction) {
    std::string src = EngineShaders::GetSource(EngineShader::OverlayVS);
    EXPECT_NE(src.find("VSOutput main("), std::string::npos)
        << "Overlay VS should contain 'VSOutput main(' entry point";
}

TEST(EngineShaders, OverlayVSContainsSVVertexID) {
    std::string src = EngineShaders::GetSource(EngineShader::OverlayVS);
    EXPECT_NE(src.find("SV_VertexID"), std::string::npos)
        << "Overlay VS should use SV_VertexID for bindless vertex fetch";
}

TEST(EngineShaders, OverlayTextPSContainsFontAtlasSampling) {
    std::string src = EngineShaders::GetSource(EngineShader::OverlayTextPS);
    EXPECT_NE(src.find("fontAtlas"), std::string::npos)
        << "Overlay text PS should reference font atlas texture";
}

TEST(EngineShaders, OverlayRectPSContainsSVTarget) {
    std::string src = EngineShaders::GetSource(EngineShader::OverlayRectPS);
    EXPECT_NE(src.find("SV_Target"), std::string::npos)
        << "Overlay rect PS should output to SV_Target";
}

TEST(EngineShaders, GridVSContainsFullscreenTriangle) {
    std::string src = EngineShaders::GetSource(EngineShader::GridVS);
    EXPECT_NE(src.find("SV_VertexID"), std::string::npos)
        << "Grid VS should use SV_VertexID for fullscreen triangle generation";
}

TEST(EngineShaders, GridPSContainsGridLine) {
    std::string src = EngineShaders::GetSource(EngineShader::GridPS);
    EXPECT_NE(src.find("GridLine"), std::string::npos)
        << "Grid PS should contain the GridLine helper function";
}

// ─────────────────────────────────────────────────────────────────────────────
// GetName — filenames match expected convention
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineShaders, OverlayVSNameIsCorrect) {
    EXPECT_STREQ(EngineShaders::GetName(EngineShader::OverlayVS), "overlay_vs.hlsl");
}

TEST(EngineShaders, OverlayTextPSNameIsCorrect) {
    EXPECT_STREQ(EngineShaders::GetName(EngineShader::OverlayTextPS), "overlay_text_ps.hlsl");
}

TEST(EngineShaders, OverlayRectPSNameIsCorrect) {
    EXPECT_STREQ(EngineShaders::GetName(EngineShader::OverlayRectPS), "overlay_rect_ps.hlsl");
}

TEST(EngineShaders, GridVSNameIsCorrect) {
    EXPECT_STREQ(EngineShaders::GetName(EngineShader::GridVS), "grid_vs.hlsl");
}

TEST(EngineShaders, GridPSNameIsCorrect) {
    EXPECT_STREQ(EngineShaders::GetName(EngineShader::GridPS), "grid_ps.hlsl");
}

// ─────────────────────────────────────────────────────────────────────────────
// GetName — names end with correct stage suffix for stage inference
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineShaders, VertexShaderNamesEndWithVS) {
    std::string vsName = EngineShaders::GetName(EngineShader::OverlayVS);
    EXPECT_NE(vsName.find("_vs.hlsl"), std::string::npos)
        << "VS shader name must end with _vs.hlsl for stage inference";

    std::string gridVsName = EngineShaders::GetName(EngineShader::GridVS);
    EXPECT_NE(gridVsName.find("_vs.hlsl"), std::string::npos);
}

TEST(EngineShaders, PixelShaderNamesEndWithPS) {
    std::string textPsName = EngineShaders::GetName(EngineShader::OverlayTextPS);
    EXPECT_NE(textPsName.find("_ps.hlsl"), std::string::npos)
        << "PS shader name must end with _ps.hlsl for stage inference";

    std::string rectPsName = EngineShaders::GetName(EngineShader::OverlayRectPS);
    EXPECT_NE(rectPsName.find("_ps.hlsl"), std::string::npos);

    std::string gridPsName = EngineShaders::GetName(EngineShader::GridPS);
    EXPECT_NE(gridPsName.find("_ps.hlsl"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// All sources are distinct (no accidental duplication)
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineShaders, AllSourcesAreDistinct) {
    const char* sources[] = {
        EngineShaders::GetSource(EngineShader::OverlayVS),
        EngineShaders::GetSource(EngineShader::OverlayTextPS),
        EngineShaders::GetSource(EngineShader::OverlayRectPS),
        EngineShaders::GetSource(EngineShader::GridVS),
        EngineShaders::GetSource(EngineShader::GridPS),
    };

    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            EXPECT_STRNE(sources[i], sources[j])
                << "Shader sources " << i << " and " << j << " should be distinct";
        }
    }
}
