/// ShaderCompiler Implementation
///
/// Dynamically loads dxcompiler.dll via LoadLibrary/GetProcAddress.
/// Creates IDxcCompiler3 + IDxcUtils, compiles HLSL to DXIL or SPIRV.

#ifdef _WIN32

#include "shader_compiler.h"
#include "log.h"
#include "paths.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <unknwn.h>

// ─────────────────────────────────────────────────────────────────────────────
// DXC UUID extraction for zig-clang (no __declspec(uuid) support)
//
// Instead of suppressing CROSS_PLATFORM_UUIDOF and defining IIDs manually
// (which caused the IDxcUtils IID bug — CLSID was used instead of IID),
// we redefine the macro to both forward-declare the struct AND capture the
// UUID string. The IIDs are then parsed from the string at compile time
// via a helper that converts "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" to GUID.
//
// Source of truth: CROSS_PLATFORM_UUIDOF declarations in dxcapi.h
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a hex character to its 4-bit value at compile time.
static constexpr uint8_t HexVal(char c) {
    return (c >= '0' && c <= '9') ? (uint8_t)(c - '0') :
           (c >= 'a' && c <= 'f') ? (uint8_t)(c - 'a' + 10) :
           (c >= 'A' && c <= 'F') ? (uint8_t)(c - 'A' + 10) : 0;
}

/// Parses two hex characters into a byte at compile time.
static constexpr uint8_t HexByte(const char* s, int i) {
    return (HexVal(s[i]) << 4) | HexVal(s[i + 1]);
}

/// Parses a UUID string "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" into a GUID.
static constexpr GUID ParseUUID(const char* s) {
    return GUID{
        // Data1: XXXXXXXX (8 hex chars at offset 0)
        (uint32_t)(HexVal(s[0]) << 28 | HexVal(s[1]) << 24 |
                   HexVal(s[2]) << 20 | HexVal(s[3]) << 16 |
                   HexVal(s[4]) << 12 | HexVal(s[5]) << 8  |
                   HexVal(s[6]) << 4  | HexVal(s[7])),
        // Data2: XXXX (4 hex chars at offset 9)
        (uint16_t)(HexVal(s[9]) << 12 | HexVal(s[10]) << 8 |
                   HexVal(s[11]) << 4 | HexVal(s[12])),
        // Data3: XXXX (4 hex chars at offset 14)
        (uint16_t)(HexVal(s[14]) << 12 | HexVal(s[15]) << 8 |
                   HexVal(s[16]) << 4 | HexVal(s[17])),
        // Data4: XXXX-XXXXXXXXXXXX (2+6 bytes at offset 19 and 24)
        {HexByte(s, 19), HexByte(s, 21),
         HexByte(s, 24), HexByte(s, 26), HexByte(s, 28),
         HexByte(s, 30), HexByte(s, 32), HexByte(s, 34)}
    };
}

// Redefine CROSS_PLATFORM_UUIDOF to forward-declare and generate IID constant.
// This is the ONLY place we override the macro — IIDs come from dxcapi.h directly.
#undef CROSS_PLATFORM_UUIDOF
#define CROSS_PLATFORM_UUIDOF(iface, spec)                  \
    struct iface;                                            \
    static constexpr IID IID_##iface = ParseUUID(spec);

