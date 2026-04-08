/// UzEngine — DX12 Pipeline State Object Creation
///
/// Sampler creation, root signature construction, and graphics PSO creation.
/// Adapts root signature layout based on negotiated shader model (SM5 vs SM6.6 bindless).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_resource_creation.h"
#include "dx12_guids.h"
#include "D3D12MemAlloc.h"
#include "d3dx12.h"
#include "log.h"
#include "paths.h"
#include "profiler.h"

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// DXC API for root signature compilation fallback
//
// On Win10 without Agility SDK dllexport, the system d3d12.dll's serializer
// rejects DIRECTLY_INDEXED root signature flags. DXC can compile an HLSL root
// signature definition with -T rootsig_1_1 which handles these flags natively.
//
// We use the same CROSS_PLATFORM_UUIDOF override trick as shader_compiler.cpp
// because zig-clang does not support __declspec(uuid).
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a hex character to its 4-bit value at compile time.
static constexpr uint8_t DxcHexVal(char c) {
    return (c >= '0' && c <= '9') ? static_cast<uint8_t>(c - '0') :
           (c >= 'a' && c <= 'f') ? static_cast<uint8_t>(c - 'a' + 10) :
           (c >= 'A' && c <= 'F') ? static_cast<uint8_t>(c - 'A' + 10) : 0;
}

/// Parses two hex characters into a byte at compile time.
static constexpr uint8_t DxcHexByte(const char* s, int i) {
    return static_cast<uint8_t>((DxcHexVal(s[i]) << 4) | DxcHexVal(s[i + 1]));
}

/// Parses a UUID string "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" into a GUID.
static constexpr GUID DxcParseUUID(const char* s) {
    return GUID{
        static_cast<uint32_t>(DxcHexVal(s[0]) << 28 | DxcHexVal(s[1]) << 24 |
                   DxcHexVal(s[2]) << 20 | DxcHexVal(s[3]) << 16 |
                   DxcHexVal(s[4]) << 12 | DxcHexVal(s[5]) << 8  |
                   DxcHexVal(s[6]) << 4  | DxcHexVal(s[7])),
        static_cast<uint16_t>(DxcHexVal(s[9]) << 12 | DxcHexVal(s[10]) << 8 |
                   DxcHexVal(s[11]) << 4 | DxcHexVal(s[12])),
        static_cast<uint16_t>(DxcHexVal(s[14]) << 12 | DxcHexVal(s[15]) << 8 |
                   DxcHexVal(s[16]) << 4 | DxcHexVal(s[17])),
        {DxcHexByte(s, 19), DxcHexByte(s, 21),
         DxcHexByte(s, 24), DxcHexByte(s, 26), DxcHexByte(s, 28),
         DxcHexByte(s, 30), DxcHexByte(s, 32), DxcHexByte(s, 34)}
    };
}

// Override the macro so dxcapi.h forward-declares structs and generates IID constants.
#undef CROSS_PLATFORM_UUIDOF
#define CROSS_PLATFORM_UUIDOF(iface, spec)                  \
    struct iface;                                            \
    static constexpr IID IID_##iface = DxcParseUUID(spec);

#include "dxcapi.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation — DX12 Enum Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts RHIFilterMode to D3D12_FILTER.
static D3D12_FILTER ToD3D12Filter(RHIFilterMode filter) {
    switch (filter) {
        case RHIFilterMode::Point:       return D3D12_FILTER_MIN_MAG_MIP_POINT;
        case RHIFilterMode::Linear:      return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        case RHIFilterMode::Anisotropic: return D3D12_FILTER_ANISOTROPIC;
        default:                         return D3D12_FILTER_MIN_MAG_MIP_POINT;
    }
}

/// Converts RHIAddressMode to D3D12_TEXTURE_ADDRESS_MODE.
static D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Wrap:   return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case RHIAddressMode::Clamp:  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case RHIAddressMode::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case RHIAddressMode::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        default:                     return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

