/// Shader-Slang compiler for HLSL -> MSL (Metal) and HLSL -> SPIR-V compilation.
///
/// Wraps the Slang compilation API (slang_createGlobalSession + ISession) to
/// provide direct HLSL -> MSL output for the Metal backend. Can also produce
/// SPIR-V, potentially replacing DXC for Vulkan in the future.
///
/// # Dynamic Loading
/// Slang is loaded at runtime via `dlopen` (macOS/Linux) or `LoadLibrary` (Windows).
/// If the Slang shared library is not found, `Init()` returns false and `Compile()`
/// returns an error result. This matches the DXC compiler's graceful fallback pattern.
///
/// # Usage
/// ```cpp
/// SlangCompiler compiler;
/// if (compiler.Init()) {
///     SlangCompileDesc desc{};
///     desc.source = hlslCode;
///     desc.entryPoint = "VSMain";
///     desc.stage = SlangShaderStage::Vertex;
///     desc.outputFormat = SlangOutputFormat::MSL;
///     auto result = compiler.Compile(desc);
///     if (result.success) {
///         // result.output contains MSL source code
///     }
/// }
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Each thread should use its own SlangCompiler instance.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Shader stage for Slang compilation.
///
/// Maps to Slang's internal stage enum values. Metal uses "vertex" / "fragment" /
/// "kernel" terminology, but we keep the DirectX naming convention for consistency
/// with the rest of the engine.
enum class SlangShaderStage : uint8_t {
    Vertex,    ///< Vertex shader (Metal: vertex function)
    Pixel,     ///< Pixel/fragment shader (Metal: fragment function)
    Compute,   ///< Compute shader (Metal: kernel function)
};

/// Output format for Slang compilation.
///
/// Determines the code generation target. MSL is the primary use case for the
/// Metal backend. SPIR-V and HLSL re-emission are available for cross-compilation
/// and debugging workflows.
enum class SlangOutputFormat : uint8_t {
    MSL,       ///< Metal Shading Language source text
    SPIRV,     ///< SPIR-V binary blob
    HLSL,      ///< Re-emit HLSL (for debugging/validation)
};

/// Compilation descriptor for a single shader.
struct SlangCompileDesc {
    const char* source = nullptr;          ///< HLSL source code (null-terminated)
    const char* entryPoint = "main";       ///< Entry point function name
    const char* sourceName = nullptr;      ///< Logical name for error messages
    SlangShaderStage stage = SlangShaderStage::Vertex;  ///< Shader stage
    SlangOutputFormat outputFormat = SlangOutputFormat::MSL;  ///< Target format
    std::string defines;                   ///< Semicolon-separated defines ("A=1;B"), empty for none
};

/// Result of a Slang compilation.
struct SlangCompileResult {
    bool success = false;
    std::string output;            ///< Text output (MSL/HLSL source)
    std::vector<uint8_t> bytecode; ///< Binary output (SPIR-V)
    std::string errors;            ///< Compiler diagnostics (errors and warnings)
};

/// Runtime-loaded Slang compiler instance.
///
/// Owns the dynamic library handle and all Slang session objects. Call `Shutdown()`
/// explicitly or let the destructor handle cleanup.
///
/// # Thread Safety
/// A single `SlangCompiler` instance must NOT be used from multiple threads
/// concurrently. Create one instance per thread if parallel compilation is needed.
class SlangCompiler {
public:
    SlangCompiler() = default;
    ~SlangCompiler();

    /// Non-copyable — owns native library handle and session.
    SlangCompiler(const SlangCompiler&) = delete;
    SlangCompiler& operator=(const SlangCompiler&) = delete;

    /// Loads the Slang shared library and creates a global session.
    ///
    /// Searches for `libslang.dylib` (macOS), `slang.dll` (Windows), or
    /// `libslang.so` (Linux) in the engine's thirdparty directory first,
    /// then falls back to system library paths.
    ///
    /// # Returns
    /// `true` if the library was loaded and the session created successfully.
    /// `false` if the library is not found or session creation fails.
    bool Init();

    /// Returns true if `Init()` succeeded and the global session is active.
    [[nodiscard]] bool IsInitialized() const;

    /// Compiles HLSL source to the target format specified in `desc`.
    ///
    /// # Errors
    /// Returns an error result (without crashing) if:
    /// - The compiler is not initialized
    /// - `desc.source` is null
    /// - The HLSL source has syntax or semantic errors
    [[nodiscard]] SlangCompileResult Compile(const SlangCompileDesc& desc);

    /// Releases the Slang session and unloads the shared library.
    ///
    /// Safe to call multiple times or without prior `Init()`.
    void Shutdown();

    /// Returns the Slang compiler version string (e.g., "2026.16.1").
    ///
    /// # Returns
    /// Version string, or empty string if not initialized.
    [[nodiscard]] std::string GetVersion() const;

private:
    /// Loads the Slang shared library and resolves the global-session entry point.
    bool LoadSlangLibrary();

    /// Unloads the Slang shared library.
    void UnloadLibrary();

    void* m_libraryHandle = nullptr;  ///< dlopen/LoadLibrary handle for libslang
    void* m_globalSession = nullptr;  ///< slang::IGlobalSession* (opaque — no slang.h in this header)

    /// slang_createGlobalSession entry point (only symbol resolved at runtime;
    /// everything else goes through the interface vtables).
    using FnCreateGlobalSession = int (*)(int apiVersion, void** outGlobalSession);
    FnCreateGlobalSession m_fnCreateGlobalSession = nullptr;
};

} // namespace uz