#include "dxcapi.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// String Conversion Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                   static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                         static_cast<int>(s.size()), result.data(), len);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool ShaderCompiler::Init(const std::string& dxcPath) {
    if (m_compiler) return true;

    // Resolve relative paths against the executable directory.
    // "dxcompiler.dll" → "{exe_dir}/dxcompiler.dll"
    auto resolved = ResolveEngineResource(dxcPath);
    std::wstring widePath = resolved.wstring();

    // Set DLL directory so dxcompiler.dll can find dxil.dll in the same folder
    {
        wchar_t fullPath[MAX_PATH];
        if (GetFullPathNameW(widePath.c_str(), MAX_PATH, fullPath, nullptr)) {
            std::wstring dir(fullPath);
            auto pos = dir.find_last_of(L"\\/");
            if (pos != std::wstring::npos) {
                dir.resize(pos);
                SetDllDirectoryW(dir.c_str());
            }
        }
    }

    UZ_LOG(Renderer, Info, "ShaderCompiler: loading '{}'", resolved.string());

    HMODULE hModule = LoadLibraryW(widePath.c_str());
    if (!hModule) {
        DWORD err = GetLastError();
        UZ_LOG(Renderer, Error, "ShaderCompiler: failed to load '{}' (Win32 error={})",
               resolved.string(), err);
        return false;
    }
    m_dxcModule = hModule;

    auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(
        GetProcAddress(hModule, "DxcCreateInstance"));
    if (!createInstance) {
        UZ_LOG(Renderer, Error, "ShaderCompiler: DxcCreateInstance not found in '{}'", resolved.string());
        Shutdown();
        return false;
    }

    HRESULT hr = createInstance(CLSID_DxcCompiler, IID_IDxcCompiler3,
                                 reinterpret_cast<LPVOID*>(&m_compiler));
    if (FAILED(hr)) {
        UZ_LOG(Renderer, Error, "ShaderCompiler: failed to create IDxcCompiler3 (hr=0x{:08x})",
               static_cast<unsigned>(hr));
        Shutdown();
        return false;
    }

    hr = createInstance(CLSID_DxcUtils, IID_IDxcUtils,
                         reinterpret_cast<LPVOID*>(&m_utils));
    if (FAILED(hr)) {
        UZ_LOG(Renderer, Error, "ShaderCompiler: failed to create IDxcUtils (hr=0x{:08x})",
               static_cast<unsigned>(hr));
        Shutdown();
        return false;
    }

    UZ_LOG(Renderer, Info, "ShaderCompiler: loaded '{}', version: {}", resolved.string(), GetVersion());
    return true;
}

ShaderCompiler::~ShaderCompiler() {
    Shutdown();
}

