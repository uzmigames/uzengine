/// Textured Quad Demo — DemoApplication-based backend-agnostic checkerboard quad.
///
/// Renders a procedural 256x256 checkerboard texture on a fullscreen-ish quad.
/// Automatically selects SM6.6 bindless or SM5 Input Assembler path.
/// Validates: texture upload pipeline, SRV binding, sampler creation, root constants.

#pragma once

#include "demo_application.h"
#include "rhi_types.h"

#include <cstdint>
#include <memory>

namespace uz {

class ShaderPipeline;
class ShaderCompiler;
class ShaderCompilerFXC;

/// Textured quad demo — draws a checkerboard quad using the RHI abstraction.
///
/// Extends `DemoApplication` to eliminate window/device/swapchain boilerplate.
/// All GPU resources are created in `OnInit()` and released in `OnShutdown()`.
class TexturedQuadDemo final : public DemoApplication {
public:
    TexturedQuadDemo();
    ~TexturedQuadDemo() override;

protected:
    void OnInit() override;
    void OnFrame(float dt) override;
    void OnShutdown() override;

private:
    /// Generates the 256x256 procedural checkerboard, creates GPU texture,
    /// uploads via staging buffer, and transitions to ShaderResource state.
    void CreateCheckerboardTexture();

    /// Compiles and creates vertex/pixel shader handles via ShaderPipeline.
    void CompileShaders();

    /// Creates the graphics pipeline (bindless or SM5 input-assembled).
    void CreatePipeline();

    /// Creates the quad vertex buffer (6 verts, 2 triangles).
    void CreateVertexBuffer();

    // ── GPU resources ───────────────────────────────────────────────────
    RHIPipelineHandle pipeline_;
    RHIShaderHandle vsHandle_;
    RHIShaderHandle psHandle_;
    RHIBufferHandle vbHandle_;
    RHITextureHandle texHandle_;
    RHISamplerHandle samplerHandle_;

    uint32_t texSrvIndex_ = UINT32_MAX;
    uint32_t vbSrvIndex_ = 0;
    uint32_t samplerIdx_ = UINT32_MAX;

    bool bindless_ = false;

    // ── Shader compilation (Windows only, forward-declared) ─────────────
    std::unique_ptr<ShaderPipeline> shaderPipeline_;
    std::unique_ptr<ShaderCompiler> compiler_;
    std::unique_ptr<ShaderCompilerFXC> compilerFXC_;
};

} // namespace uz
