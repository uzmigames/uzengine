/// UzEngine Shader Compiler
///
/// Dynamically loads DXC (dxcompiler.dll) and compiles HLSL to DXIL or SPIRV.
/// DXC is NOT linked at build time — it is loaded via LoadLibrary/GetProcAddress.
///
/// # Supported Targets
/// - DXIL SM6.6: `-T vs_6_6 / ps_6_6 / cs_6_6`
/// - DXIL SM6.0: `-T vs_6_0 / ps_6_0 / cs_6_0`
/// - DXIL SM5.1: `-T vs_5_1 / ps_5_1 / cs_5_1` (DXC, not FXC)
/// - SPIRV:      `-spirv -fspv-target-env=vulkan1.3`
///
/// # Thread Safety
/// Each ShaderCompiler instance owns its own DXC compiler/utils objects.
/// Multiple instances can compile in parallel on different threads.

#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

struct IDxcCompiler3;
struct IDxcUtils;

namespace uz {

/// Shader stage (determines profile string for DXC).
///
/// Maps to the DXC profile prefix: Vertex = "vs", Pixel = "ps", etc.
enum class ShaderStageCompile : uint8_t {
    Vertex,         ///< Vertex shader (vs_X_Y)
    Pixel,          ///< Pixel/fragment shader (ps_X_Y)
    Compute,        ///< Compute shader (cs_X_Y)
    Geometry,       ///< Geometry shader (gs_X_Y)
    Hull,           ///< Hull/tessellation control shader (hs_X_Y)
    Domain,         ///< Domain/tessellation evaluation shader (ds_X_Y)
    Amplification,  ///< Amplification shader (as_X_Y) — mesh shader pipeline
    Mesh,           ///< Mesh shader (ms_X_Y) — mesh shader pipeline
};

/// Shader model version for compilation target profile.
///
/// Combined with `ShaderStageCompile` to produce the DXC `-T` profile
/// string (e.g., `SM6_6` + `Vertex` = `vs_6_6`).
enum class ShaderModelCompile : uint8_t {
    SM5_0,  ///< Shader Model 5.0 — DX11/DX12 legacy, compiled via FXC (d3dcompiler_47.dll)
    SM5_1,  ///< Shader Model 5.1 — DX12 legacy, resource binding via registers
    SM6_0,  ///< Shader Model 6.0 — wave intrinsics
    SM6_1,  ///< Shader Model 6.1 — SV_Barycentrics, programmable sample positions
    SM6_2,  ///< Shader Model 6.2 — float16, denorm control
    SM6_3,  ///< Shader Model 6.3 — DirectX Raytracing 1.0
    SM6_4,  ///< Shader Model 6.4 — variable rate shading, low-precision integer dot
    SM6_5,  ///< Shader Model 6.5 — DXR 1.1, sampler feedback, mesh shaders
    SM6_6,  ///< Shader Model 6.6 — bindless (ResourceDescriptorHeap), 64-bit atomics
    SM6_7,  ///< Shader Model 6.7 — raw buffer load/store, relaxed casting
    SM6_8,  ///< Shader Model 6.8 — work graphs, wave size range, expanded comparison samplers
    SM6_9,  ///< Shader Model 6.9 — cooperative vectors, inline RT in compute, extended atomics
};

/// Output format for compiled bytecode.
enum class ShaderOutputFormat : uint8_t {
    DXIL,     ///< DX12 native bytecode
    SPIRV,    ///< Vulkan SPIR-V bytecode
};

/// Description of a shader compilation job.
///
/// Fully describes a single compilation unit. All fields have sensible defaults
/// for a vertex shader targeting SM6.6 DXIL output.
///
/// # Examples
/// ```cpp
/// ShaderCompileDesc desc;
/// desc.source = hlslSource;
/// desc.sourceName = "pbr.hlsl";
/// desc.stage = ShaderStageCompile::Pixel;
/// desc.defines = {"USE_PBR=1", "HAS_NORMAL_MAP=1"};
/// desc.generateDebugInfo = true; // dev build
/// ```
struct ShaderCompileDesc {
    std::string source;                  ///< HLSL source code (full text, not a path)
    std::string entryPoint = "main";     ///< Entry point function name
    std::string sourceName;              ///< Source filename (for DXC error messages)
    ShaderStageCompile stage = ShaderStageCompile::Vertex;   ///< Pipeline stage
    ShaderModelCompile model = ShaderModelCompile::SM6_6;    ///< Target shader model
    ShaderOutputFormat output = ShaderOutputFormat::DXIL;    ///< Output bytecode format
    std::vector<std::string> defines;    ///< Preprocessor defines (e.g., "USE_PBR=1")
    std::vector<std::string> includeDirs; ///< Include search directories (passed as `-I` to DXC)
    bool generateDebugInfo = false;      ///< Include debug info (`-Zi`) for PIX/RenderDoc
    bool disableOptimizations = false;   ///< Disable optimizations (`-Od`) for debugging
};

/// Result of a shader compilation.
///
/// On success, `bytecode` contains the compiled DXIL or SPIRV data. On failure,
/// `errors` contains the DXC error/warning output and `bytecode` is empty.
struct ShaderCompileResult {
    bool success = false;                ///< True if compilation succeeded without errors
    std::vector<uint8_t> bytecode;       ///< Compiled bytecode (DXIL or SPIRV). Empty on failure.
    std::string errors;                  ///< Error/warning messages from DXC. May be non-empty even on success (warnings).
};

/// Dynamically loads DXC and compiles HLSL shaders.
///
/// Loads `dxcompiler.dll` on `Init()`, creates `IDxcCompiler3` and `IDxcUtils`.
/// Call `Compile()` to compile individual shaders.
///
/// # Thread Safety
/// Each `ShaderCompiler` instance owns its own DXC COM objects. Multiple
/// instances can compile in parallel on different threads. A single instance
/// must NOT be used from multiple threads concurrently.
///
/// # Lifetime
/// Owns the DXC DLL handle and COM objects. `Shutdown()` is called automatically
/// by the destructor if not called explicitly.
///
/// # Examples
/// ```cpp
/// ShaderCompiler compiler;
/// if (!compiler.Init("lib/dxcompiler.dll")) { /* handle error */ }
///
/// ShaderCompileDesc desc;
/// desc.source = hlslCode;
/// desc.stage = ShaderStageCompile::Pixel;
/// desc.model = ShaderModelCompile::SM6_6;
///
/// auto result = compiler.Compile(desc);
/// if (result.success) { /* use result.bytecode */ }
/// ```
class ShaderCompiler {
public:
    ShaderCompiler() = default;
    ~ShaderCompiler();

    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    /// Loads dxcompiler.dll and creates DXC compiler/utils COM objects.
    ///
    /// # Arguments
    /// * `dxcPath` — Path to `dxcompiler.dll`. Defaults to `../lib/dxcompiler.dll`
    ///               relative to the executable (exe in build/bin/, DLLs in build/lib/).
    ///
    /// # Returns
    /// `true` if DXC was loaded and all COM objects created successfully.
    /// `false` if the DLL cannot be found/loaded or `DxcCreateInstance` fails.
    bool Init(const std::string& dxcPath = "../lib/dxcompiler.dll");

