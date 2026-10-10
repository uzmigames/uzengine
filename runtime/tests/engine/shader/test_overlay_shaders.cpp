/// Tests for the embedded editor overlay shaders (TextRenderer).
///
/// TextRenderer::Init compiles all seven overlay shaders from their embedded
/// sources and fails as a whole if any of them is missing or does not
/// compile — the editor overlay (menus, panels, icons, MSDF labels) then
/// disappears. These tests compile every overlay shader through DXC in both
/// variants the renderer builds: SM6.6 bindless (UZ_BINDLESS=1, vertex fetch
/// and textures through the descriptor heaps) and the register-bound path.
///
/// Requires dxcompiler.dll next to the test executable; the compile tests are
/// skipped (not failed) when DXC cannot be loaded, like test_shader_compiler.

#ifdef _WIN32

#include "engine_shaders.h"
#include "shader_compiler.h"

#include <gtest/gtest.h>

#include <string>

namespace {

struct OverlayShaderCase {
    uz::EngineShader shader;
    uz::ShaderStageCompile stage;
};

/// Every shader TextRenderer::Init compiles, with its pipeline stage.
constexpr OverlayShaderCase kOverlayShaders[] = {
    {uz::EngineShader::OverlayVS,       uz::ShaderStageCompile::Vertex},
    {uz::EngineShader::OverlayTextPS,   uz::ShaderStageCompile::Pixel},
    {uz::EngineShader::OverlayRectPS,   uz::ShaderStageCompile::Pixel},
    {uz::EngineShader::OverlaySpritePS, uz::ShaderStageCompile::Pixel},
    {uz::EngineShader::OverlayRrectVS,  uz::ShaderStageCompile::Vertex},
    {uz::EngineShader::OverlayRrectPS,  uz::ShaderStageCompile::Pixel},
    {uz::EngineShader::OverlayMsdfPS,   uz::ShaderStageCompile::Pixel},
};

class OverlayShaderCompileTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!m_compiler.Init()) {
            GTEST_SKIP() << "DXC not available (dxcompiler.dll not next to test exe)";
        }
    }

    /// Compiles one embedded overlay shader; returns DXC's messages on failure.
    uz::ShaderCompileResult Compile(const OverlayShaderCase& c, bool bindless) {
        uz::ShaderCompileDesc desc;
        desc.source     = uz::EngineShaders::GetSource(c.shader);
        desc.sourceName = uz::EngineShaders::GetName(c.shader);
        desc.stage      = c.stage;
        desc.model      = bindless ? uz::ShaderModelCompile::SM6_6 : uz::ShaderModelCompile::SM6_0;
        if (bindless) desc.defines.push_back("UZ_BINDLESS=1");
        return m_compiler.Compile(desc);
    }

    uz::ShaderCompiler m_compiler;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Embedded sources — the four shaders that went missing from the tree
// ─────────────────────────────────────────────────────────────────────────────

TEST(OverlayShaderSources, SpriteRrectAndMsdfShadersAreEmbedded) {
    EXPECT_STREQ(uz::EngineShaders::GetName(uz::EngineShader::OverlaySpritePS), "overlay_sprite_ps.hlsl");
    EXPECT_STREQ(uz::EngineShaders::GetName(uz::EngineShader::OverlayRrectVS),  "overlay_rrect_vs.hlsl");
    EXPECT_STREQ(uz::EngineShaders::GetName(uz::EngineShader::OverlayRrectPS),  "overlay_rrect_ps.hlsl");
    EXPECT_STREQ(uz::EngineShaders::GetName(uz::EngineShader::OverlayMsdfPS),   "overlay_msdf_ps.hlsl");
    for (const auto& c : kOverlayShaders) {
        const std::string src = uz::EngineShaders::GetSource(c.shader);
        EXPECT_NE(src.find(" main("), std::string::npos)
            << uz::EngineShaders::GetName(c.shader) << " must define the `main` entry point";
    }
}

TEST(OverlayShaderSources, RrectVertexShaderMirrors96ByteVertex) {
    // overlay_rrect_vs.hlsl reads OverlayRrectVertex (text_renderer.h, 96 bytes,
    // static_assert there) from a StructuredBuffer: the HLSL mirror must carry
    // every field in the same order.
    const std::string src = uz::EngineShaders::GetSource(uz::EngineShader::OverlayRrectVS);
    const char* fields[] = {"float2 position;", "float2 localPos;", "float2 halfExtent;",
                            "float2 shadowOffset;", "float  cornerRadius;", "float  borderThickness;",
                            "float  shadowBlur;", "float  _pad0;", "float4 fillColor;",
                            "float4 borderColor;", "float4 shadowColor;"};
    size_t cursor = 0;
    for (const char* field : fields) {
        const size_t at = src.find(field, cursor);
        ASSERT_NE(at, std::string::npos) << "missing or out-of-order field: " << field;
        cursor = at;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DXC compilation — both binding models
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(OverlayShaderCompileTest, AllOverlayShadersCompileBindlessSM66) {
    for (const auto& c : kOverlayShaders) {
        const uz::ShaderCompileResult result = Compile(c, /*bindless=*/true);
        EXPECT_TRUE(result.success) << uz::EngineShaders::GetName(c.shader) << ":\n" << result.errors;
        EXPECT_FALSE(result.bytecode.empty()) << uz::EngineShaders::GetName(c.shader);
    }
}

TEST_F(OverlayShaderCompileTest, AllOverlayShadersCompileRegisterBound) {
    for (const auto& c : kOverlayShaders) {
        const uz::ShaderCompileResult result = Compile(c, /*bindless=*/false);
        EXPECT_TRUE(result.success) << uz::EngineShaders::GetName(c.shader) << ":\n" << result.errors;
        EXPECT_FALSE(result.bytecode.empty()) << uz::EngineShaders::GetName(c.shader);
    }
}

#endif // _WIN32
