/// Tests for RHI configuration resolution (rhi_config.h/.cpp).

#include "rhi_config.h"
#include "config.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// ResolveRHIConfig — Default Behavior
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, DefaultConfigUsesPlatformBackend) {
    Config config;
    const auto rhi = ResolveRHIConfig(config);

#ifdef _WIN32
    EXPECT_EQ(rhi.backend, RHIBackend::DX12);
#elif defined(__APPLE__)
    EXPECT_EQ(rhi.backend, RHIBackend::Metal);
#else
    EXPECT_EQ(rhi.backend, RHIBackend::Vulkan);
#endif
    EXPECT_FALSE(rhi.shaderModel.has_value()); // auto-negotiate by default
    EXPECT_FALSE(rhi.enableValidation);
    EXPECT_FALSE(rhi.enableGpuValidation);
    EXPECT_FALSE(rhi.enableBreadcrumbs);
    EXPECT_EQ(rhi.adapterIndex, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveRHIConfig — CLI Backend Override
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, CLIOverrideBackendVulkan) {
    Config config;
    const char* argv[] = {"test", "--rhi=vulkan"};
    config.ApplyCLIOverrides(2, argv);
    const auto rhi = ResolveRHIConfig(config);
    EXPECT_EQ(rhi.backend, RHIBackend::Vulkan);
}

TEST(RHIConfig, CLIOverrideBackendDX12) {
    Config config;
    const char* argv[] = {"test", "--rhi=dx12"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(ResolveRHIConfig(config).backend, RHIBackend::DX12);
}

TEST(RHIConfig, CLIOverrideBackendMetal) {
    Config config;
    const char* argv[] = {"test", "--rhi=metal"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(ResolveRHIConfig(config).backend, RHIBackend::Metal);
}

TEST(RHIConfig, CLIOverrideBackendNull) {
    // Note: --rhi=null via CLI is parsed as JSON null by Config (merge_patch
    // semantics delete the key). Use project config to test the Null backend.
    Config config;
    config.LoadFromString(R"({"rhi":"null"})");
    EXPECT_EQ(ResolveRHIConfig(config).backend, RHIBackend::Null);
}

// ─────────────────────────────────────────────────────────────────────────────
// Backend Parsing — Case Insensitivity
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, BackendParsingCaseInsensitive) {
    const char* variants[] = {"DX12", "Dx12", "dx12", "dX12"};
    for (const auto* v : variants) {
        Config config;
        const std::string arg = std::string("--rhi=") + v;
        const char* argv[] = {"test", arg.c_str()};
        config.ApplyCLIOverrides(2, argv);
        EXPECT_EQ(ResolveRHIConfig(config).backend, RHIBackend::DX12)
            << "Failed for variant: " << v;
    }
}

TEST(RHIConfig, VulkanParsingCaseInsensitive) {
    Config config;
    const char* argv[] = {"test", "--rhi=VULKAN"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(ResolveRHIConfig(config).backend, RHIBackend::Vulkan);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Model Parsing
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, ShaderModelSM5) {
    Config config;
    const char* argv[] = {"test", "--shader-model=sm5"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(*ResolveRHIConfig(config).shaderModel, ShaderModel::SM5_0);
}

TEST(RHIConfig, ShaderModelSM5_0Explicit) {
    Config config;
    const char* argv[] = {"test", "--shader-model=sm5.0"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(*ResolveRHIConfig(config).shaderModel, ShaderModel::SM5_0);
}

TEST(RHIConfig, ShaderModelSM5_1) {
    Config config;
    const char* argv[] = {"test", "--shader-model=sm5.1"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(*ResolveRHIConfig(config).shaderModel, ShaderModel::SM5_1);
}

TEST(RHIConfig, ShaderModelSM6) {
    Config config;
    const char* argv[] = {"test", "--shader-model=sm6"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(*ResolveRHIConfig(config).shaderModel, ShaderModel::SM6_0);
}

TEST(RHIConfig, ShaderModelSM6_6) {
    Config config;
    const char* argv[] = {"test", "--shader-model=sm6.6"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(*ResolveRHIConfig(config).shaderModel, ShaderModel::SM6_6);
}

TEST(RHIConfig, ShaderModelCaseInsensitive) {
    Config config;
    const char* argv[] = {"test", "--shader-model=SM6.6"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(*ResolveRHIConfig(config).shaderModel, ShaderModel::SM6_6);
}

// ─────────────────────────────────────────────────────────────────────────────
// Project Config Fallback
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, ProjectConfigFallbackBackend) {
    Config config;
    config.LoadFromString(R"({"render":{"rhi":"vulkan"}})");
    EXPECT_EQ(ResolveRHIConfig(config).backend, RHIBackend::Vulkan);
}

TEST(RHIConfig, ProjectConfigFallbackShaderModel) {
    Config config;
    config.LoadFromString(R"({"render":{"shader_model":"sm5.1"}})");
    EXPECT_EQ(*ResolveRHIConfig(config).shaderModel, ShaderModel::SM5_1);
}

TEST(RHIConfig, CLIOverridesProjectConfig) {
    Config config;
    config.LoadFromString(R"({"render":{"rhi":"metal"}})");
    const char* argv[] = {"test", "--rhi=vulkan"};
    config.ApplyCLIOverrides(2, argv);
    // CLI "rhi" key should take priority over project "render.rhi"
    EXPECT_EQ(ResolveRHIConfig(config).backend, RHIBackend::Vulkan);
}

// ─────────────────────────────────────────────────────────────────────────────
// Boolean Flags
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, GpuValidationFlag) {
    Config config;
    const char* argv[] = {"test", "--gpu-validation"};
    config.ApplyCLIOverrides(2, argv);
    const auto rhi = ResolveRHIConfig(config);
    EXPECT_TRUE(rhi.enableValidation);
    EXPECT_TRUE(rhi.enableGpuValidation);
}

TEST(RHIConfig, GpuBreadcrumbsFlag) {
    Config config;
    const char* argv[] = {"test", "--gpu-breadcrumbs"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_TRUE(ResolveRHIConfig(config).enableBreadcrumbs);
}

TEST(RHIConfig, GpuValidationFromProject) {
    Config config;
    config.LoadFromString(R"({"render":{"gpu_validation":true}})");
    EXPECT_TRUE(ResolveRHIConfig(config).enableValidation);
}

// ─────────────────────────────────────────────────────────────────────────────
// Adapter Index
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, AdapterIndexFromCLI) {
    Config config;
    const char* argv[] = {"test", "--adapter=2"};
    config.ApplyCLIOverrides(2, argv);
    EXPECT_EQ(ResolveRHIConfig(config).adapterIndex, 2u);
}

TEST(RHIConfig, AdapterIndexFromProject) {
    Config config;
    config.LoadFromString(R"({"render":{"adapter":1}})");
    EXPECT_EQ(ResolveRHIConfig(config).adapterIndex, 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// RHIBackendToString / ShaderModelToString
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, BackendToStringAllValues) {
    EXPECT_STREQ(RHIBackendToString(RHIBackend::DX12), "DX12");
    EXPECT_STREQ(RHIBackendToString(RHIBackend::Vulkan), "Vulkan");
    EXPECT_STREQ(RHIBackendToString(RHIBackend::Metal), "Metal");
    EXPECT_STREQ(RHIBackendToString(RHIBackend::Null), "Null");
}

TEST(RHIConfig, ShaderModelToStringAllValues) {
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM5_0), "SM5.0");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM5_1), "SM5.1");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM6_0), "SM6.0");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM6_1), "SM6.1");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM6_2), "SM6.2");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM6_3), "SM6.3");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM6_4), "SM6.4");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM6_5), "SM6.5");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM6_6), "SM6.6");
    EXPECT_STREQ(ShaderModelToString(ShaderModel::SM6_7), "SM6.7");
}