    /// Returns true if DXC is loaded and ready to compile.
    [[nodiscard]] bool IsInitialized() const { return m_compiler != nullptr; }

    /// Compiles a shader from HLSL source.
    ///
    /// # Arguments
    /// * `desc` — Full compilation descriptor (source, stage, model, defines, etc.).
    ///
    /// # Returns
    /// `ShaderCompileResult` with `success=true` and populated `bytecode` on success,
    /// or `success=false` with error messages in `errors`.
    [[nodiscard]] ShaderCompileResult Compile(const ShaderCompileDesc& desc);

    /// Returns the DXC compiler version string (e.g., "1.7.2308").
    ///
    /// # Returns
    /// Version string, or empty string if not initialized.
    [[nodiscard]] std::string GetVersion() const;

    /// Shuts down DXC, releases COM objects, and unloads the DLL.
    ///
    /// Safe to call multiple times or without prior `Init()`.
    void Shutdown();

private:
    /// Builds the DXC argument array from a compile description.
    std::vector<std::wstring> BuildArguments(const ShaderCompileDesc& desc) const;

    /// Returns the DXC target profile string (e.g., "vs_6_6", "ps_6_0").
    static std::wstring GetTargetProfile(ShaderStageCompile stage, ShaderModelCompile model);

    void* m_dxcModule = nullptr;         ///< HMODULE for dxcompiler.dll
    IDxcCompiler3* m_compiler = nullptr;
    IDxcUtils* m_utils = nullptr;
};

} // namespace uz

#endif // _WIN32
