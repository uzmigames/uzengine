/// ShaderCompilerFXC Implementation
///
/// Dynamically loads d3dcompiler_47.dll via LoadLibrary/GetProcAddress.
/// Uses the D3DCompile() function to compile HLSL to DXBC for SM 5.0.
///
/// FXC types (D3D_SHADER_MACRO, ID3DBlob, pD3DCompile) are defined inline
/// to avoid header conflicts between zig MinGW and directx_headers thirdparty.

#ifdef _WIN32

#include "shader_compiler_fxc.h"
#include "log.h"
#include "paths.h"
#include "profiler.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <unknwn.h>

// ─────────────────────────────────────────────────────────────────────────────
// FXC types — defined inline to avoid d3dcompiler.h / d3dcommon.h header
// conflicts between zig MinGW and our directx_headers thirdparty.
// ─────────────────────────────────────────────────────────────────────────────

/// Minimal ID3DBlob COM interface (same ABI as IFxcBlob).
/// Only the methods we need: GetBufferPointer, GetBufferSize, Release.
struct IFxcBlob : public IUnknown {
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};

/// Preprocessor macro definition for D3DCompile (matches D3D_SHADER_MACRO layout).
struct FxcShaderMacro {
    const char* Name;
    const char* Definition;
};

/// D3DCompile flags (from d3dcompiler.h)
static constexpr UINT kFxcEnableStrictness    = (1 << 11);  // D3DCOMPILE_ENABLE_STRICTNESS
static constexpr UINT kFxcDebug               = (1 << 0);   // D3DCOMPILE_DEBUG
static constexpr UINT kFxcSkipOptimization    = (1 << 2);   // D3DCOMPILE_SKIP_OPTIMIZATION

/// D3DCompile function signature (from d3dcompiler.h).
using FxcD3DCompileFunc = HRESULT(WINAPI*)(
    const void* pSrcData,
    SIZE_T SrcDataSize,
    const char* pSourceName,
    const FxcShaderMacro* pDefines,
    void* pInclude,      // ID3DInclude* — we pass nullptr
    const char* pEntrypoint,
    const char* pTarget,
    UINT Flags1,
    UINT Flags2,
    IFxcBlob** ppCode,
    IFxcBlob** ppErrorMsgs);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool ShaderCompilerFXC::Init(const std::string& fxcPath) {
    UZ_PROFILE_FUNCTION();
    if (m_d3dCompile) return true;

    auto resolved = ResolveEngineResource(fxcPath);
    std::wstring widePath = resolved.wstring();

    UZ_LOG(Renderer, Info, "ShaderCompilerFXC: loading '{}'", resolved.string());

    HMODULE hModule = LoadLibraryW(widePath.c_str());
    if (!hModule) {
        DWORD err = GetLastError();
        UZ_LOG(Renderer, Error, "ShaderCompilerFXC: failed to load '{}' (Win32 error={})",
               resolved.string(), err);
        return false;
    }
    m_fxcModule = hModule;

    m_d3dCompile = reinterpret_cast<void*>(
        GetProcAddress(hModule, "D3DCompile"));
    if (!m_d3dCompile) {
        UZ_LOG(Renderer, Error, "ShaderCompilerFXC: D3DCompile not found in '{}'",
               resolved.string());
        Shutdown();
        return false;
    }

    UZ_LOG(Renderer, Info, "ShaderCompilerFXC: loaded '{}' successfully", resolved.string());
    return true;
}

ShaderCompilerFXC::~ShaderCompilerFXC() {
    Shutdown();
}

