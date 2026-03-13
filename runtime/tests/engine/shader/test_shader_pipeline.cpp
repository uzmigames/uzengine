/// Tests for ShaderPipeline — post-switch shader compilation orchestrator.
///
/// All tests run without DXC. They verify orchestration logic: lifecycle,
/// VFS integration, cache checking, error handling, and hot-reload polling.
///
/// Windows-only: ShaderPipeline depends on DXC/FXC which are Windows-only.

#ifdef _WIN32

#include "shader_pipeline.h"
#include "shader_source_manager.h"
#include "shader_vfs.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

class ShaderPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_testDir = "test_shader_pipeline_tmp";
        m_cacheDir = m_testDir + "/cache";
        m_shaderDir = m_testDir + "/shaders";

        fs::create_directories(m_shaderDir);
        fs::create_directories(m_cacheDir);
    }

    void TearDown() override {
        fs::remove_all(m_testDir);
    }

    static void WriteFile(const std::string& path, const std::string& content) {
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream f(path);
        f << content;
    }

    uz::ShaderPipeline::Config MakeConfig() const {
        uz::ShaderPipeline::Config cfg;
        cfg.cacheDir = m_cacheDir;
        cfg.targetBackend = "dx12_sm66";
        cfg.enableHotReload = false;
        return cfg;
    }

    std::string m_testDir;
    std::string m_cacheDir;
    std::string m_shaderDir;
};

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, IsNotInitializedByDefault) {
    uz::ShaderPipeline pipeline;
    EXPECT_FALSE(pipeline.IsInitialized());
}

TEST_F(ShaderPipelineTest, InitSetsInitializedState) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());
    EXPECT_TRUE(pipeline.IsInitialized());
}

TEST_F(ShaderPipelineTest, ShutdownClearsInitializedState) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());
    pipeline.Shutdown();
    EXPECT_FALSE(pipeline.IsInitialized());
}

TEST_F(ShaderPipelineTest, DoubleInitIsNoOp) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());
    EXPECT_TRUE(pipeline.IsInitialized());

    // Second Init should not crash or change state
    pipeline.Init(MakeConfig());
    EXPECT_TRUE(pipeline.IsInitialized());
}

TEST_F(ShaderPipelineTest, DoubleShutdownIsSafe) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());
    pipeline.Shutdown();
    EXPECT_FALSE(pipeline.IsInitialized());

    // Second Shutdown should not crash
    pipeline.Shutdown();
    EXPECT_FALSE(pipeline.IsInitialized());
}

TEST_F(ShaderPipelineTest, ShutdownWithoutInitIsSafe) {
    uz::ShaderPipeline pipeline;
    pipeline.Shutdown();
    EXPECT_FALSE(pipeline.IsInitialized());
}

TEST_F(ShaderPipelineTest, DestructorCallsShutdown) {
    {
        uz::ShaderPipeline pipeline;
        pipeline.Init(MakeConfig());
        EXPECT_TRUE(pipeline.IsInitialized());
    }
    // No crash = destructor handled cleanup
}

// ─────────────────────────────────────────────────────────────────────────────
// Config defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, ConfigDefaultValues) {
    uz::ShaderPipeline::Config cfg;
    EXPECT_EQ(cfg.cacheDir, "cache/shaders");
    EXPECT_EQ(cfg.targetBackend, "dx12_sm67");
    EXPECT_TRUE(cfg.enableHotReload);
}

// ─────────────────────────────────────────────────────────────────────────────
// VFS / Accessors
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, MountAddsToVFS) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());
    pipeline.Mount("/engine", m_shaderDir);

    EXPECT_EQ(pipeline.GetVFS().GetMountCount(), 1u);
}

TEST_F(ShaderPipelineTest, MountBeforeInitIsIgnored) {
    uz::ShaderPipeline pipeline;
    // Should not crash — just logs a warning
    pipeline.Mount("/engine", m_shaderDir);
    // Pipeline not initialized, so we can't check VFS
    EXPECT_FALSE(pipeline.IsInitialized());
}

TEST_F(ShaderPipelineTest, GetVFSReturnsReference) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());

    auto& vfs = pipeline.GetVFS();
    vfs.Mount("/test", m_shaderDir);
    EXPECT_EQ(pipeline.GetVFS().GetMountCount(), 1u);
}

TEST_F(ShaderPipelineTest, GetSourceManagerReturnsReference) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());

    auto& srcMgr = pipeline.GetSourceManager();
    EXPECT_EQ(srcMgr.GetCacheSize(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// SetCompiler
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, SetCompilerStoresReference) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());

    // We can't easily verify the stored pointer without exposing it,
    // but we can verify that setting nullptr doesn't crash
    pipeline.SetCompiler(nullptr);
    EXPECT_TRUE(pipeline.IsInitialized());
}

