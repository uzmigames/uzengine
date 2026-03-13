/// UzEngine Shader Manager — Top-Level Shader Compilation Convenience Layer.
///
/// Wraps `ShaderPipeline`, `ShaderCompiler`, and `ShaderCompilerFXC` into a
/// single object that auto-detects bindless capability from the device and
/// provides one-call bytecode/shader access for engine shaders.
///
/// # Ownership
/// Owns the shader compilers (`ShaderCompiler`, `ShaderCompilerFXC`) and the
/// `ShaderPipeline`. Does NOT own the `IRHIDevice` — the device must outlive
/// the manager.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render/init thread (same
/// constraint as `ShaderPipeline`).
///
/// # Examples
/// ```cpp
/// ShaderManager shaders;
/// if (!shaders.Init(device)) { /* handle failure */ }
///
/// auto vs = shaders.CreateShader(EngineShader::MeshVS, RHIShaderStage::Vertex);
/// auto ps = shaders.CreateShader(EngineShader::MeshPS, RHIShaderStage::Pixel);
/// ```

#pragma once

#include "engine_shaders.h"
#include "rhi_types.h"
#include "shader_pipeline.h"

#include <memory>
#include <span>

namespace uz {

class IRHIDevice;
class ShaderCompiler;
class ShaderCompilerFXC;

/// Top-level convenience for shader compilation.
///
/// Auto-detects bindless from the device, configures the full shader pipeline
/// (VFS, cache, compilers), and provides single-call bytecode/shader access
/// for engine shaders.
class ShaderManager {
public:
    ShaderManager() = default;
    ~ShaderManager();

    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;
    ShaderManager(ShaderManager&&) = delete;
    ShaderManager& operator=(ShaderManager&&) = delete;

    /// Initialize the shader subsystem.
    ///
    /// Must be called before any other method. Auto-detects bindless support
    /// from `device->IsBindless()`, creates compilers, configures cache directory
    /// and VFS mounts for engine shaders.
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this manager.
    ///
    /// # Returns
    /// `true` on success, `false` if any subsystem fails to initialize.
    [[nodiscard]] bool Init(IRHIDevice* device);

    /// Get compiled bytecode for an engine shader.
    ///
    /// Compiles on first access via the shader pipeline, then returns the
    /// cached result on subsequent calls. The returned span is valid for the
    /// lifetime of this manager.
    ///
    /// # Arguments
    /// * `shader` — Engine shader enum value identifying the shader to compile.
    ///
    /// # Returns
    /// Non-empty span containing DXIL/DXBC bytecode on success.
    /// Empty span on compilation failure (error is logged).
    [[nodiscard]] std::vector<uint8_t> GetBytecode(EngineShader shader);

    /// Compile and create an RHI shader handle in one call.
    ///
    /// Gets bytecode via `GetBytecode()`, then creates a device shader object.
    /// The shader stage must match the shader type (Vertex for `*_vs`, Pixel
    /// for `*_ps`, etc.) — no automatic stage inference is performed.
    ///
    /// # Arguments
    /// * `shader` — Engine shader enum value.
    /// * `stage`  — Shader stage (must match the shader's actual stage).
    ///
    /// # Returns
    /// Valid `RHIShaderHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIShaderHandle CreateShader(EngineShader shader, RHIShaderStage stage);

    /// Shutdown and release all resources.
    ///
    /// Resets compilers and pipeline. Safe to call multiple times or without
    /// prior `Init()`. Called automatically by the destructor.
    void Shutdown();

    /// Returns true if `Init()` has been called successfully and `Shutdown()`
    /// has not been called since.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    IRHIDevice* m_device = nullptr;
    std::unique_ptr<ShaderCompiler> m_compiler;
    std::unique_ptr<ShaderCompilerFXC> m_compilerFXC;
    ShaderPipeline m_pipeline;
    bool m_initialized = false;
    bool m_bindless = false;
};

} // namespace uz