void ShaderCompilerFXC::Shutdown() {
    m_d3dCompile = nullptr;
    if (m_fxcModule) {
        FreeLibrary(static_cast<HMODULE>(m_fxcModule));
        m_fxcModule = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Target Profile Mapping
// ─────────────────────────────────────────────────────────────────────────────

std::string ShaderCompilerFXC::GetTargetProfile(ShaderStageCompile stage,
                                                 ShaderModelCompile model) {
    const char* prefix = "vs";
    switch (stage) {
        case ShaderStageCompile::Vertex:        prefix = "vs"; break;
        case ShaderStageCompile::Pixel:         prefix = "ps"; break;
        case ShaderStageCompile::Compute:       prefix = "cs"; break;
        case ShaderStageCompile::Geometry:      prefix = "gs"; break;
        case ShaderStageCompile::Hull:          prefix = "hs"; break;
        case ShaderStageCompile::Domain:        prefix = "ds"; break;
        case ShaderStageCompile::Amplification: prefix = "as"; break;
        case ShaderStageCompile::Mesh:          prefix = "ms"; break;
    }

    const char* version = "5_0";
    switch (model) {
        case ShaderModelCompile::SM5_0: version = "5_0"; break;
        case ShaderModelCompile::SM5_1: version = "5_1"; break;
        default:
            UZ_LOG(Renderer, Error, "ShaderCompilerFXC: SM 6.0+ passed to FXC — use DXC instead");
            version = "5_0";
            break;
    }

    return std::string(prefix) + "_" + version;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile
// ─────────────────────────────────────────────────────────────────────────────

ShaderCompileResult ShaderCompilerFXC::Compile(const ShaderCompileDesc& desc) {
    UZ_PROFILE_FUNCTION();
    ShaderCompileResult result;

    if (!m_d3dCompile) {
        result.errors = "ShaderCompilerFXC not initialized";
        return result;
    }

    // Parse "KEY=VALUE" strings into name/definition pairs.
    // Stored in a parallel vector to keep pointers stable for the macro array.
    struct MacroPair { std::string name; std::string value; };
    std::vector<MacroPair> macroPairs;
    macroPairs.reserve(desc.defines.size());

    for (const auto& def : desc.defines) {
        MacroPair pair;
        auto eqPos = def.find('=');
        if (eqPos != std::string::npos) {
            pair.name = def.substr(0, eqPos);
            pair.value = def.substr(eqPos + 1);
        } else {
            pair.name = def;
            pair.value = "1";
        }
        macroPairs.push_back(std::move(pair));
    }

    std::vector<FxcShaderMacro> macros;
    macros.reserve(macroPairs.size() + 1);
    for (const auto& pair : macroPairs) {
        macros.push_back({pair.name.c_str(), pair.value.c_str()});
    }
    macros.push_back({nullptr, nullptr}); // Null terminator required by D3DCompile

    auto profile = GetTargetProfile(desc.stage, desc.model);

    UINT flags1 = kFxcEnableStrictness;
    if (desc.generateDebugInfo) {
        flags1 |= kFxcDebug;
    }
    if (desc.disableOptimizations) {
        flags1 |= kFxcSkipOptimization;
    }

    auto d3dCompileFunc = reinterpret_cast<FxcD3DCompileFunc>(m_d3dCompile);

    IFxcBlob* pCode = nullptr;
    IFxcBlob* pErrors = nullptr;

    HRESULT hr = d3dCompileFunc(
        desc.source.c_str(),
        desc.source.size(),
        desc.sourceName.empty() ? nullptr : desc.sourceName.c_str(),
        macros.data(),
        nullptr, // No include handler — sources are pre-resolved by ShaderSourceManager
        desc.entryPoint.c_str(),
        profile.c_str(),
        flags1,
        0,       // flags2 (effect compilation flags, unused)
        &pCode,
        &pErrors);

    // Extract error messages
    if (pErrors && pErrors->GetBufferSize() > 0) {
        result.errors = std::string(
            static_cast<const char*>(pErrors->GetBufferPointer()),
            pErrors->GetBufferSize());
    }

    if (SUCCEEDED(hr) && pCode && pCode->GetBufferSize() > 0) {
        auto* data = static_cast<const uint8_t*>(pCode->GetBufferPointer());
        result.bytecode.assign(data, data + pCode->GetBufferSize());
        result.success = true;
    }

    if (pCode) pCode->Release();
    if (pErrors) pErrors->Release();

    return result;
}

} // namespace uz

#endif // _WIN32