TEST_F(ShaderPipelineTest, SetCompilerBeforeInitIsIgnored) {
    uz::ShaderPipeline pipeline;
    // Should not crash
    pipeline.SetCompiler(nullptr);
    EXPECT_FALSE(pipeline.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// CompileAll
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, CompileAllWithNoMountsReturnsZeroTotal) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());

    auto result = pipeline.CompileAll();
    EXPECT_EQ(result.total, 0u);
    EXPECT_EQ(result.cached, 0u);
    EXPECT_EQ(result.compiled, 0u);
    EXPECT_EQ(result.failed, 0u);
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(ShaderPipelineTest, CompileAllBeforeInitReturnsError) {
    uz::ShaderPipeline pipeline;

    auto result = pipeline.CompileAll();
    EXPECT_EQ(result.total, 0u);
    EXPECT_FALSE(result.errors.empty());
    EXPECT_EQ(result.errors[0], "Pipeline not initialized");
}

TEST_F(ShaderPipelineTest, CompileAllWithNoCompilerReportsFailures) {
    WriteFile(m_shaderDir + "/test.hlsl", "float4 main() : SV_Target { return 0; }");

    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());
    pipeline.Mount("/shaders", m_shaderDir);
    // No compiler set

    auto result = pipeline.CompileAll();
    EXPECT_GE(result.total, 1u);
    EXPECT_EQ(result.compiled, 0u);
    EXPECT_GE(result.failed, 1u);
    EXPECT_FALSE(result.errors.empty());

    // Error should mention no compiler
    bool hasCompilerError = false;
    for (const auto& err : result.errors) {
        if (err.find("No DXC compiler") != std::string::npos ||
            err.find("No FXC compiler") != std::string::npos) {
            hasCompilerError = true;
            break;
        }
    }
    EXPECT_TRUE(hasCompilerError);
}

TEST_F(ShaderPipelineTest, CompileAllWithPermutationsExpandsVariants) {
    const std::string source =
        "#pragma uz_permutation USE_ALPHA\n"
        "float4 main() : SV_Target { return 0; }";
    WriteFile(m_shaderDir + "/perm.hlsl", source);

    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());
    pipeline.Mount("/shaders", m_shaderDir);
    // No compiler — all variants will fail, but total should be 2

    auto result = pipeline.CompileAll();
    EXPECT_EQ(result.total, 2u);
    EXPECT_EQ(result.failed, 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// CompileShader
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, CompileShaderWithNonExistentPathReturnsFalse) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());

    EXPECT_FALSE(pipeline.CompileShader("/nonexistent/shader.hlsl"));
}

TEST_F(ShaderPipelineTest, CompileShaderBeforeInitReturnsFalse) {
    uz::ShaderPipeline pipeline;
    EXPECT_FALSE(pipeline.CompileShader("/any/path.hlsl"));
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadBytecode
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, LoadBytecodeWithNonExistentPathReturnsEmpty) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());

    auto bytecode = pipeline.LoadBytecode("/nonexistent/shader.hlsl");
    EXPECT_TRUE(bytecode.empty());
}

TEST_F(ShaderPipelineTest, LoadBytecodeBeforeInitReturnsEmpty) {
    uz::ShaderPipeline pipeline;

    auto bytecode = pipeline.LoadBytecode("/any/path.hlsl");
    EXPECT_TRUE(bytecode.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// PollHotReload
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, PollHotReloadWithNoChangesReturnsEmpty) {
    uz::ShaderPipeline pipeline;
    pipeline.Init(MakeConfig());

    auto recompiled = pipeline.PollHotReload();
    EXPECT_TRUE(recompiled.empty());
}

TEST_F(ShaderPipelineTest, PollHotReloadBeforeInitReturnsEmpty) {
    uz::ShaderPipeline pipeline;

    auto recompiled = pipeline.PollHotReload();
    EXPECT_TRUE(recompiled.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Re-Init after Shutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPipelineTest, ReInitAfterShutdownWorks) {
    uz::ShaderPipeline pipeline;

    pipeline.Init(MakeConfig());
    EXPECT_TRUE(pipeline.IsInitialized());
    pipeline.Mount("/engine", m_shaderDir);
    EXPECT_EQ(pipeline.GetVFS().GetMountCount(), 1u);

    pipeline.Shutdown();
    EXPECT_FALSE(pipeline.IsInitialized());

    // Re-init with fresh state
    pipeline.Init(MakeConfig());
    EXPECT_TRUE(pipeline.IsInitialized());
    EXPECT_EQ(pipeline.GetVFS().GetMountCount(), 0u);
}

#endif // _WIN32
