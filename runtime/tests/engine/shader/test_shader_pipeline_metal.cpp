/// ShaderPipeline macOS (Slang→MSL) tests
/// (task phase1_port-metal-shader-pipeline, Phase 2).
///
/// The Slang→MSL ShaderPipeline is the `#else // !_WIN32` implementation in
/// shader_pipeline.cpp; Windows builds compile the DXC/FXC implementation of the
/// same class instead, so these tests are compiled where the code under test is.
/// Where libslang cannot be loaded at runtime they skip via IsInitialized().

#ifndef _WIN32

#include <gtest/gtest.h>

#include "shader_pipeline.h"

#include <cstring>

namespace {

const char* kFlatPS = R"(
float4 main(float3 color : COLOR) : SV_Target { return float4(color, 1.0); }
)";

const char* kFullscreenVS = R"(
struct FSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
FSOut main(uint vid : SV_VertexID) {
    FSOut o;
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";

// Bytecode on macOS/Metal is MSL source text (Metal compiles MSL at
// CreateShader time) — decode as UTF-8 for assertions.
std::string AsText(const std::vector<uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

TEST(ShaderPipelineMetal, LoadBytecodeFromSource_ReturnsMSLText) {
    uz::ShaderPipeline pipeline;
    uz::ShaderPipeline::Config config{};
    pipeline.Init(config);
    if (!pipeline.IsInitialized()) {
        GTEST_SKIP() << "libslang unavailable";
    }

    const auto bytes = pipeline.LoadBytecodeFromSource("flat_ps.hlsl", kFlatPS);
    ASSERT_FALSE(bytes.empty());
    const std::string msl = AsText(bytes);
    EXPECT_NE(msl.find("#include <metal_stdlib>"), std::string::npos);
    EXPECT_NE(msl.find("[[fragment]]"), std::string::npos);
    pipeline.Shutdown();
}

TEST(ShaderPipelineMetal, VertexShader_HasVertexEntry) {
    uz::ShaderPipeline pipeline;
    uz::ShaderPipeline::Config config{};
    pipeline.Init(config);
    if (!pipeline.IsInitialized()) {
        GTEST_SKIP() << "libslang unavailable";
    }

    const auto bytes = pipeline.LoadBytecodeFromSource("fullscreen_vs.hlsl", kFullscreenVS);
    ASSERT_FALSE(bytes.empty());
    const std::string msl = AsText(bytes);
    EXPECT_NE(msl.find("[[vertex]]"), std::string::npos);
    EXPECT_NE(msl.find("main_0"), std::string::npos);
    pipeline.Shutdown();
}

TEST(ShaderPipelineMetal, SecondCallServedFromCache) {
    uz::ShaderPipeline pipeline;
    uz::ShaderPipeline::Config config{};
    pipeline.Init(config);
    if (!pipeline.IsInitialized()) {
        GTEST_SKIP() << "libslang unavailable";
    }

    const auto first = pipeline.LoadBytecodeFromSource("flat_ps.hlsl", kFlatPS);
    const auto second = pipeline.LoadBytecodeFromSource("flat_ps.hlsl", kFlatPS);
    ASSERT_FALSE(first.empty());
    EXPECT_EQ(first, second);
    pipeline.Shutdown();
}

TEST(ShaderPipelineMetal, InvalidSource_ReturnsEmpty) {
    uz::ShaderPipeline pipeline;
    uz::ShaderPipeline::Config config{};
    pipeline.Init(config);
    if (!pipeline.IsInitialized()) {
        GTEST_SKIP() << "libslang unavailable";
    }

    const auto bytes = pipeline.LoadBytecodeFromSource(
        "broken_ps.hlsl", "float4 main( { syntax error");
    EXPECT_TRUE(bytes.empty());
    pipeline.Shutdown();
}

TEST(ShaderPipelineMetal, BindlessDefine_NeverInjectedOnMetal) {
    // The SM5 register-bound path is the only Metal-compatible one — passing
    // UZ_BINDLESS in defines must not reach the compiler.
    uz::ShaderPipeline pipeline;
    uz::ShaderPipeline::Config config{};
    config.bindless = true;  // must be ignored for the Metal target
    pipeline.Init(config);
    if (!pipeline.IsInitialized()) {
        GTEST_SKIP() << "libslang unavailable";
    }

    const char* src = R"(
#ifdef UZ_BINDLESS
#error bindless must be off on metal
#endif
float4 main() : SV_Target { return float4(1, 1, 1, 1); }
)";
    const auto bytes = pipeline.LoadBytecodeFromSource("bindless_probe_ps.hlsl", src);
    EXPECT_FALSE(bytes.empty());
    pipeline.Shutdown();
}

} // namespace

#endif // !_WIN32