void ShaderCompiler::Shutdown() {
    if (m_compiler) { m_compiler->Release(); m_compiler = nullptr; }
    if (m_utils) { m_utils->Release(); m_utils = nullptr; }
    if (m_dxcModule) {
        FreeLibrary(static_cast<HMODULE>(m_dxcModule));
        m_dxcModule = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Target Profile Mapping
// ─────────────────────────────────────────────────────────────────────────────

std::wstring ShaderCompiler::GetTargetProfile(ShaderStageCompile stage,
                                               ShaderModelCompile model) {
    const wchar_t* prefix = L"vs";
    switch (stage) {
        case ShaderStageCompile::Vertex:        prefix = L"vs"; break;
        case ShaderStageCompile::Pixel:         prefix = L"ps"; break;
        case ShaderStageCompile::Compute:       prefix = L"cs"; break;
        case ShaderStageCompile::Geometry:      prefix = L"gs"; break;
        case ShaderStageCompile::Hull:          prefix = L"hs"; break;
        case ShaderStageCompile::Domain:        prefix = L"ds"; break;
        case ShaderStageCompile::Amplification: prefix = L"as"; break;
        case ShaderStageCompile::Mesh:          prefix = L"ms"; break;
    }

    const wchar_t* version = L"6_6";
    switch (model) {
        case ShaderModelCompile::SM5_1: version = L"5_1"; break;
        case ShaderModelCompile::SM6_0: version = L"6_0"; break;
        case ShaderModelCompile::SM6_1: version = L"6_1"; break;
        case ShaderModelCompile::SM6_2: version = L"6_2"; break;
        case ShaderModelCompile::SM6_3: version = L"6_3"; break;
        case ShaderModelCompile::SM6_4: version = L"6_4"; break;
        case ShaderModelCompile::SM6_5: version = L"6_5"; break;
        case ShaderModelCompile::SM6_6: version = L"6_6"; break;
        case ShaderModelCompile::SM6_7: version = L"6_7"; break;
    }

    return std::wstring(prefix) + L"_" + version;
}

// ─────────────────────────────────────────────────────────────────────────────
// Argument Building
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::wstring> ShaderCompiler::BuildArguments(
    const ShaderCompileDesc& desc) const
{
    std::vector<std::wstring> args;

    // Entry point
    args.push_back(L"-E");
    args.push_back(ToWide(desc.entryPoint));

    // Target profile
    args.push_back(L"-T");
    args.push_back(GetTargetProfile(desc.stage, desc.model));

    // SPIRV output
    if (desc.output == ShaderOutputFormat::SPIRV) {
        args.push_back(L"-spirv");
        args.push_back(L"-fspv-target-env=vulkan1.3");
    }

    // Debug info
    if (desc.generateDebugInfo) {
        args.push_back(L"-Zi");
    }

    // Disable optimizations
    if (desc.disableOptimizations) {
        args.push_back(L"-Od");
    }

    // Preprocessor defines
    for (const auto& def : desc.defines) {
        args.push_back(L"-D");
        args.push_back(ToWide(def));
    }

    // Source name for error messages
    if (!desc.sourceName.empty()) {
        args.push_back(ToWide(desc.sourceName));
    }

    return args;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile
// ─────────────────────────────────────────────────────────────────────────────

ShaderCompileResult ShaderCompiler::Compile(const ShaderCompileDesc& desc) {
    ShaderCompileResult result;

    if (!m_compiler || !m_utils) {
        result.errors = "ShaderCompiler not initialized";
        return result;
    }

    // Build source buffer
    DxcBuffer sourceBuffer{};
    sourceBuffer.Ptr = desc.source.c_str();
    sourceBuffer.Size = desc.source.size();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    // Build arguments
    auto argStrings = BuildArguments(desc);
    std::vector<LPCWSTR> argPtrs;
    argPtrs.reserve(argStrings.size());
    for (const auto& arg : argStrings) {
        argPtrs.push_back(arg.c_str());
    }

    // Compile
    IDxcResult* pResult = nullptr;
    HRESULT hr = m_compiler->Compile(
        &sourceBuffer,
        argPtrs.data(),
        static_cast<UINT32>(argPtrs.size()),
        nullptr,
        IID_IDxcResult,
        reinterpret_cast<LPVOID*>(&pResult));

    if (FAILED(hr)) {
        result.errors = "IDxcCompiler3::Compile failed (hr=0x" +
                         std::to_string(hr) + ")";
        return result;
    }

    // Extract errors/warnings
    IDxcBlobUtf8* pErrors = nullptr;
    pResult->GetOutput(DXC_OUT_ERRORS, IID_IDxcBlobUtf8,
                        reinterpret_cast<void**>(&pErrors), nullptr);
    if (pErrors && pErrors->GetStringLength() > 0) {
        result.errors = std::string(pErrors->GetStringPointer(),
                                     pErrors->GetStringLength());
    }

    // Check compilation status
    HRESULT status;
    pResult->GetStatus(&status);

    if (SUCCEEDED(status)) {
        // Extract bytecode
        IDxcBlob* pObject = nullptr;
        pResult->GetOutput(DXC_OUT_OBJECT, IID_IDxcBlob,
                            reinterpret_cast<void**>(&pObject), nullptr);
        if (pObject && pObject->GetBufferSize() > 0) {
            auto* data = static_cast<const uint8_t*>(pObject->GetBufferPointer());
            result.bytecode.assign(data, data + pObject->GetBufferSize());
            result.success = true;
        }
        if (pObject) pObject->Release();
    }

    if (pErrors) pErrors->Release();
    pResult->Release();

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Version
// ─────────────────────────────────────────────────────────────────────────────

std::string ShaderCompiler::GetVersion() const {
    if (!m_compiler) return {};

    IDxcVersionInfo* pVersion = nullptr;
    HRESULT hr = m_compiler->QueryInterface(IID_IDxcVersionInfo,
                                             reinterpret_cast<void**>(&pVersion));
    if (FAILED(hr) || !pVersion) return "unknown";

    UINT32 major = 0, minor = 0;
    pVersion->GetVersion(&major, &minor);
    pVersion->Release();

    return std::to_string(major) + "." + std::to_string(minor);
}

} // namespace uz

#endif // _WIN32