/// Converts RHICompareFunc to D3D12_COMPARISON_FUNC (sampler section).
static D3D12_COMPARISON_FUNC ToD3D12CompareFunc(RHICompareFunc func) {
    switch (func) {
        case RHICompareFunc::Never:        return D3D12_COMPARISON_FUNC_NEVER;
        case RHICompareFunc::Less:         return D3D12_COMPARISON_FUNC_LESS;
        case RHICompareFunc::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
        case RHICompareFunc::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHICompareFunc::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
        case RHICompareFunc::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHICompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHICompareFunc::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
        default:                           return D3D12_COMPARISON_FUNC_NEVER;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a sampler descriptor in the global sampler heap.
bool CreateDX12Sampler(ID3D12Device* device,
                       DescriptorAllocator& samplerHeap,
                       const RHISamplerDesc& desc,
                       DX12Sampler& outSampler) {
    UZ_PROFILE_FUNCTION();
    uint32_t idx = samplerHeap.Allocate();
    if (idx == DescriptorAllocator::kInvalidIndex) {
        UZ_LOG(RHI, Error, "CreateSampler: sampler heap full (name='{}')",
               desc.debugName ? desc.debugName : "");
        return false;
    }

    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = ToD3D12Filter(desc.filter);
    samplerDesc.AddressU = ToD3D12AddressMode(desc.addressU);
    samplerDesc.AddressV = ToD3D12AddressMode(desc.addressV);
    samplerDesc.AddressW = ToD3D12AddressMode(desc.addressW);
    samplerDesc.MipLODBias = desc.mipLodBias;
    samplerDesc.MaxAnisotropy = desc.maxAnisotropy;
    samplerDesc.ComparisonFunc = ToD3D12CompareFunc(desc.compareFunc);
    samplerDesc.MinLOD = desc.minLod;
    samplerDesc.MaxLOD = desc.maxLod;
    // BorderColor defaults to {0,0,0,0}

    device->CreateSampler(&samplerDesc, samplerHeap.CpuHandle(idx));
    outSampler.samplerIndex = idx;

    UZ_LOG(RHI, Debug, "CreateSampler: idx={}, filter={} (name='{}')",
           idx, static_cast<int>(desc.filter), desc.debugName ? desc.debugName : "");

    return true;
}

/// Frees a sampler's descriptor index in the global sampler heap.
void DestroyDX12Sampler(DX12Sampler& sampler, DescriptorAllocator& samplerHeap) {
    if (sampler.samplerIndex != UINT32_MAX) {
        samplerHeap.Free(sampler.samplerIndex);
        sampler.samplerIndex = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Root Signature — Global (adapts to shader model)
// ─────────────────────────────────────────────────────────────────────────────

/// Creates the global root signature, adapting layout to the negotiated shader model.
///
/// SM6.6+: uses DIRECTLY_INDEXED flags for bindless heap access. Root layout:
///   [0] 4x uint32 root constants at b0 (descriptor indices)
///   [1] Root CBV at b1 (per-frame data)
///   [2] Root CBV at b2 (per-object data)
///   [3] Root CBV at b4 (shadow data)
///
/// SM5.x: uses Input Assembler + descriptor table for SRV/texture binding. Root layout:
///   [0] 4x uint32 root constants at b0
///   [1] Root CBV at b1 (per-frame data)
///   [2] Root CBV at b2 (per-object data)
///   [3] Root CBV at b4 (shadow data)
///   [4] SRV descriptor table at t0, space0, unbounded (textures + structured buffers)
ID3D12RootSignature* CreateBindlessRootSignature(
        ID3D12Device* device, ShaderModel sm,
        D3D_ROOT_SIGNATURE_VERSION maxVersion) {
    UZ_PROFILE_FUNCTION();
    const bool bindless = (sm >= ShaderModel::SM6_6);

    // SM6.6: 5 root params (+ UAV table for compute). SM5: 6 root params (+ SRV table).
    D3D12_ROOT_PARAMETER1 rootParams[7] = {};
    uint32_t numParams = 6;  // 0=b0, 1=b1, 2=b2, 3=b4(shadow), 4=b3(material), 5=UAV

    // Root parameter 0: root constants (uint32 descriptorIndex) at b0
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;   // b0
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 16; // 16 x uint32 — supports root indices + algorithm params
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root parameter 1: root CBV for per-frame data at b1
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1;  // b1 — per-frame
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root parameter 2: root CBV for per-object data at b2
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 2;  // b2 — per-object
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root parameter 3: root CBV for shadow data at b4
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].Descriptor.ShaderRegister = 4;  // b4 — shadow data
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root parameter 4: UAV descriptor table at u0 (for compute shaders)
    D3D12_DESCRIPTOR_RANGE1 uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 4; // u0..u3 (enough for IBL bake + future compute)
    uavRange.BaseShaderRegister = 0;    // u0
    uavRange.RegisterSpace = 0;
    uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[4].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root parameter 5: root CBV for material graph params at b3
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[5].Descriptor.ShaderRegister = 3;  // b3 — material graph params + texture indices
    rootParams[5].Descriptor.RegisterSpace = 0;
    rootParams[5].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // SM5 only: Root parameter 6 — SRV descriptor table for textures/structured buffers
    D3D12_DESCRIPTOR_RANGE1 srvRange = {};
    if (!bindless) {
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = UINT_MAX; // unbounded
        srvRange.BaseShaderRegister = 0;    // t0
        srvRange.RegisterSpace = 0;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[6].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[6].DescriptorTable.pDescriptorRanges = &srvRange;
        rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        numParams = 7;  // 0=b0, 1=b1, 2=b2, 3=b4, 4=UAV, 5=b3(material), 6=SRV(SM5)
    }

    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    if (bindless) {
        flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    } else {
        // SM5: allow Input Assembler vertex/index buffer binding
        flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    }

    // Static sampler s0: anisotropic wrap (used by mesh_ps and any textured shader)
    D3D12_STATIC_SAMPLER_DESC staticSamplers[4] = {};
    /// 16x anisotropic filtering — standard quality for material textures.
    staticSamplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].MipLODBias = 0.f;
    staticSamplers[0].MaxAnisotropy = 16;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSamplers[0].MinLOD = 0.f;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;  // s0
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static sampler s1: comparison sampler for shadow PCF sampling
    staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].MipLODBias = 0.f;
    staticSamplers[1].MaxAnisotropy = 1;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS; // Shadow comparison: pixel lit when reference < stored (standard convention with reversed-Z ortho)
    staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSamplers[1].MinLOD = 0.f;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 1;  // s1
    staticSamplers[1].RegisterSpace = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static sampler s2: bilinear clamp (post-process, depth sampling, temporal reads)
    staticSamplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].MipLODBias = 0.f;
    staticSamplers[2].MaxAnisotropy = 1;
    staticSamplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSamplers[2].MinLOD = 0.f;
    staticSamplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[2].ShaderRegister = 2;  // s2
    staticSamplers[2].RegisterSpace = 0;
    staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static sampler s3: bilinear wrap (3D noise textures, weather maps)
    staticSamplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[3].MipLODBias = 0.f;
    staticSamplers[3].MaxAnisotropy = 1;
    staticSamplers[3].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[3].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSamplers[3].MinLOD = 0.f;
    staticSamplers[3].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[3].ShaderRegister = 3;  // s3
    staticSamplers[3].RegisterSpace = 0;
    staticSamplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1.NumParameters = numParams;
    versionedDesc.Desc_1_1.pParameters = rootParams;
    versionedDesc.Desc_1_1.NumStaticSamplers = 4;
    versionedDesc.Desc_1_1.pStaticSamplers = staticSamplers;
    versionedDesc.Desc_1_1.Flags = flags;

    // Serialize with the d3dx12 helper which auto-downgrades 1.1→1.0.
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&versionedDesc, maxVersion,
                                                        &serialized, &error);

    // On Win10 without Agility SDK dllexport, the system d3d12.dll serializer rejects
    // DIRECTLY_INDEXED flags (0xC00). Fall back to DXC which can compile an HLSL root
    // signature definition with -T rootsig_1_1 — DXC ships with SM6.6 support and
    // handles these flags natively. The resulting bytecode is accepted by the Agility
    // SDK device created via CreateDeviceFactory.
    if (FAILED(hr) && bindless) {
        UZ_LOG(RHI, Info, "System serializer rejected bindless flags (hr=0x{:08X}) — "
               "compiling root signature via DXC", static_cast<uint32_t>(hr));
        error.Reset();
        serialized.Reset();

        // HLSL root signature definition — must match the C++ layout EXACTLY:
        //   [0] 16x root constants at b0
        //   [1] CBV at b1 (DATA_STATIC_WHILE_SET_AT_EXECUTE)
        //   [2] CBV at b2 (DATA_STATIC_WHILE_SET_AT_EXECUTE)
        //   [3] CBV at b4 (DATA_STATIC_WHILE_SET_AT_EXECUTE)
        //   [4] UAV table u0..u3 (DESCRIPTORS_VOLATILE)
        //   s0: aniso16 wrap, s1: comparison linear/point border (shadow PCF),
        //   s2: trilinear clamp, s3: trilinear wrap
        static const char kRootSigHLSL[] =
            "#define RS \""
            "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED),"
            "RootConstants(num32BitConstants=16, b0, visibility=SHADER_VISIBILITY_ALL),"
            "CBV(b1, flags=DATA_STATIC_WHILE_SET_AT_EXECUTE, visibility=SHADER_VISIBILITY_ALL),"
            "CBV(b2, flags=DATA_STATIC_WHILE_SET_AT_EXECUTE, visibility=SHADER_VISIBILITY_ALL),"
            "CBV(b4, flags=DATA_STATIC_WHILE_SET_AT_EXECUTE, visibility=SHADER_VISIBILITY_ALL),"
            "DescriptorTable(UAV(u0, numDescriptors=4, flags=DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_ALL),"
            "StaticSampler(s0, filter=FILTER_ANISOTROPIC, maxAnisotropy=16, "
                "addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP, "
                "comparisonFunc=COMPARISON_NEVER, borderColor=STATIC_BORDER_COLOR_OPAQUE_BLACK, "
                "maxLOD=3.402823466e+38),"
            "StaticSampler(s1, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, "
                "addressU=TEXTURE_ADDRESS_BORDER, addressV=TEXTURE_ADDRESS_BORDER, addressW=TEXTURE_ADDRESS_BORDER, "
                "comparisonFunc=COMPARISON_LESS, borderColor=STATIC_BORDER_COLOR_OPAQUE_BLACK, "
                "maxLOD=3.402823466e+38),"
            "StaticSampler(s2, filter=FILTER_MIN_MAG_MIP_LINEAR, "
                "addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP, "
                "comparisonFunc=COMPARISON_NEVER, borderColor=STATIC_BORDER_COLOR_OPAQUE_BLACK, "
                "maxLOD=3.402823466e+38),"
            "StaticSampler(s3, filter=FILTER_MIN_MAG_MIP_LINEAR, "
                "addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP, "
                "comparisonFunc=COMPARISON_NEVER, borderColor=STATIC_BORDER_COLOR_OPAQUE_BLACK, "
                "maxLOD=3.402823466e+38)"
            "\"\n";

        // DXC may already be loaded by ShaderCompiler — check first.
        HMODULE dxcModule = GetModuleHandleA("dxcompiler.dll");
        if (!dxcModule) {
            // ShaderCompiler hasn't initialized yet (root sig creation runs before
            // ShaderManager). Load dxcompiler.dll from the engine directory where
            // it's deployed alongside uzengine_runtime.dll and D3D12Core.dll.
            std::string dxcPath = GetExeDir().string() + "\\dxcompiler.dll";
            dxcModule = LoadLibraryA(dxcPath.c_str());
        }

        if (dxcModule) {
            auto pfnDxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
                GetProcAddress(dxcModule, "DxcCreateInstance"));

            if (pfnDxcCreateInstance) {
                ComPtr<IDxcUtils> dxcUtils;
                ComPtr<IDxcCompiler3> dxcCompiler;
                pfnDxcCreateInstance(CLSID_DxcUtils, IID_IDxcUtils,
                                     reinterpret_cast<void**>(dxcUtils.GetAddressOf()));
                pfnDxcCreateInstance(CLSID_DxcCompiler, IID_IDxcCompiler3,
                                     reinterpret_cast<void**>(dxcCompiler.GetAddressOf()));

                if (dxcUtils && dxcCompiler) {
                    ComPtr<IDxcBlobEncoding> sourceBlob;
                    dxcUtils->CreateBlobFromPinned(
                        kRootSigHLSL, sizeof(kRootSigHLSL) - 1,
                        DXC_CP_UTF8, sourceBlob.GetAddressOf());

                    // Compile with rootsig_1_1 target — DXC produces valid bytecode
                    // that includes DIRECTLY_INDEXED flags regardless of system d3d12.dll version.
                    LPCWSTR args[] = {
                        L"-T", L"rootsig_1_1",
                        L"-E", L"RS",
                    };
                    DxcBuffer sourceBuffer = {
                        sourceBlob->GetBufferPointer(),
                        sourceBlob->GetBufferSize(),
                        DXC_CP_UTF8
                    };

                    ComPtr<IDxcResult> dxcResult;
                    HRESULT dxcHr = dxcCompiler->Compile(
                        &sourceBuffer, args, _countof(args),
                        nullptr, IID_IDxcResult,
                        reinterpret_cast<void**>(dxcResult.GetAddressOf()));

                    if (SUCCEEDED(dxcHr) && dxcResult) {
                        HRESULT status;
                        dxcResult->GetStatus(&status);

                        if (SUCCEEDED(status)) {
                            ComPtr<IDxcBlob> rootSigBlob;
                            dxcResult->GetOutput(DXC_OUT_OBJECT, IID_IDxcBlob,
                                reinterpret_cast<void**>(rootSigBlob.GetAddressOf()),
                                nullptr);

                            if (rootSigBlob && rootSigBlob->GetBufferSize() > 0) {
                                ID3D12RootSignature* rootSig = nullptr;
                                hr = device->CreateRootSignature(0,
                                    rootSigBlob->GetBufferPointer(),
                                    rootSigBlob->GetBufferSize(),
                                    IID_ID3D12RootSignature,
                                    reinterpret_cast<void**>(&rootSig));

                                if (SUCCEEDED(hr)) {
                                    UZ_LOG(RHI, Info, "Root signature created via DXC "
                                           "(SM6.6 DIRECTLY_INDEXED, rootsig_1_1)");
                                    return rootSig;
                                }
                                UZ_LOG(RHI, Error, "CreateRootSignature failed with DXC "
                                       "blob, hr=0x{:08X}", static_cast<uint32_t>(hr));
                            }
                        } else {
                            ComPtr<IDxcBlobUtf8> errors;
                            dxcResult->GetOutput(DXC_OUT_ERRORS, IID_IDxcBlobUtf8,
                                reinterpret_cast<void**>(errors.GetAddressOf()),
                                nullptr);
                            if (errors && errors->GetStringLength() > 0) {
                                UZ_LOG(RHI, Error, "DXC root sig compile failed: {}",
                                       errors->GetStringPointer());
                            }
                        }
                    }
                }
            }
        } else {
            UZ_LOG(RHI, Warning, "dxcompiler.dll not available for root sig compilation");
        }

        // Last resort: strip bindless flags so at least a non-bindless root signature
        // can be created. Shaders compiled for SM6.6 may still work if the device
        // supports bindless without the root signature flags (device factory mode).
        UZ_LOG(RHI, Warning, "DXC root sig path failed — stripping DIRECTLY_INDEXED "
               "for serialization (device factory mode)");
        error.Reset();
        versionedDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        hr = D3DX12SerializeVersionedRootSignature(&versionedDesc, maxVersion,
                                                    &serialized, &error);
    }
    if (FAILED(hr)) {
        const char* errMsg = error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown";
        UZ_LOG(RHI, Error, "Failed to serialize root signature: {}", errMsg);
        return nullptr;
    }

    ID3D12RootSignature* rootSig = nullptr;
    hr = device->CreateRootSignature(0,
        serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_ID3D12RootSignature, reinterpret_cast<void**>(&rootSig));

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create root signature, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return nullptr;
    }

    if (bindless) {
        UZ_LOG(RHI, Info, "Root signature created (SM6.6, serialized as v{}, flags stripped)",
               maxVersion == D3D_ROOT_SIGNATURE_VERSION_1_1 ? "1.1" : "1.0");
    } else {
        UZ_LOG(RHI, Info, "Root signature created (standard)");
    }
    return rootSig;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline State Object Creation — DX12 Enum Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts RHICullMode to D3D12_CULL_MODE.
