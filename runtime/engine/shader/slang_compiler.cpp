/// SlangCompiler implementation — dynamically loads libslang and compiles
/// HLSL sources to MSL (Metal), SPIR-V, or re-emitted HLSL through the
/// modern Slang interface API (IGlobalSession/ISession/IModule).

#include "slang_compiler.h"

#include "log.h"
#include "paths.h"

#include <slang.h>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

UZ_DECLARE_LOG_CATEGORY(Shader);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Platform-Specific Library Loading
// ─────────────────────────────────────────────────────────────────────────────

#if defined(_WIN32)
    static constexpr const char* kSlangLibraryName = "slang.dll";
#elif defined(__APPLE__)
    static constexpr const char* kSlangLibraryName = "libslang.dylib";
#else
    static constexpr const char* kSlangLibraryName = "libslang.so";
#endif

static void* PlatformLoadLibrary(const char* path) {
#if defined(_WIN32)
    return static_cast<void*>(::LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* PlatformGetSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        ::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

static void PlatformFreeLibrary(void* handle) {
    if (!handle) return;
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

static std::string PlatformGetLoadError() {
#if defined(_WIN32)
    DWORD err = ::GetLastError();
    return "error " + std::to_string(err);
#else
    const char* err = dlerror();
    return err ? std::string(err) : std::string("unknown error");
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Enum mapping
// ─────────────────────────────────────────────────────────────────────────────

static SlangStage MapShaderStage(SlangShaderStage stage) {
    switch (stage) {
        case SlangShaderStage::Vertex:  return SLANG_STAGE_VERTEX;
        case SlangShaderStage::Pixel:   return SLANG_STAGE_FRAGMENT;
        case SlangShaderStage::Compute: return SLANG_STAGE_COMPUTE;
    }
    return SLANG_STAGE_VERTEX;
}

static SlangCompileTarget MapOutputTarget(SlangOutputFormat format) {
    switch (format) {
        case SlangOutputFormat::MSL:   return SLANG_METAL;
        case SlangOutputFormat::SPIRV: return SLANG_SPIRV;
        case SlangOutputFormat::HLSL:  return SLANG_HLSL;
    }
    return SLANG_METAL;
}

static const char* OutputFormatName(SlangOutputFormat format) {
    switch (format) {
        case SlangOutputFormat::MSL:   return "MSL";
        case SlangOutputFormat::SPIRV: return "SPIR-V";
        case SlangOutputFormat::HLSL:  return "HLSL";
    }
    return "unknown";
}

/// Captures a Slang diagnostic blob's text into a std::string.
static std::string BlobToString(slang::IBlob* blob) {
    if (!blob) return {};
    const auto* data = static_cast<const char*>(blob->getBufferPointer());
    const size_t size = blob->getBufferSize();
    if (!data || size == 0) return {};
    std::string text(data, size);
    while (!text.empty() && (text.back() == '\0' || text.back() == '\n')) {
        text.pop_back();
    }
    return text;
}

// ─────────────────────────────────────────────────────────────────────────────
// Library loading
// ─────────────────────────────────────────────────────────────────────────────

bool SlangCompiler::LoadSlangLibrary() {
    if (m_libraryHandle) return true;

    // Try engine thirdparty directory first, then system paths
    auto thirdpartyPath = ResolveEngineResource(
        std::string("thirdparty/slang/bin/") + kSlangLibraryName);

    m_libraryHandle = PlatformLoadLibrary(thirdpartyPath.string().c_str());

    if (!m_libraryHandle) {
        UZ_LOG(Shader, Warning,
               "SlangCompiler: thirdparty not found, trying system path '{}': {}",
               kSlangLibraryName, PlatformGetLoadError());
        m_libraryHandle = PlatformLoadLibrary(kSlangLibraryName);
    }

    if (!m_libraryHandle) {
        UZ_LOG(Shader, Error,
               "SlangCompiler: failed to load {}. "
               "Install Slang to thirdparty/slang/ or add it to the system library path.",
               kSlangLibraryName);
        return false;
    }

    m_fnCreateGlobalSession = reinterpret_cast<FnCreateGlobalSession>(
        PlatformGetSymbol(m_libraryHandle, "slang_createGlobalSession"));
    if (!m_fnCreateGlobalSession) {
        UZ_LOG(Shader, Error,
               "SlangCompiler: slang_createGlobalSession not found in {}",
               kSlangLibraryName);
        UnloadLibrary();
        return false;
    }

    void* gs = nullptr;
    const int res = m_fnCreateGlobalSession(SLANG_API_VERSION, &gs);
    if (res != 0 || !gs) {
        UZ_LOG(Shader, Error,
               "SlangCompiler: slang_createGlobalSession failed (result={})", res);
        UnloadLibrary();
        return false;
    }
    m_globalSession = gs;

    UZ_LOG(Shader, Info, "SlangCompiler: loaded {} (version {})",
           kSlangLibraryName, GetVersion());
    return true;
}

void SlangCompiler::UnloadLibrary() {
    if (m_globalSession) {
        static_cast<slang::IGlobalSession*>(m_globalSession)->release();
        m_globalSession = nullptr;
    }
    // Intentionally never dlclose the library: libslang does not survive an
    // unload+reload cycle in the same process (crashes on the next session).
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

SlangCompiler::~SlangCompiler() {
    Shutdown();
}

bool SlangCompiler::Init() {
    if (m_globalSession) return true;
    if (m_libraryHandle) {
        // Library still loaded from a prior Shutdown — just recreate the session.
        void* gs = nullptr;
        const int res = m_fnCreateGlobalSession(SLANG_API_VERSION, &gs);
        if (res != 0 || !gs) return false;
        m_globalSession = gs;
        return true;
    }
    return LoadSlangLibrary();
}

bool SlangCompiler::IsInitialized() const {
    return m_globalSession != nullptr;
}

void SlangCompiler::Shutdown() {
    UnloadLibrary();
}

// ─────────────────────────────────────────────────────────────────────────────
// Compilation
// ─────────────────────────────────────────────────────────────────────────────

SlangCompileResult SlangCompiler::Compile(const SlangCompileDesc& desc) {
    SlangCompileResult result;

    if (!m_globalSession) {
        result.errors = "SlangCompiler not initialized — install libslang to "
                        "thirdparty/slang/ and call Init() before Compile()";
        return result;
    }

    if (!desc.source) {
        result.errors = "SlangCompiler: null source";
        return result;
    }

    auto* globalSession = static_cast<slang::IGlobalSession*>(m_globalSession);

    // Per-compilation session with the requested code generation target
    slang::TargetDesc targetDesc{};
    targetDesc.format = MapOutputTarget(desc.outputFormat);

    slang::SessionDesc sessionDesc{};
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    slang::ISession* session = nullptr;
    {
        const SlangResult res = globalSession->createSession(sessionDesc, &session);
        if (SLANG_FAILED(res) || !session) {
            result.errors = "SlangCompiler: createSession failed (result " +
                            std::to_string(res) + ")";
            return result;
        }
    }

    const char* entryPoint = desc.entryPoint ? desc.entryPoint : "main";
    const char* sourceName = desc.sourceName ? desc.sourceName : "shader";

    // The .hlsl extension makes Slang parse the source as HLSL
    const std::string modulePath = std::string(sourceName) + ".hlsl";

    slang::IBlob* moduleDiags = nullptr;
    slang::IModule* module = session->loadModuleFromSourceString(
        sourceName, modulePath.c_str(), desc.source, &moduleDiags);
    const std::string moduleDiagText = BlobToString(moduleDiags);
    if (moduleDiags) moduleDiags->release();

    if (!module) {
        result.errors = moduleDiagText.empty()
            ? "SlangCompiler: loadModuleFromSourceString failed"
            : moduleDiagText;
        session->release();
        return result;
    }
    module->addRef();

    slang::IBlob* epDiags = nullptr;
    slang::IEntryPoint* entry = nullptr;
    {
        const SlangResult res = module->findAndCheckEntryPoint(
            entryPoint, MapShaderStage(desc.stage), &entry, &epDiags);
        const std::string epDiagText = BlobToString(epDiags);
        if (epDiags) epDiags->release();
        if (SLANG_FAILED(res) || !entry) {
            result.errors = epDiagText.empty()
                ? std::string("SlangCompiler: entry point '") + entryPoint + "' not found"
                : epDiagText;
            module->release();
            session->release();
            return result;
        }
    }

    // Compose module + entry point into a linked program
    slang::IComponentType* components[] = { module, entry };
    slang::IComponentType* program = nullptr;
    slang::IBlob* linkDiags = nullptr;
    {
        const SlangResult res = session->createCompositeComponentType(
            components, 2, &program, &linkDiags);
        const std::string linkDiagText = BlobToString(linkDiags);
        if (linkDiags) linkDiags->release();
        if (SLANG_FAILED(res) || !program) {
            result.errors = linkDiagText.empty()
                ? "SlangCompiler: createCompositeComponentType failed"
                : linkDiagText;
            entry->release();
            module->release();
            session->release();
            return result;
        }
    }

    // Emit the whole linked program for the target. getTargetCode is used
    // instead of getEntryPointCode — the latter crashes in Slang 2026.16.1's
    // Metal source-from-IR emission; target-level emission includes the
    // requested entry point (the composite links only module + entry point).
    slang::IBlob* codeBlob = nullptr;
    slang::IBlob* codeDiags = nullptr;
    const SlangResult codeRes = program->getTargetCode(0, &codeBlob, &codeDiags);

    // Collect diagnostics from all stages (Slang reports warnings the same way)
    if (!moduleDiagText.empty()) result.errors += moduleDiagText;
    if (const std::string t = BlobToString(codeDiags); !t.empty()) {
        if (!result.errors.empty()) result.errors += '\n';
        result.errors += t;
    }
    if (codeDiags) codeDiags->release();

    if (SLANG_FAILED(codeRes) || !codeBlob) {
        if (result.errors.empty()) {
            result.errors = "SlangCompiler: getTargetCode failed (result " +
                            std::to_string(codeRes) + ")";
        }
        UZ_LOG(Shader, Error,
               "SlangCompiler: compilation failed for '{}': {}",
               sourceName, result.errors);
        program->release();
        entry->release();
        module->release();
        session->release();
        return result;
    }

    const auto* data = static_cast<const uint8_t*>(codeBlob->getBufferPointer());
    const size_t size = codeBlob->getBufferSize();
    if (!data || size == 0) {
        result.errors = "SlangCompiler: compilation succeeded but produced empty output";
        codeBlob->release();
        program->release();
        entry->release();
        module->release();
        session->release();
        return result;
    }

    if (desc.outputFormat == SlangOutputFormat::SPIRV) {
        result.bytecode.assign(data, data + size);
    } else {
        // Text targets (MSL/HLSL): trailing NUL may be included in the blob
        size_t textLen = size;
        if (textLen > 0 && data[textLen - 1] == 0) --textLen;
        result.output.assign(reinterpret_cast<const char*>(data), textLen);
    }
    result.success = true;

    UZ_LOG(Shader, Info, "SlangCompiler: compiled '{}' -> {} ({} bytes)",
           sourceName, OutputFormatName(desc.outputFormat), size);

    codeBlob->release();
    program->release();
    entry->release();
    module->release();
    session->release();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Version
// ─────────────────────────────────────────────────────────────────────────────

std::string SlangCompiler::GetVersion() const {
    if (!m_globalSession) return {};

    // The build tag identifies the Slang runtime build (e.g., "2026.16.1")
    const char* tag = static_cast<slang::IGlobalSession*>(m_globalSession)->getBuildTagString();
    return tag ? std::string(tag) : std::string();
}

} // namespace uz
