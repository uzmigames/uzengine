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
enum class ShaderStageCompile : uint8_t {
    Vertex,
    Pixel,
    Compute,
    Geometry,
    Hull,
    Domain,
    Amplification,
    Mesh,
};

/// Shader model version for compilation target profile.
enum class ShaderModelCompile : uint8_t {
    SM5_1,
    SM6_0,
    SM6_6,
};

/// Output format for compiled bytecode.
enum class ShaderOutputFormat : uint8_t {
    DXIL,     ///< DX12 native bytecode
    SPIRV,    ///< Vulkan SPIR-V bytecode
};

/// Description of a shader compilation job.
struct ShaderCompileDesc {
    std::string source;                  ///< HLSL source code
    std::string entryPoint = "main";     ///< Entry point function name
    std::string sourceName;              ///< Source filename (for error messages)
    ShaderStageCompile stage = ShaderStageCompile::Vertex;
    ShaderModelCompile model = ShaderModelCompile::SM6_6;
    ShaderOutputFormat output = ShaderOutputFormat::DXIL;
    std::vector<std::string> defines;    ///< Preprocessor defines (e.g., "USE_PBR=1")
    bool generateDebugInfo = false;      ///< Include debug info (-Zi)
    bool disableOptimizations = false;   ///< Disable optimizations (-Od)
};

/// Result of a shader compilation.
struct ShaderCompileResult {
    bool success = false;
    std::vector<uint8_t> bytecode;       ///< Compiled bytecode (DXIL or SPIRV)
    std::string errors;                  ///< Error/warning messages from DXC
};

/// Dynamically loads DXC and compiles HLSL shaders.
///
/// Loads dxcompiler.dll on Init(), creates IDxcCompiler3 and IDxcUtils.
/// Call Compile() to compile individual shaders.
class ShaderCompiler {
public:
    ShaderCompiler() = default;
    ~ShaderCompiler();

    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    /// Loads dxcompiler.dll from the given path and creates DXC objects.
    ///
    /// Returns false if the DLL cannot be loaded or DXC objects cannot be created.
    bool Init(const std::string& dxcPath = "dxcompiler.dll");

    /// Returns true if DXC is loaded and ready.
    [[nodiscard]] bool IsInitialized() const { return m_compiler != nullptr; }

    /// Compiles a shader from HLSL source.
    [[nodiscard]] ShaderCompileResult Compile(const ShaderCompileDesc& desc);

    /// Returns the DXC compiler version string, or empty if not initialized.
    [[nodiscard]] std::string GetVersion() const;

    /// Shuts down DXC and unloads the DLL.
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
