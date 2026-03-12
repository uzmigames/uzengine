/// SlangCompiler Implementation
///
/// Dynamically loads the Slang shared library via dlopen (macOS/Linux) or
/// LoadLibrary (Windows). Creates a SlangSession and compiles HLSL source
/// to MSL, SPIR-V, or re-emitted HLSL.
///
/// # Slang API Overview
/// The Slang C API follows a session-based model:
/// 1. `spCreateSession()` — creates a global session
/// 2. `spCreateCompileRequest()` — creates a per-compilation request
/// 3. Configure the request: target, source, entry point
/// 4. `spCompile()` — runs compilation, returns status code
/// 5. Extract results: `spGetEntryPointSource()` for text, `spGetEntryPointCode()` for binary
/// 6. `spDestroyCompileRequest()` — cleanup per-request
/// 7. `spDestroySession()` — cleanup session
///
/// # Library Not Found
/// If the Slang library is not installed, `Init()` logs a warning and returns
/// false. All subsequent `Compile()` calls return an error result directing the
/// user to install Slang. This matches the DXC compiler's graceful fallback.

#include "slang_compiler.h"
#include "log.h"
#include "paths.h"
#include "profiler.h"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#else
    #include <dlfcn.h>
#endif

UZ_DECLARE_LOG_CATEGORY(Shader);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Slang API Constants
//
// These mirror the Slang C API enums so we don't depend on slang.h at compile
// time. Values are from the Slang public API specification (2024.x).
// ─────────────────────────────────────────────────────────────────────────────

/// Slang API version passed to `spCreateSession`.
/// 0 requests the latest available version from the loaded library.
static constexpr int kSlangApiVersion = 0;

/// Slang source language enum: SLANG_SOURCE_LANGUAGE_HLSL = 5.
static constexpr int kSlangSourceLanguageHLSL = 5;

/// Slang code generation targets.
static constexpr int kSlangTargetMetal = 15;   ///< SLANG_METAL
static constexpr int kSlangTargetSPIRV = 9;    ///< SLANG_SPIRV
static constexpr int kSlangTargetHLSL  = 2;    ///< SLANG_HLSL

/// Slang shader stage constants.
static constexpr int kSlangStageVertex   = 1;  ///< SLANG_STAGE_VERTEX
static constexpr int kSlangStageFragment = 2;  ///< SLANG_STAGE_FRAGMENT
static constexpr int kSlangStageCompute  = 5;  ///< SLANG_STAGE_COMPUTE

/// Slang compilation result: 0 means success.
static constexpr int kSlangOK = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Platform-Specific Library Loading
// ─────────────────────────────────────────────────────────────────────────────

/// Platform-specific shared library filename for Slang.
#if defined(_WIN32)
    static constexpr const char* kSlangLibraryName = "slang.dll";
#elif defined(__APPLE__)
    static constexpr const char* kSlangLibraryName = "libslang.dylib";
#else
    static constexpr const char* kSlangLibraryName = "libslang.so";
#endif