// ─────────────────────────────────────────────────────────────────────────────
// ToDeviceDesc
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, ToDeviceDescCopiesFields) {
    RHIConfig cfg{};
    cfg.backend = RHIBackend::Vulkan;
    cfg.enableValidation = true;
    cfg.enableGpuValidation = true;
    cfg.enableBreadcrumbs = true;
    cfg.adapterIndex = 3;

    const auto desc = ToDeviceDesc(cfg);
    EXPECT_EQ(desc.backend, RHIBackend::Vulkan);
    EXPECT_TRUE(desc.enableValidation);
    EXPECT_TRUE(desc.enableGpuValidation);
    EXPECT_EQ(desc.adapterIndex, 3u);
}

TEST(RHIConfig, ToDeviceDescDefaultsMatchRHIConfig) {
    const auto desc = ToDeviceDesc(RHIConfig{});
    EXPECT_EQ(desc.backend, RHIBackend::DX12);
    EXPECT_FALSE(desc.enableValidation);
    EXPECT_FALSE(desc.enableGpuValidation);
    EXPECT_EQ(desc.adapterIndex, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Unknown / Invalid Input
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, UnknownBackendKeepsDefault) {
    Config config;
    const char* argv[] = {"test", "--rhi=opengl"};
    config.ApplyCLIOverrides(2, argv);
    const auto rhi = ResolveRHIConfig(config);
    // Should fall back to platform default
#ifdef _WIN32
    EXPECT_EQ(rhi.backend, RHIBackend::DX12);
#endif
}

TEST(RHIConfig, UnknownShaderModelKeepsDefault) {
    Config config;
    const char* argv[] = {"test", "--shader-model=sm7"};
    config.ApplyCLIOverrides(2, argv);
    // ParseShaderModel returns false for unknown "sm7", so the optional stays empty.
    EXPECT_FALSE(ResolveRHIConfig(config).shaderModel.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// Combined CLI Flags
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIConfig, MultipleCliFlags) {
    Config config;
    // Use "vulkan" instead of "null" for CLI test (--rhi=null triggers JSON
    // merge_patch deletion semantics in Config).
    const char* argv[] = {"test", "--rhi=vulkan", "--shader-model=sm5.1",
                          "--gpu-validation", "--gpu-breadcrumbs", "--adapter=1"};
    config.ApplyCLIOverrides(6, argv);
    const auto rhi = ResolveRHIConfig(config);
    EXPECT_EQ(rhi.backend, RHIBackend::Vulkan);
    EXPECT_EQ(*rhi.shaderModel, ShaderModel::SM5_1);
    EXPECT_TRUE(rhi.enableValidation);
    EXPECT_TRUE(rhi.enableBreadcrumbs);
    EXPECT_EQ(rhi.adapterIndex, 1u);
}
