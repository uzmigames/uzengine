/// Tests for ShaderCompiler — DXC dynamic loading and HLSL compilation.
///
/// These tests require dxcompiler.dll to be accessible at runtime.
/// Tests are skipped (not failed) if DXC cannot be loaded.

#ifdef _WIN32

#include "shader_compiler.h"
#include <gtest/gtest.h>

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

class ShaderCompilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_initialized = m_compiler.Init();
        if (!m_initialized) {
            GTEST_SKIP() << "DXC not available (dxcompiler.dll not next to test exe)";
        }
    }

    uz::ShaderCompiler m_compiler;
    bool m_initialized = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Minimal HLSL Sources
// ─────────────────────────────────────────────────────────────────────────────

static const char* kSimpleVS = R"(
float4 main(uint vid : SV_VertexID) : SV_Position {
    return float4(0, 0, 0, 1);
}
)";

static const char* kSimplePS = R"(
float4 main() : SV_Target {
    return float4(1, 0, 0, 1);
}
)";

static const char* kSimpleCS = R"(
RWBuffer<float> output : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    output[id.x] = float(id.x);
}
)";

static const char* kInvalidHLSL = R"(
this is not valid hlsl code at all!!!
)";

static const char* kDefineShader = R"(
float4 main() : SV_Target {
#ifdef USE_RED
    return float4(1, 0, 0, 1);
#else
    return float4(0, 1, 0, 1);
#endif
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderCompilerInitTest, UninitializedByDefault) {
    uz::ShaderCompiler compiler;
    EXPECT_FALSE(compiler.IsInitialized());
}

TEST(ShaderCompilerInitTest, InitFailsWithBadPath) {
    uz::ShaderCompiler compiler;
    EXPECT_FALSE(compiler.Init("nonexistent_dxc_library.dll"));
    EXPECT_FALSE(compiler.IsInitialized());
}

TEST(ShaderCompilerInitTest, DoubleInitIsNoOp) {
    uz::ShaderCompiler compiler;
    if (!compiler.Init()) {
        GTEST_SKIP() << "DXC not available";
    }
    EXPECT_TRUE(compiler.Init());
    EXPECT_TRUE(compiler.IsInitialized());
}

TEST(ShaderCompilerInitTest, ShutdownMakesUninitialised) {
    uz::ShaderCompiler compiler;
    if (!compiler.Init()) {
        GTEST_SKIP() << "DXC not available";
    }
    compiler.Shutdown();
    EXPECT_FALSE(compiler.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Version
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCompilerTest, GetVersionReturnsNonEmpty) {
    auto version = m_compiler.GetVersion();
    EXPECT_FALSE(version.empty());
    EXPECT_NE(version, "unknown");
    // Version should contain a dot (e.g., "1.8")
    EXPECT_NE(version.find('.'), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile — Success Cases
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCompilerTest, CompileVertexShader) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimpleVS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Vertex;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::DXIL;

    auto result = m_compiler.Compile(desc);
    EXPECT_TRUE(result.success) << "Errors: " << result.errors;
    EXPECT_FALSE(result.bytecode.empty());
    EXPECT_GT(result.bytecode.size(), 16u);
}

TEST_F(ShaderCompilerTest, CompilePixelShader) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimplePS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Pixel;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::DXIL;

    auto result = m_compiler.Compile(desc);
    EXPECT_TRUE(result.success) << "Errors: " << result.errors;
    EXPECT_FALSE(result.bytecode.empty());
}

TEST_F(ShaderCompilerTest, CompileComputeShader) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimpleCS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Compute;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::DXIL;

    auto result = m_compiler.Compile(desc);
    EXPECT_TRUE(result.success) << "Errors: " << result.errors;
    EXPECT_FALSE(result.bytecode.empty());
}

TEST_F(ShaderCompilerTest, CompileSM6_0) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimpleVS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Vertex;
    desc.model = uz::ShaderModelCompile::SM6_0;
    desc.output = uz::ShaderOutputFormat::DXIL;

    auto result = m_compiler.Compile(desc);
    EXPECT_TRUE(result.success) << "Errors: " << result.errors;
    EXPECT_FALSE(result.bytecode.empty());
}