static D3D12_CULL_MODE ToDx12CullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None:  return D3D12_CULL_MODE_NONE;
        case RHICullMode::Front: return D3D12_CULL_MODE_FRONT;
        case RHICullMode::Back:  return D3D12_CULL_MODE_BACK;
    }
    return D3D12_CULL_MODE_BACK;
}

/// Converts RHIFillMode to D3D12_FILL_MODE.
static D3D12_FILL_MODE ToDx12FillMode(RHIFillMode mode) {
    switch (mode) {
        case RHIFillMode::Solid:     return D3D12_FILL_MODE_SOLID;
        case RHIFillMode::Wireframe: return D3D12_FILL_MODE_WIREFRAME;
    }
    return D3D12_FILL_MODE_SOLID;
}

/// Converts RHICompareFunc to D3D12_COMPARISON_FUNC.
static D3D12_COMPARISON_FUNC ToDx12CompareFunc(RHICompareFunc func) {
    switch (func) {
        case RHICompareFunc::Never:        return D3D12_COMPARISON_FUNC_NEVER;
        case RHICompareFunc::Less:         return D3D12_COMPARISON_FUNC_LESS;
        case RHICompareFunc::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
        case RHICompareFunc::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHICompareFunc::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
        case RHICompareFunc::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHICompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHICompareFunc::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_LESS;
}

/// Converts RHIBlendFactor to D3D12_BLEND.
static D3D12_BLEND ToDx12Blend(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero:         return D3D12_BLEND_ZERO;
        case RHIBlendFactor::One:          return D3D12_BLEND_ONE;
        case RHIBlendFactor::SrcColor:     return D3D12_BLEND_SRC_COLOR;
        case RHIBlendFactor::InvSrcColor:  return D3D12_BLEND_INV_SRC_COLOR;
        case RHIBlendFactor::SrcAlpha:     return D3D12_BLEND_SRC_ALPHA;
        case RHIBlendFactor::InvSrcAlpha:  return D3D12_BLEND_INV_SRC_ALPHA;
        case RHIBlendFactor::DstColor:     return D3D12_BLEND_DEST_COLOR;
        case RHIBlendFactor::InvDstColor:  return D3D12_BLEND_INV_DEST_COLOR;
        case RHIBlendFactor::DstAlpha:     return D3D12_BLEND_DEST_ALPHA;
        case RHIBlendFactor::InvDstAlpha:  return D3D12_BLEND_INV_DEST_ALPHA;
    }
    return D3D12_BLEND_ONE;
}

