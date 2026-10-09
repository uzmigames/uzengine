/// ShaderManager macOS (Slang→MSL→Metal) tests
/// (task phase1_port-metal-shader-pipeline, Phase 3).

#include <gtest/gtest.h>

#include "shader_manager.h"
// Complete types for the compilers ShaderManager owns through unique_ptr: this TU destroys a
// ShaderManager, so the deleters are instantiated here (merge 2026-10-05).
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "rhi.h"
#include "rhi_backend.h"

namespace {

class ShaderManagerMetalTest : public ::testing::Test {
protected:
    void SetUp() override {
        uz::RHIDeviceDesc desc{};
        desc.backend = uz::RHIBackend::Metal;
        m_device = uz::CreateRHIDevice(desc);
        if (!m_device) {
            GTEST_SKIP() << "Metal device creation failed (no GPU or macOS)";
        }
        if (!m_manager.Init(m_device.get())) {
            GTEST_SKIP() << "ShaderManager init failed (libslang unavailable?)";
        }
    }
    void TearDown() override {
        m_manager.Shutdown();
        m_device.reset();
    }

    std::unique_ptr<uz::IRHIDevice> m_device;
    uz::ShaderManager m_manager;
};

TEST_F(ShaderManagerMetalTest, Init_SucceedsOnMetalDevice) {
    EXPECT_TRUE(m_manager.IsInitialized());
}

TEST_F(ShaderManagerMetalTest, CreateShader_MeshVS_ReturnsValidHandle) {
    auto handle = m_manager.CreateShader(uz::EngineShader::MeshVS,
                                         uz::RHIShaderStage::Vertex);
    EXPECT_TRUE(handle.IsValid());
}

TEST_F(ShaderManagerMetalTest, CreateShader_GbufferPS_ReturnsValidHandle) {
    auto handle = m_manager.CreateShader(uz::EngineShader::GbufferPS,
                                         uz::RHIShaderStage::Pixel);
    EXPECT_TRUE(handle.IsValid());
}

TEST_F(ShaderManagerMetalTest, GetBytecode_DeferredLightingPS_IsMSLText) {
    const auto bytes = m_manager.GetBytecode(uz::EngineShader::DeferredLightingPS);
    ASSERT_FALSE(bytes.empty());
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    EXPECT_NE(text.find("#include <metal_stdlib>"), std::string::npos);
}

} // namespace