TEST_F(ShaderCompilerTest, CompileSM5_1_MayFail) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimpleVS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Vertex;
    desc.model = uz::ShaderModelCompile::SM5_1;
    desc.output = uz::ShaderOutputFormat::DXIL;

    auto result = m_compiler.Compile(desc);
    // SM5.1 may not be supported by all DXC versions (e.g., DXC 1.8+)
    if (!result.success) {
        GTEST_SKIP() << "SM5.1 not supported by this DXC: " << result.errors;
    }
    EXPECT_FALSE(result.bytecode.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile — SPIRV Output
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCompilerTest, CompileToSPIRV) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimpleVS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Vertex;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::SPIRV;

    auto result = m_compiler.Compile(desc);
    EXPECT_TRUE(result.success) << "Errors: " << result.errors;
    EXPECT_FALSE(result.bytecode.empty());

    // SPIR-V magic number: 0x07230203
    ASSERT_GE(result.bytecode.size(), 4u);
    uint32_t magic = *reinterpret_cast<const uint32_t*>(result.bytecode.data());
    EXPECT_EQ(magic, 0x07230203u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile — Error Cases
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCompilerTest, CompileInvalidHLSLFails) {
    uz::ShaderCompileDesc desc;
    desc.source = kInvalidHLSL;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Pixel;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::DXIL;

    auto result = m_compiler.Compile(desc);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.bytecode.empty());
    EXPECT_FALSE(result.errors.empty());
}

TEST_F(ShaderCompilerTest, CompileUninitializedFails) {
    uz::ShaderCompiler uninit;
    uz::ShaderCompileDesc desc;
    desc.source = kSimpleVS;

    auto result = uninit.Compile(desc);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errors, "ShaderCompiler not initialized");
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile — Preprocessor Defines
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCompilerTest, CompileWithDefines) {
    uz::ShaderCompileDesc desc;
    desc.source = kDefineShader;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Pixel;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::DXIL;
    desc.defines = {"USE_RED"};

    auto result = m_compiler.Compile(desc);
    EXPECT_TRUE(result.success) << "Errors: " << result.errors;
    EXPECT_FALSE(result.bytecode.empty());
}

TEST_F(ShaderCompilerTest, DifferentDefinesProduceDifferentBytecode) {
    uz::ShaderCompileDesc descRed;
    descRed.source = kDefineShader;
    descRed.entryPoint = "main";
    descRed.stage = uz::ShaderStageCompile::Pixel;
    descRed.model = uz::ShaderModelCompile::SM6_6;
    descRed.output = uz::ShaderOutputFormat::DXIL;
    descRed.defines = {"USE_RED"};

    uz::ShaderCompileDesc descGreen = descRed;
    descGreen.defines = {};

    auto resultRed = m_compiler.Compile(descRed);
    auto resultGreen = m_compiler.Compile(descGreen);

    ASSERT_TRUE(resultRed.success);
    ASSERT_TRUE(resultGreen.success);
    EXPECT_NE(resultRed.bytecode, resultGreen.bytecode);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile — Debug / Optimization Flags
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCompilerTest, CompileWithDebugInfo) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimplePS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Pixel;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::DXIL;
    desc.generateDebugInfo = true;

    auto result = m_compiler.Compile(desc);
    EXPECT_TRUE(result.success) << "Errors: " << result.errors;
    EXPECT_FALSE(result.bytecode.empty());
}

TEST_F(ShaderCompilerTest, CompileWithDisabledOptimizations) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimplePS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Pixel;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::DXIL;
    desc.disableOptimizations = true;

    auto result = m_compiler.Compile(desc);
    EXPECT_TRUE(result.success) << "Errors: " << result.errors;
    EXPECT_FALSE(result.bytecode.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Bytecode Determinism
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCompilerTest, SameInputProducesSameBytecode) {
    uz::ShaderCompileDesc desc;
    desc.source = kSimplePS;
    desc.entryPoint = "main";
    desc.stage = uz::ShaderStageCompile::Pixel;
    desc.model = uz::ShaderModelCompile::SM6_6;
    desc.output = uz::ShaderOutputFormat::DXIL;

    auto result1 = m_compiler.Compile(desc);
    auto result2 = m_compiler.Compile(desc);

    ASSERT_TRUE(result1.success);
    ASSERT_TRUE(result2.success);
    EXPECT_EQ(result1.bytecode, result2.bytecode);
}

#endif // _WIN32