/// Converts RHIBlendOp to D3D12_BLEND_OP.
static D3D12_BLEND_OP ToDx12BlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add:         return D3D12_BLEND_OP_ADD;
        case RHIBlendOp::Subtract:    return D3D12_BLEND_OP_SUBTRACT;
        case RHIBlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case RHIBlendOp::Min:         return D3D12_BLEND_OP_MIN;
        case RHIBlendOp::Max:         return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}

/// Converts RHIPrimitiveTopology to D3D12_PRIMITIVE_TOPOLOGY_TYPE.
static D3D12_PRIMITIVE_TOPOLOGY_TYPE ToDx12TopologyType(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:
        case RHIPrimitiveTopology::TriangleStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        case RHIPrimitiveTopology::LineList:
        case RHIPrimitiveTopology::LineStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case RHIPrimitiveTopology::PointList:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline State Object Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a graphics PSO from RHIGraphicsPipelineDesc, resolving shader handles from the pool.
bool CreateDX12GraphicsPipeline(ID3D12Device* device,
                                 ID3D12RootSignature* rootSignature,
                                 const RHIGraphicsPipelineDesc& desc,
                                 const ResourcePool<DX12Shader>& shaderPool,
                                 DX12Pipeline& outPipeline) {
    UZ_PROFILE_FUNCTION();
    // Resolve shader bytecodes
    static constexpr size_t kMaxShaderBytecodeSize = 64 * 1024 * 1024; // 64 MB sanity limit

    const DX12Shader* vs = shaderPool.Get(desc.vertexShader.index, desc.vertexShader.generation);
    if (!vs || vs->bytecode.empty()) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: invalid vertex shader (name='{}')",
               desc.debugName ? desc.debugName : "");
        return false;
    }
    if (vs->bytecode.size() > kMaxShaderBytecodeSize) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: vertex shader bytecode exceeds max size: {} bytes (name='{}')",
               vs->bytecode.size(), desc.debugName ? desc.debugName : "");
        return false;
    }

    const DX12Shader* ps = nullptr;
    if (desc.pixelShader.index != UINT32_MAX && desc.pixelShader.generation != 0) {
        ps = shaderPool.Get(desc.pixelShader.index, desc.pixelShader.generation);
        if (!ps || ps->bytecode.empty()) {
            UZ_LOG(RHI, Error, "CreateGraphicsPipeline: invalid pixel shader (name='{}')",
                   desc.debugName ? desc.debugName : "");
            return false;
        }
        if (ps->bytecode.size() > kMaxShaderBytecodeSize) {
            UZ_LOG(RHI, Error, "CreateGraphicsPipeline: pixel shader bytecode exceeds max size: {} bytes (name='{}')",
                   ps->bytecode.size(), desc.debugName ? desc.debugName : "");
            return false;
        }
    }

    // Build D3D12 PSO descriptor
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;

    // Shaders
    psoDesc.VS.pShaderBytecode = vs->bytecode.data();
    psoDesc.VS.BytecodeLength = vs->bytecode.size();
    if (ps) {
        psoDesc.PS.pShaderBytecode = ps->bytecode.data();
        psoDesc.PS.BytecodeLength = ps->bytecode.size();
    }

    // Input layout: SM5 uses Input Assembler, SM6.6 uses bindless SRV fetch
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    if (desc.inputElementCount > 0 && desc.inputElements != nullptr) {
        inputElements.reserve(desc.inputElementCount);
        for (uint32_t i = 0; i < desc.inputElementCount; ++i) {
            const auto& elem = desc.inputElements[i];
            D3D12_INPUT_ELEMENT_DESC d3dElem = {};
            d3dElem.SemanticName = InputSemanticToString(elem.semantic);
            d3dElem.SemanticIndex = elem.semanticIndex;
            d3dElem.Format = ToDxgiFormat(elem.format);
            d3dElem.InputSlot = elem.inputSlot;
            d3dElem.AlignedByteOffset = elem.byteOffset;
            d3dElem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            d3dElem.InstanceDataStepRate = 0;
            inputElements.push_back(d3dElem);
        }
        psoDesc.InputLayout.pInputElementDescs = inputElements.data();
        psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
    } else {
        psoDesc.InputLayout.pInputElementDescs = nullptr;
        psoDesc.InputLayout.NumElements = 0;
    }

    // Rasterizer
    psoDesc.RasterizerState.FillMode = ToDx12FillMode(desc.rasterizer.fillMode);
    psoDesc.RasterizerState.CullMode = ToDx12CullMode(desc.rasterizer.cullMode);
    psoDesc.RasterizerState.FrontCounterClockwise = desc.rasterizer.frontCounterClockwise;
    psoDesc.RasterizerState.DepthBias = static_cast<INT>(desc.rasterizer.depthBias);
    psoDesc.RasterizerState.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Depth-stencil
    psoDesc.DepthStencilState.DepthEnable = desc.depthStencil.depthEnable;
    psoDesc.DepthStencilState.DepthWriteMask = desc.depthStencil.depthWrite
        ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = ToDx12CompareFunc(desc.depthStencil.depthFunc);
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DSVFormat = ToDxgiFormat(desc.depthStencilFormat);

    // Blend state — set a valid default for RT[0] even when renderTargetCount=0,
    // as some DX12 drivers reject PSOs with zeroed blend state on all targets.
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = (desc.renderTargetCount > 1) ? TRUE : FALSE;
    if (desc.renderTargetCount == 0) {
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    for (uint32_t i = 0; i < desc.renderTargetCount && i < 8; ++i) {
        auto& src = desc.renderTargetBlend[i];
        auto& dst = psoDesc.BlendState.RenderTarget[i];
        dst.BlendEnable = src.blendEnable;
        dst.SrcBlend = ToDx12Blend(src.srcColor);
        dst.DestBlend = ToDx12Blend(src.dstColor);
        dst.BlendOp = ToDx12BlendOp(src.colorOp);
        dst.SrcBlendAlpha = ToDx12Blend(src.srcAlpha);
        dst.DestBlendAlpha = ToDx12Blend(src.dstAlpha);
        dst.BlendOpAlpha = ToDx12BlendOp(src.alphaOp);
        dst.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        dst.LogicOpEnable = FALSE;
    }

    // Render target formats
    psoDesc.NumRenderTargets = desc.renderTargetCount;
    for (uint32_t i = 0; i < desc.renderTargetCount && i < 8; ++i) {
        psoDesc.RTVFormats[i] = ToDxgiFormat(desc.renderTargetFormats[i]);
    }

    // Topology
    psoDesc.PrimitiveTopologyType = ToDx12TopologyType(desc.topology);

    // Sample desc (no MSAA)
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = UINT_MAX;

    // Apply cached PSO blob if provided (skips driver compilation)
    if (desc.cachedBlobData && desc.cachedBlobSize > 0) {
        psoDesc.CachedPSO.pCachedBlob = desc.cachedBlobData;
        psoDesc.CachedPSO.CachedBlobSizeInBytes = desc.cachedBlobSize;
    }

    // Create PSO
    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc,
        IID_ID3D12PipelineState, reinterpret_cast<void**>(&pso));
    // If creation fails with a cached blob, retry without it (stale cache)
    if (FAILED(hr) && psoDesc.CachedPSO.pCachedBlob != nullptr) {
        UZ_LOG(RHI, Warning, "CreateGraphicsPipelineState failed with cached blob (hr=0x{:08X}), retrying without cache (name='{}')",
               static_cast<uint32_t>(hr), desc.debugName ? desc.debugName : "");
        psoDesc.CachedPSO.pCachedBlob = nullptr;
        psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;
        hr = device->CreateGraphicsPipelineState(&psoDesc,
            IID_ID3D12PipelineState, reinterpret_cast<void**>(&pso));
    }

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipelineState failed: hr=0x{:08X} (name='{}') "
               "VS={} PS={} numRT={} DSV={} topology={}",
               static_cast<uint32_t>(hr), desc.debugName ? desc.debugName : "",
               psoDesc.VS.BytecodeLength, psoDesc.PS.BytecodeLength,
               psoDesc.NumRenderTargets,
               static_cast<uint32_t>(psoDesc.DSVFormat),
               static_cast<uint32_t>(psoDesc.PrimitiveTopologyType));

        return false;
    }

    outPipeline.pso = pso;
    outPipeline.desc = desc;

    UZ_LOG(RHI, Info, "Graphics pipeline created (name='{}')",
           desc.debugName ? desc.debugName : "");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Pipeline State Object Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a compute PSO from RHIComputePipelineDesc, resolving the shader handle from the pool.