/// Loads a shared library by path. Returns the native handle or nullptr.
static void* PlatformLoadLibrary(const char* path) {
#if defined(_WIN32)
    return static_cast<void*>(::LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

/// Resolves a symbol from a loaded shared library. Returns nullptr on failure.
static void* PlatformGetSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        ::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

/// Unloads a shared library.
static void PlatformFreeLibrary(void* handle) {
    if (!handle) return;
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

/// Returns a human-readable error string for the last library load failure.
static std::string PlatformGetLoadError() {
#if defined(_WIN32)
    DWORD err = ::GetLastError();
    return "Win32 error=" + std::to_string(err);
#else
    const char* msg = dlerror();
    return msg ? std::string(msg) : "unknown error";
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage / Target Mapping Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Maps UzEngine shader stage enum to Slang's SLANG_STAGE_* constant.
///
/// # Arguments
/// * `stage` — Engine-side shader stage.
///
/// # Returns
/// Slang stage integer constant.
static int MapShaderStage(SlangShaderStage stage) {
    switch (stage) {
        case SlangShaderStage::Vertex:  return kSlangStageVertex;
        case SlangShaderStage::Pixel:   return kSlangStageFragment;
        case SlangShaderStage::Compute: return kSlangStageCompute;
    }
    return kSlangStageVertex;
}

/// Maps UzEngine output format enum to Slang's code generation target constant.
///
/// # Arguments
/// * `format` — Desired output format.
///
/// # Returns
/// Slang target integer constant.
static int MapOutputTarget(SlangOutputFormat format) {
    switch (format) {
        case SlangOutputFormat::MSL:   return kSlangTargetMetal;
        case SlangOutputFormat::SPIRV: return kSlangTargetSPIRV;
        case SlangOutputFormat::HLSL:  return kSlangTargetHLSL;
    }
    return kSlangTargetMetal;
}

/// Returns a human-readable name for the output format (for log messages).
static const char* OutputFormatName(SlangOutputFormat format) {
    switch (format) {
        case SlangOutputFormat::MSL:   return "MSL";
        case SlangOutputFormat::SPIRV: return "SPIR-V";
        case SlangOutputFormat::HLSL:  return "HLSL";
    }
    return "unknown";
}

/// Returns a human-readable name for the shader stage (for log messages).
static const char* ShaderStageName(SlangShaderStage stage) {
    switch (stage) {
        case SlangShaderStage::Vertex:  return "vertex";
        case SlangShaderStage::Pixel:   return "pixel";
        case SlangShaderStage::Compute: return "compute";
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Library Loading
// ─────────────────────────────────────────────────────────────────────────────

/// Resolves a single Slang API function pointer by name.
/// Returns false and logs an error if the symbol is not found.
#define RESOLVE_SLANG_FN(fnPtr, fnName)                                        \
    do {                                                                        \
        (fnPtr) = reinterpret_cast<decltype(fnPtr)>(                           \
            PlatformGetSymbol(m_libraryHandle, (fnName)));                     \
        if (!(fnPtr)) {                                                        \
            UZ_LOG(Shader, Error,                                              \
                   "SlangCompiler: symbol '{}' not found in Slang library",    \
                   (fnName));                                                   \
            UnloadLibrary();                                                    \
            return false;                                                       \
        }                                                                       \
    } while (0)

bool SlangCompiler::LoadLibrary() {
    // Try engine thirdparty directory first, then system paths
    auto thirdpartyPath = ResolveEngineResource(
        std::string("thirdparty/slang/bin/") + kSlangLibraryName);
    std::string resolvedPath = thirdpartyPath.string();

    UZ_LOG(Shader, Info, "SlangCompiler: trying '{}'", resolvedPath);
    m_libraryHandle = PlatformLoadLibrary(resolvedPath.c_str());

    if (!m_libraryHandle) {
        // Fall back to system search paths
        UZ_LOG(Shader, Info,
               "SlangCompiler: thirdparty not found, trying system path '{}'",
               kSlangLibraryName);
        m_libraryHandle = PlatformLoadLibrary(kSlangLibraryName);
    }

    if (!m_libraryHandle) {
        UZ_LOG(Shader, Warning,
               "SlangCompiler: failed to load Slang library — {} "
               "Install Slang to thirdparty/slang/ or add it to the system library path.",
               PlatformGetLoadError());
        return false;
    }

    // Resolve all Slang C API function pointers
    RESOLVE_SLANG_FN(m_fnCreateSession,        "spCreateSession");
    RESOLVE_SLANG_FN(m_fnDestroySession,       "spDestroySession");
    RESOLVE_SLANG_FN(m_fnCreateCompileRequest, "spCreateCompileRequest");
    RESOLVE_SLANG_FN(m_fnDestroyCompileRequest,"spDestroyCompileRequest");
    RESOLVE_SLANG_FN(m_fnSetCodeGenTarget,     "spSetCodeGenTarget");
    RESOLVE_SLANG_FN(m_fnAddTranslationUnit,   "spAddTranslationUnit");
    RESOLVE_SLANG_FN(m_fnAddSourceString,      "spAddTranslationUnitSourceString");
    RESOLVE_SLANG_FN(m_fnAddEntryPoint,        "spAddEntryPoint");
    RESOLVE_SLANG_FN(m_fnCompile,              "spCompile");
    RESOLVE_SLANG_FN(m_fnGetDiagnosticOutput,  "spGetDiagnosticOutput");
    RESOLVE_SLANG_FN(m_fnGetEntryPointSource,  "spGetEntryPointSource");
    RESOLVE_SLANG_FN(m_fnGetEntryPointCode,    "spGetEntryPointCode");

    UZ_LOG(Shader, Info, "SlangCompiler: Slang library loaded successfully");
    return true;
}

#undef RESOLVE_SLANG_FN

void SlangCompiler::UnloadLibrary() {
    // Clear all function pointers before unloading
    m_fnCreateSession = nullptr;
    m_fnDestroySession = nullptr;
    m_fnCreateCompileRequest = nullptr;
    m_fnDestroyCompileRequest = nullptr;
    m_fnSetCodeGenTarget = nullptr;
    m_fnAddTranslationUnit = nullptr;
    m_fnAddSourceString = nullptr;
    m_fnAddEntryPoint = nullptr;
    m_fnCompile = nullptr;
    m_fnGetDiagnosticOutput = nullptr;
    m_fnGetEntryPointSource = nullptr;
    m_fnGetEntryPointCode = nullptr;

    PlatformFreeLibrary(m_libraryHandle);
    m_libraryHandle = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown / Destructor
// ─────────────────────────────────────────────────────────────────────────────

bool SlangCompiler::Init() {
    UZ_PROFILE_FUNCTION();

    if (m_session) return true;  // Already initialized

    if (!LoadLibrary()) {
        return false;
    }

    // Create the Slang global session
    m_session = m_fnCreateSession(kSlangApiVersion);
    if (!m_session) {
        UZ_LOG(Shader, Error,
               "SlangCompiler: spCreateSession() returned null — "
               "Slang library may be incompatible");
        UnloadLibrary();
        return false;
    }

    UZ_LOG(Shader, Info, "SlangCompiler: session created, version: {}",
           GetVersion());
    return true;
}

bool SlangCompiler::IsInitialized() const {
    return m_session != nullptr;
}

void SlangCompiler::Shutdown() {
    UZ_PROFILE_FUNCTION();

    if (m_session && m_fnDestroySession) {
        m_fnDestroySession(m_session);
        m_session = nullptr;
    }

    UnloadLibrary();
}

SlangCompiler::~SlangCompiler() {
    Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile
// ─────────────────────────────────────────────────────────────────────────────

SlangCompileResult SlangCompiler::Compile(const SlangCompileDesc& desc) {
    UZ_PROFILE_FUNCTION();
    SlangCompileResult result;

    // Validate preconditions
    if (!m_session) {
        result.errors =
            "SlangCompiler not initialized — install libslang to "
            "thirdparty/slang/ and call Init() before Compile()";
        return result;
    }

    if (!desc.source) {
        result.errors = "SlangCompiler: source is null";
        return result;
    }

    UZ_LOG(Shader, Info,
           "SlangCompiler: compiling '{}' entry='{}' stage={} target={}",
           desc.sourceName ? desc.sourceName : "<unnamed>",
           desc.entryPoint ? desc.entryPoint : "main",
           ShaderStageName(desc.stage),
           OutputFormatName(desc.outputFormat));

    // Create a fresh compile request for this compilation unit
    void* request = m_fnCreateCompileRequest(m_session);
    if (!request) {
        result.errors = "SlangCompiler: spCreateCompileRequest() returned null";
        return result;
    }

    // Set the code generation target (MSL, SPIR-V, or HLSL)
    m_fnSetCodeGenTarget(request, MapOutputTarget(desc.outputFormat));

    // Add a translation unit with the HLSL source
    int unitIndex = m_fnAddTranslationUnit(
        request,
        kSlangSourceLanguageHLSL,
        desc.sourceName ? desc.sourceName : "shader");

    // Provide the HLSL source text
    m_fnAddSourceString(
        request,
        unitIndex,
        desc.sourceName ? desc.sourceName : "shader",
        desc.source);

    // Define the entry point and shader stage
    int entryPointIndex = m_fnAddEntryPoint(
        request,
        unitIndex,
        desc.entryPoint ? desc.entryPoint : "main",
        MapShaderStage(desc.stage));

    // Run compilation
    int compileStatus = m_fnCompile(request);

    // Extract diagnostics (errors/warnings) regardless of success
    const char* diagnostics = m_fnGetDiagnosticOutput(request);
    if (diagnostics && diagnostics[0] != '\0') {
        result.errors = diagnostics;
    }

    if (compileStatus != kSlangOK) {
        UZ_LOG(Shader, Error,
               "SlangCompiler: compilation failed for '{}': {}",
               desc.sourceName ? desc.sourceName : "<unnamed>",
               result.errors.empty() ? "unknown error" : result.errors);
        m_fnDestroyCompileRequest(request);
        return result;
    }

    // Extract compiled output based on target format
    if (desc.outputFormat == SlangOutputFormat::SPIRV) {
        // SPIR-V: binary blob output
        size_t codeSize = 0;
        const void* codePtr = m_fnGetEntryPointCode(
            request, entryPointIndex, 0 /* targetIndex */, &codeSize);

        if (codePtr && codeSize > 0) {
            auto* bytes = static_cast<const uint8_t*>(codePtr);
            result.bytecode.assign(bytes, bytes + codeSize);
            result.success = true;
        } else {
            result.errors = "SlangCompiler: compilation succeeded but "
                            "spGetEntryPointCode returned empty output";
        }
    } else {
        // MSL / HLSL: text source output
        const char* sourceOutput = m_fnGetEntryPointSource(
            request, entryPointIndex, 0 /* targetIndex */);

        if (sourceOutput && sourceOutput[0] != '\0') {
            result.output = sourceOutput;
            result.success = true;
        } else {
            result.errors = "SlangCompiler: compilation succeeded but "
                            "spGetEntryPointSource returned empty output";
        }
    }

    if (result.success) {
        UZ_LOG(Shader, Info,
               "SlangCompiler: compiled '{}' -> {} ({} bytes)",
               desc.sourceName ? desc.sourceName : "<unnamed>",
               OutputFormatName(desc.outputFormat),
               desc.outputFormat == SlangOutputFormat::SPIRV
                   ? result.bytecode.size()
                   : result.output.size());
    }

    m_fnDestroyCompileRequest(request);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Version
// ─────────────────────────────────────────────────────────────────────────────

std::string SlangCompiler::GetVersion() const {
    if (!m_session) return {};

    // Slang embeds version info in the session. The C API exposes
    // `spGetBuildTagString()` which returns a compile-time version tag.
    // We resolve it dynamically since it may not exist in older builds.
    if (!m_libraryHandle) return "unknown";

    auto fnGetBuildTag = reinterpret_cast<const char* (*)()>(
        PlatformGetSymbol(m_libraryHandle, "spGetBuildTagString"));

    if (fnGetBuildTag) {
        const char* tag = fnGetBuildTag();
        if (tag) return std::string(tag);
    }

    return "unknown";
}

} // namespace uz
