/// SlangCompiler smoke tests — macOS HLSL→MSL toolchain bring-up
/// (task phase1_port-metal-shader-pipeline, Phase 1).

#include <gtest/gtest.h>

#include "slang_compiler.h"

namespace {

const char* kTriangleVS = R"(
struct VSOut { float4 pos : SV_Position; float3 color : COLOR; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    float2 p[3] = { float2(0, 0.5), float2(-0.5, -0.5), float2(0.5, -0.5) };
    o.pos = float4(p[vid], 0, 1);
    o.color = float3(vid == 0, vid == 1, vid == 2);
    return o;
}
)";

const char* kFlatPS = R"(
float4 main(float3 color : COLOR) : SV_Target { return float4(color, 1.0); }
)";

const char* kAccumCS = R"(
RWStructuredBuffer<float> data : register(u0);
[numthreads(4, 1, 1)]
void main(uint tid : SV_DispatchThreadID) { data[tid] += 1.0; }
)";

class SlangCompilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!m_compiler.Init()) {
            GTEST_SKIP() << "libslang.dylib not found (see thirdparty/slang)";
        }
    }
    uz::SlangCompiler m_compiler;
};

TEST_F(SlangCompilerTest, VertexShader_CompilesToMSL) {
    uz::SlangCompileDesc desc{};
    desc.source       = kTriangleVS;
    desc.entryPoint   = "main";
    desc.sourceName   = "triangle_vs";
    desc.stage        = uz::SlangShaderStage::Vertex;
    desc.outputFormat = uz::SlangOutputFormat::MSL;

    const auto result = m_compiler.Compile(desc);
    ASSERT_TRUE(result.success) << result.errors;
    EXPECT_NE(result.output.find("[[vertex]]"), std::string::npos);
    // Slang renames the HLSL `main` entry point to `main_0` in MSL.
    EXPECT_NE(result.output.find("main_0"), std::string::npos);
}

TEST_F(SlangCompilerTest, PixelShader_CompilesToMSL) {
    uz::SlangCompileDesc desc{};
    desc.source       = kFlatPS;
    desc.entryPoint   = "main";
    desc.sourceName   = "flat_ps";
    desc.stage        = uz::SlangShaderStage::Pixel;
    desc.outputFormat = uz::SlangOutputFormat::MSL;

    const auto result = m_compiler.Compile(desc);
    ASSERT_TRUE(result.success) << result.errors;
    EXPECT_NE(result.output.find("[[fragment]]"), std::string::npos);
}

TEST_F(SlangCompilerTest, ComputeShader_CompilesToMSL) {
    uz::SlangCompileDesc desc{};
    desc.source       = kAccumCS;
    desc.entryPoint   = "main";
    desc.sourceName   = "accum_cs";
    desc.stage        = uz::SlangShaderStage::Compute;
    desc.outputFormat = uz::SlangOutputFormat::MSL;

    const auto result = m_compiler.Compile(desc);
    ASSERT_TRUE(result.success) << result.errors;
    EXPECT_NE(result.output.find("[[kernel]]"), std::string::npos);
}

TEST_F(SlangCompilerTest, InvalidHLSL_ReportsDiagnostics) {
    uz::SlangCompileDesc desc{};
    desc.source       = "float4 main( { syntax error";
    desc.entryPoint   = "main";
    desc.sourceName   = "broken_ps";
    desc.stage        = uz::SlangShaderStage::Pixel;
    desc.outputFormat = uz::SlangOutputFormat::MSL;

    const auto result = m_compiler.Compile(desc);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errors.empty());
}

} // namespace