bool CreateDX12ComputePipeline(ID3D12Device* device,
                                ID3D12RootSignature* rootSignature,
                                const RHIComputePipelineDesc& desc,
                                const ResourcePool<DX12Shader>& shaderPool,
                                DX12Pipeline& outPipeline) {
    UZ_PROFILE_FUNCTION();
    static constexpr size_t kMaxShaderBytecodeSize = 64 * 1024 * 1024; // 64 MB sanity limit

    const DX12Shader* cs = shaderPool.Get(desc.computeShader.index, desc.computeShader.generation);
    if (!cs || cs->bytecode.empty()) {
        UZ_LOG(RHI, Error, "CreateComputePipeline: invalid compute shader (name='{}')",
               desc.debugName ? desc.debugName : "");
        return false;
    }
    if (cs->stage != RHIShaderStage::Compute) {
        UZ_LOG(RHI, Error, "CreateComputePipeline: shader is not a compute shader, stage={} (name='{}')",
               static_cast<int>(cs->stage), desc.debugName ? desc.debugName : "");
        return false;
    }
    if (cs->bytecode.size() > kMaxShaderBytecodeSize) {
        UZ_LOG(RHI, Error, "CreateComputePipeline: compute shader bytecode exceeds max size: {} bytes (name='{}')",
               cs->bytecode.size(), desc.debugName ? desc.debugName : "");
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.CS.pShaderBytecode = cs->bytecode.data();
    psoDesc.CS.BytecodeLength = cs->bytecode.size();

    // Apply cached PSO blob if provided (skips driver compilation)
    if (desc.cachedBlobData && desc.cachedBlobSize > 0) {
        psoDesc.CachedPSO.pCachedBlob = desc.cachedBlobData;
        psoDesc.CachedPSO.CachedBlobSizeInBytes = desc.cachedBlobSize;
    }

    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = device->CreateComputePipelineState(&psoDesc,
        IID_ID3D12PipelineState, reinterpret_cast<void**>(&pso));

    // If creation fails with a cached blob, retry without it (stale cache)
    if (FAILED(hr) && psoDesc.CachedPSO.pCachedBlob != nullptr) {
        UZ_LOG(RHI, Warning, "CreateComputePipelineState failed with cached blob (hr=0x{:08X}), retrying without cache (name='{}')",
               static_cast<uint32_t>(hr), desc.debugName ? desc.debugName : "");
        psoDesc.CachedPSO.pCachedBlob = nullptr;
        psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;
        hr = device->CreateComputePipelineState(&psoDesc,
            IID_ID3D12PipelineState, reinterpret_cast<void**>(&pso));
    }

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateComputePipelineState failed: hr=0x{:08X} (name='{}') CS={}",
               static_cast<uint32_t>(hr), desc.debugName ? desc.debugName : "",
               psoDesc.CS.BytecodeLength);
        return false;
    }

    outPipeline.pso = pso;
    outPipeline.isCompute = true;
    // Graphics desc is not applicable for compute — leave default-initialized

    UZ_LOG(RHI, Info, "Compute pipeline created (name='{}')",
           desc.debugName ? desc.debugName : "");
    return true;
}

/// Releases a pipeline state object's COM reference.
void DestroyDX12Pipeline(DX12Pipeline& pipeline) {
    if (pipeline.pso) {
        pipeline.pso->Release();
        pipeline.pso = nullptr;
    }
}

} // namespace uz
