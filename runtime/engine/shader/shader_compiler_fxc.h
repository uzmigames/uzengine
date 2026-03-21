/// UzEngine FXC Shader Compiler
///
/// Dynamically loads FXC (d3dcompiler_47.dll) and compiles HLSL to DXBC.
/// Used for Shader Model 5.0 targets — DX12 SM5 fallback and DX11 backend.
/// FXC is NOT linked at build time — loaded via LoadLibrary/GetProcAddress.
///
/// # Supported Targets
/// - DXBC SM5.0: `-T vs_5_0 / ps_5_0 / cs_5_0` (DX11 + DX12 fallback)
///
/// # When to Use
/// SM >= 6.0 → use ShaderCompiler (DXC). SM 5.0 → use this (FXC).
/// This follows the reference engine's dual-compiler pattern: `DoesShaderModelRequireDXC()`.
///
/// # Thread Safety
/// Each ShaderCompilerFXC instance owns its own DLL handle.
/// Multiple instances can compile in parallel on different threads.

#pragma once

#ifdef _WIN32

#include "shader_compiler.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Dynamically loads FXC and compiles HLSL shaders to DXBC bytecode.
///
/// Loads `d3dcompiler_47.dll` on `Init()`, resolves `D3DCompile` entry point.
/// Call `Compile()` to compile individual shaders targeting SM 5.0.
///
/// # Thread Safety
/// Each `ShaderCompilerFXC` instance owns its own DLL handle. Multiple
/// instances can compile in parallel on different threads. A single instance
/// must NOT be used from multiple threads concurrently.
///
/// # Lifetime
/// Owns the FXC DLL handle. `Shutdown()` is called automatically
/// by the destructor if not called explicitly.
///
/// # Examples
/// ```cpp
/// ShaderCompilerFXC fxc;
/// if (!fxc.Init("lib/d3dcompiler_47.dll")) { /* handle error */ }
///
/// ShaderCompileDesc desc;
/// desc.source = hlslCode;
/// desc.stage = ShaderStageCompile::Pixel;
/// desc.model = ShaderModelCompile::SM5_0;
///
/// auto result = fxc.Compile(desc);
/// if (result.success) { /* use result.bytecode (DXBC) */ }
/// ```
class ShaderCompilerFXC {
public:
    ShaderCompilerFXC() = default;
    ~ShaderCompilerFXC();

    ShaderCompilerFXC(const ShaderCompilerFXC&) = delete;
    ShaderCompilerFXC& operator=(const ShaderCompilerFXC&) = delete;

    /// Loads d3dcompiler_47.dll and resolves the D3DCompile entry point.
    ///
    /// # Arguments
    /// * `fxcPath` — Path to `d3dcompiler_47.dll`. Defaults to `../lib/d3dcompiler_47.dll`
    ///               relative to the executable (exe in build/bin/, DLLs in build/lib/).
    ///
    /// # Returns
    /// `true` if the DLL was loaded and `D3DCompile` resolved successfully.
    /// `false` if the DLL cannot be found/loaded or the entry point is missing.
    bool Init(const std::string& fxcPath = "../lib/d3dcompiler_47.dll");

    /// Returns true if FXC is loaded and ready to compile.
    [[nodiscard]] bool IsInitialized() const { return m_d3dCompile != nullptr; }

    /// Compiles a shader from HLSL source using FXC (D3DCompile API).
    ///
    /// Only supports SM 5.0 profiles. Passing SM 6.0+ will fail.
    ///
    /// # Arguments
    /// * `desc` — Full compilation descriptor (source, stage, model, defines, etc.).
    ///
    /// # Returns
    /// `ShaderCompileResult` with `success=true` and DXBC `bytecode` on success,
    /// or `success=false` with error messages in `errors`.
    [[nodiscard]] ShaderCompileResult Compile(const ShaderCompileDesc& desc);

    /// Shuts down FXC and unloads the DLL.
    ///
    /// Safe to call multiple times or without prior `Init()`.
    void Shutdown();

private:
    /// Returns the FXC target profile string (e.g., "vs_5_0", "ps_5_0").
    static std::string GetTargetProfile(ShaderStageCompile stage, ShaderModelCompile model);

    void* m_fxcModule = nullptr;    ///< HMODULE for d3dcompiler_47.dll
    void* m_d3dCompile = nullptr;   ///< D3DCompile function pointer
};

} // namespace uz

#endif // _WIN32
