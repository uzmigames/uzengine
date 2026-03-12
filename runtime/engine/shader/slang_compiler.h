/// Shader-Slang compiler for HLSL -> MSL (Metal) and HLSL -> SPIR-V compilation.
///
/// Wraps the Slang compilation API to provide direct HLSL -> MSL output for the
/// Metal backend. Can also produce SPIR-V, potentially replacing DXC for Vulkan
/// in the future.
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

/// Input descriptor for a Slang compilation request.
///
/// All string fields must remain valid for the duration of the `Compile()` call.
/// The `source` field is the raw HLSL text, not a file path.
///
/// # Defaults
/// - `entryPoint`: "main"
/// - `sourceName`: "shader" (used in diagnostic messages)
/// - `stage`: Vertex
/// - `outputFormat`: MSL
struct SlangCompileDesc {
    const char* source = nullptr;       ///< HLSL source code (null-terminated)
    const char* entryPoint = "main";    ///< Entry point function name
    const char* sourceName = "shader";  ///< Source filename for error messages
    SlangShaderStage stage = SlangShaderStage::Vertex;
    SlangOutputFormat outputFormat = SlangOutputFormat::MSL;
};

/// Result of a Slang compilation.
///
/// On success for text targets (MSL, HLSL), the compiled code is in `output`.
/// On success for binary targets (SPIR-V), the blob is in `bytecode` and
/// `output` is empty.
///
/// On failure, `errors` contains diagnostic messages and both `output` and
/// `bytecode` are empty.
struct SlangCompileResult {
    bool success = false;               ///< True if compilation succeeded
    std::string output;                 ///< Compiled text output (MSL or HLSL source)
    std::vector<uint8_t> bytecode;      ///< Binary output (for SPIR-V)
    std::string errors;                 ///< Diagnostic messages (may be non-empty even on success)
};

/// Shader-Slang compiler wrapper.
///
/// Manages the Slang shared library handle and session lifecycle. The library is
/// loaded in `Init()` and unloaded in `Shutdown()` (or the destructor).
///
/// # Lifetime
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

    /// Loads the Slang shared library and creates a compilation session.
    ///
    /// Searches for `libslang.dylib` (macOS), `slang.dll` (Windows), or
    /// `libslang.so` (Linux) in the engine's thirdparty directory first,
    /// then falls back to system library paths.
    ///
    /// # Returns
    /// `true` if the library was loaded and the session created successfully.
    /// `false` if the library is not found or session creation fails.
    bool Init();

    /// Returns true if `Init()` succeeded and the session is active.
    [[nodiscard]] bool IsInitialized() const;

    /// Compiles HLSL source to the target format specified in `desc`.
    ///
    /// # Arguments
    /// * `desc` — Compilation descriptor with source, entry point, stage, and target.
    ///
    /// # Returns
    /// `SlangCompileResult` with `success=true` and populated output on success,
    /// or `success=false` with diagnostics in `errors`.
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

    /// Returns the Slang compiler version string (e.g., "2024.1.29").
    ///
    /// # Returns
    /// Version string, or empty string if not initialized.
    [[nodiscard]] std::string GetVersion() const;

private:
    /// Loads the Slang shared library and resolves function pointers.
    bool LoadLibrary();

    /// Unloads the Slang shared library.
    void UnloadLibrary();

    void* m_libraryHandle = nullptr;  ///< dlopen/LoadLibrary handle for libslang
    void* m_session = nullptr;        ///< SlangSession* (opaque to avoid Slang header dependency)

    // ─────────────────────────────────────────────────────────────────────────
    // Slang API function pointers (resolved at runtime via dlsym/GetProcAddress)
    // ─────────────────────────────────────────────────────────────────────────

    using FnCreateSession       = void* (*)(int targetApiVersion);
    using FnDestroySession      = void  (*)(void* session);
    using FnCreateCompileRequest = void* (*)(void* session);
    using FnDestroyCompileRequest = void (*)(void* request);
    using FnSetCodeGenTarget    = void  (*)(void* request, int target);
    using FnAddTranslationUnit  = int   (*)(void* request, int language, const char* name);
    using FnAddSourceString     = void  (*)(void* request, int unitIndex, const char* path, const char* source);
    using FnAddEntryPoint       = int   (*)(void* request, int unitIndex, const char* name, int stage);
    using FnCompile             = int   (*)(void* request);
    using FnGetDiagnosticOutput = const char* (*)(void* request);
    using FnGetEntryPointSource = const char* (*)(void* request, int entryIndex, int targetIndex);
    using FnGetEntryPointCode   = const void* (*)(void* request, int entryIndex, int targetIndex, size_t* outSize);

    FnCreateSession        m_fnCreateSession = nullptr;
    FnDestroySession       m_fnDestroySession = nullptr;
    FnCreateCompileRequest m_fnCreateCompileRequest = nullptr;
    FnDestroyCompileRequest m_fnDestroyCompileRequest = nullptr;
    FnSetCodeGenTarget     m_fnSetCodeGenTarget = nullptr;
    FnAddTranslationUnit   m_fnAddTranslationUnit = nullptr;
    FnAddSourceString      m_fnAddSourceString = nullptr;
    FnAddEntryPoint        m_fnAddEntryPoint = nullptr;
    FnCompile              m_fnCompile = nullptr;
    FnGetDiagnosticOutput  m_fnGetDiagnosticOutput = nullptr;
    FnGetEntryPointSource  m_fnGetEntryPointSource = nullptr;
    FnGetEntryPointCode    m_fnGetEntryPointCode = nullptr;
};

} // namespace uz
