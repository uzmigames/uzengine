#include "nrd_sdk_denoiser.h"
#include "render_graph_builder.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"
#include "paths.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(_WIN32) && defined(UZ_NRD_SDK_ENABLED)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "engine/rhi/dx12/dx12_device.h"
#include "engine/rhi/dx12/dx12_command_list.h"
#include "engine/rhi/dx12/dx12_compat.h"
#include "engine_shaders.h"
#include "shader_compiler.h"
#include "NRD.h"
#define UZ_NRD_SDK_COMPILED 1
#endif

UZ_DECLARE_LOG_CATEGORY(Renderer);
static auto* cv_NrdSdk = UZ_CVAR_BOOL("r.NRD.SDK", false,
    "Opt-in NVIDIA NRD SDK (REBLUR diffuse + RELAX specular, NRD.dll) instead of the native stage-2 reconstruction; needs r.NRD=1");
static auto* cv_NrdSdkDiffuseFrames = UZ_CVAR_INT("r.NRD.SDK.DiffuseFrames", 30,
    "REBLUR maxAccumulatedFrameNum for the diffuse channel", 0, 63);
static auto* cv_NrdSdkSpecularFrames = UZ_CVAR_INT("r.NRD.SDK.SpecularFrames", 30,
    "RELAX specularMaxAccumulatedFrameNum for the specular channel", 0, 255);
static auto* cv_NrdSdkUnitsPerMeter = UZ_CVAR_FLOAT("r.NRD.SDK.UnitsPerMeter", 100.f,
    "World units per meter for NRD's metric defaults (hit-distance normalization); the engine uses centimeters", 0.001f, 100000.f);
static auto* cv_NrdSdkFastFrames = UZ_CVAR_INT("r.NRD.SDK.FastFrames", 6,
    "REBLUR/RELAX fast-history length in frames (the history clamping reference); NRD default 6", 0, 63);
static auto* cv_NrdSdkClampSigma = UZ_CVAR_FLOAT("r.NRD.SDK.ClampSigma", 2.f,
    "REBLUR/RELAX fastHistoryClampingSigmaScale (standard deviations of the fast-history color box); NRD default 2", 1.f, 3.f);
// Measured on scripts/rendering/test_nrd_sdk.py (1-spp emitter-lit fixture, 640x360, frame 32 vs a
// 1024-frame reference): NRD's pre-accumulation pre-passes lose energy on this non-probabilistic
// signal (RELAX specular pre-pass: -13.9% on the glossy floor at any radius, 0 px: -2.4%) and REBLUR's
// anti-firefly costs MSE (6.55 -> 6.29 off). NRD documents the pre-pass as required only for
// probabilistic lobe selection, which the stage-2 attribution does not use. NRD's own defaults
// (30 / 50 px, anti-firefly on) stay reachable through these CVars.
static auto* cv_NrdSdkAntiFirefly = UZ_CVAR_BOOL("r.NRD.SDK.AntiFirefly", false,
    "REBLUR enableAntiFirefly for the diffuse channel (NRD default on; off measured -4% MSE here); RELAX specular keeps NRD's default off");
static auto* cv_NrdSdkDiffusePrepass = UZ_CVAR_FLOAT("r.NRD.SDK.DiffusePrepassRadius", 0.f,
    "REBLUR diffusePrepassBlurRadius in pixels (0 disables the pre-accumulation spatial reuse; NRD default 30)", 0.f, 200.f);
static auto* cv_NrdSdkSpecularPrepass = UZ_CVAR_FLOAT("r.NRD.SDK.SpecularPrepassRadius", 0.f,
    "RELAX specularPrepassBlurRadius in pixels (0 disables the pre-accumulation spatial reuse; NRD default 50 loses 14% specular energy on sparse emitter reflections)", 0.f, 200.f);
static auto* cv_NrdSdkAntiFireflyFrames = UZ_CVAR_INT("r.NRD.SDK.AntiFireflyFrames", 8,
    "Frames after a history reset during which REBLUR anti-firefly stays on regardless of r.NRD.SDK.AntiFirefly (a 1-spp reset frame is dominated by fireflies); 0 never", 0, 64);
static auto* cv_NrdSdkSpecularPhi = UZ_CVAR_FLOAT("r.NRD.SDK.SpecularPhiLuminance", 1.f,
    "RELAX specularPhiLuminance (A-trous luminance edge-stopping sensitivity); NRD default 1", 0.01f, 1000.f);
static auto* cv_NrdSdkSpecularMinLum = UZ_CVAR_FLOAT("r.NRD.SDK.SpecularMinLuminanceWeight", 0.f,
    "RELAX specularMinLuminanceWeight (1 = no luminance edge stopping in the specular A-trous); NRD default 0", 0.f, 1.f);
static auto* cv_NrdSdkSplitScreen = UZ_CVAR_FLOAT("r.NRD.SDK.SplitScreen", 0.f,
    "NRD split-screen diagnostic: fraction of the width showing the noisy input", 0.f, 1.f);
static auto* cv_NrdSdkDebug = UZ_CVAR_INT("r.NRD.SDK.DebugSignal", 0,
    "NRD SDK output diagnostic: 0 composite, 1 raw, 2 residual, 3 specular, 4 diffuse, 5 denoised diffuse, 6 denoised specular", 0, 6);

namespace uz {
#ifdef UZ_NRD_SDK_COMPILED
namespace {
constexpr uint32_t kFrames = 3;            // DX12CommandList::kFramesInFlight
constexpr nrd::Identifier kDiffuseId = 0;  // REBLUR_DIFFUSE
constexpr nrd::Identifier kSpecularId = 1; // RELAX_SPECULAR
constexpr D3D12_RESOURCE_STATES kSrvState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

// NRD.dll exports (NRD.h, NRD_API = extern "C", NRD_CALL = __stdcall on Win32).
struct NrdApi {
    HMODULE module = nullptr;
    nrd::Result (NRD_CALL* CreateInstance)(const nrd::InstanceCreationDesc&, nrd::Instance*&) = nullptr;
    void (NRD_CALL* DestroyInstance)(nrd::Instance&) = nullptr;
    const nrd::LibraryDesc* (NRD_CALL* GetLibraryDesc)() = nullptr;
    const nrd::InstanceDesc* (NRD_CALL* GetInstanceDesc)(const nrd::Instance&) = nullptr;
    nrd::Result (NRD_CALL* SetCommonSettings)(nrd::Instance&, const nrd::CommonSettings&) = nullptr;
    nrd::Result (NRD_CALL* SetDenoiserSettings)(nrd::Instance&, nrd::Identifier, const void*) = nullptr;
    nrd::Result (NRD_CALL* GetComputeDispatches)(nrd::Instance&, const nrd::Identifier*, uint32_t,
                                                 const nrd::DispatchDesc*&, uint32_t&) = nullptr;
    const char* (*GetResourceTypeString)(nrd::ResourceType) = nullptr;
    const char* (*GetDenoiserString)(nrd::Denoiser) = nullptr;
};

// One LoadLibrary per process; the DLL lives next to the exe (build.zig deploys
// it) like nvngx_dlss.dll and uz_brixelizer.dll.
const NrdApi* LoadNrdApi() {
    static NrdApi api;
    static bool attempted = false;
    if (attempted) return api.module ? &api : nullptr;
    attempted = true;
    const std::filesystem::path path = uz::GetExeDir() / "NRD.dll";
    api.module = LoadLibraryW(path.wstring().c_str());
    if (!api.module) api.module = LoadLibraryW(L"NRD.dll");
    if (!api.module) {
        UZ_LOG(Renderer, Warning, "NRD SDK: NRD.dll not found next to the executable ({}); run scripts/build_nrd.ps1",
               path.string());
        return nullptr;
    }
    bool ok = true;
    auto resolve = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(api.module, name));
        if (!fn) {
            UZ_LOG(Renderer, Error, "NRD SDK: NRD.dll lacks export '{}'", name);
            ok = false;
        }
    };
    resolve(api.CreateInstance, "CreateInstance");
    resolve(api.DestroyInstance, "DestroyInstance");
    resolve(api.GetLibraryDesc, "GetLibraryDesc");
    resolve(api.GetInstanceDesc, "GetInstanceDesc");
    resolve(api.SetCommonSettings, "SetCommonSettings");
    resolve(api.SetDenoiserSettings, "SetDenoiserSettings");
    resolve(api.GetComputeDispatches, "GetComputeDispatches");
    resolve(api.GetResourceTypeString, "GetResourceTypeString");
    resolve(api.GetDenoiserString, "GetDenoiserString");
    if (!ok) {
        FreeLibrary(api.module);
        api.module = nullptr;
        return nullptr;
    }
    return &api;
}

DXGI_FORMAT ToDxgi(nrd::Format format) {
    switch (format) {
        case nrd::Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
        case nrd::Format::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
        case nrd::Format::R8_UINT: return DXGI_FORMAT_R8_UINT;
        case nrd::Format::R8_SINT: return DXGI_FORMAT_R8_SINT;
        case nrd::Format::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
        case nrd::Format::RG8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
        case nrd::Format::RG8_UINT: return DXGI_FORMAT_R8G8_UINT;
        case nrd::Format::RG8_SINT: return DXGI_FORMAT_R8G8_SINT;
        case nrd::Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case nrd::Format::RGBA8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case nrd::Format::RGBA8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
        case nrd::Format::RGBA8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
        case nrd::Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case nrd::Format::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
        case nrd::Format::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
        case nrd::Format::R16_UINT: return DXGI_FORMAT_R16_UINT;
        case nrd::Format::R16_SINT: return DXGI_FORMAT_R16_SINT;
        case nrd::Format::R16_SFLOAT: return DXGI_FORMAT_R16_FLOAT;
        case nrd::Format::RG16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
        case nrd::Format::RG16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
        case nrd::Format::RG16_UINT: return DXGI_FORMAT_R16G16_UINT;
        case nrd::Format::RG16_SINT: return DXGI_FORMAT_R16G16_SINT;
        case nrd::Format::RG16_SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
        case nrd::Format::RGBA16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case nrd::Format::RGBA16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case nrd::Format::RGBA16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
        case nrd::Format::RGBA16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
        case nrd::Format::RGBA16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case nrd::Format::R32_UINT: return DXGI_FORMAT_R32_UINT;
        case nrd::Format::R32_SINT: return DXGI_FORMAT_R32_SINT;
        case nrd::Format::R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
        case nrd::Format::RG32_UINT: return DXGI_FORMAT_R32G32_UINT;
        case nrd::Format::RG32_SINT: return DXGI_FORMAT_R32G32_SINT;
        case nrd::Format::RG32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case nrd::Format::RGB32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
        case nrd::Format::RGB32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
        case nrd::Format::RGB32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
        case nrd::Format::RGBA32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
        case nrd::Format::RGBA32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
        case nrd::Format::RGBA32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case nrd::Format::R10_G10_B10_A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case nrd::Format::R10_G10_B10_A2_UINT: return DXGI_FORMAT_R10G10B10A2_UINT;
        case nrd::Format::R11_G11_B10_UFLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
        case nrd::Format::R9_G9_B9_E5_UFLOAT: return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT ToDxgi(RHIFormat format) {
    switch (format) {
        case RHIFormat::RG16Float: return DXGI_FORMAT_R16G16_FLOAT;
        case RHIFormat::RGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHIFormat::R32Float: return DXGI_FORMAT_R32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

uint32_t DivideUp(uint32_t value, uint32_t divisor) { return (value + divisor - 1) / divisor; }

// A texture NRD may bind: either one of its pools (owned here as a committed
// resource, because the pools use formats RHIFormat does not expose, such as
// R8_UINT and R16_UINT) or one of the adapter's RHI textures.
struct Tracked {
    ID3D12Resource* resource = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

struct Pipeline {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    uint32_t constantsParam = UINT32_MAX, texturesParam = UINT32_MAX, storagesParam = UINT32_MAX;
};
} // namespace

struct NrdSdkDenoiser::Impl {
    IRHIDevice* device = nullptr;
    DX12Device* dx = nullptr;
    const NrdApi* api = nullptr;
    nrd::Instance* instance = nullptr;
    const nrd::InstanceDesc* desc = nullptr;
    uint32_t width = 0, height = 0;
    // NRD pools (permanent first, then transient) and the user-side textures.
    std::vector<ComPtr<ID3D12Resource>> poolResources;
    std::vector<Tracked> pool;
    RHITextureHandle motion, normalRoughness, viewZ, diffuseInput, specularInput;
    RHITextureHandle diffuseOutput, specularOutput;
    std::vector<Pipeline> pipelines;
    // Per-frame descriptor ring and constant ring (kFrames slots each).
    ComPtr<ID3D12DescriptorHeap> heap;
    uint32_t heapIncrement = 0, descriptorsPerFrame = 0, descriptorCursor = 0;
    ComPtr<ID3D12Resource> constants;
    uint8_t* constantsMapped = nullptr;
    uint32_t constantStride = 0, constantsPerFrame = 0, constantCursor = 0;
    uint32_t frameSlot = 0;
    // Adapter dispatches (pack + modulate) on the engine's bindless heap.
    ComPtr<ID3D12RootSignature> adapterRoot;
    ComPtr<ID3D12PipelineState> packPso, modulatePso;
    // Frame state.
    uint32_t sdkFrameIndex = 0;
    uint32_t framesSinceReset = 0;
    bool clearPending = true;
    Mat4 prevView, prevProj;
    bool havePrev = false;
    bool loggedResourceError = false;

    RHITextureHandle CreateUserTexture(RHIFormat format, const char* name) {
        RHITextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        desc.debugName = name;
        return device->CreateTexture(desc);
    }
    Tracked UserTracked(RHITextureHandle handle, D3D12_RESOURCE_STATES state) const {
        Tracked t{};
        auto* texture = dx->GetTexturePool().Get(handle.index, handle.generation);
        if (texture) {
            t.resource = texture->resource;
            t.format = ToDxgi(texture->desc.format);
        }
        t.state = state;
        return t;
    }
    bool CreatePools() {
        const uint32_t total = desc->permanentPoolSize + desc->transientPoolSize;
        poolResources.resize(total);
        pool.resize(total);
        for (uint32_t i = 0; i < total; ++i) {
            const bool permanent = i < desc->permanentPoolSize;
            const nrd::TextureDesc& texture = permanent ? desc->permanentPool[i]
                                                        : desc->transientPool[i - desc->permanentPoolSize];
            const uint32_t factor = std::max<uint32_t>(1u, texture.downsampleFactor);
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width = DivideUp(width, factor);
            rd.Height = DivideUp(height, factor);
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.Format = ToDxgi(texture.format);
            rd.SampleDesc = {1, 0};
            rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (rd.Format == DXGI_FORMAT_UNKNOWN) {
                UZ_LOG(Renderer, Error, "NRD SDK: pool texture {} uses nrd::Format {} without a DXGI mapping",
                       i, uint32_t(texture.format));
                return false;
            }
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            HRESULT hr = dx->GetDevice()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&poolResources[i]));
            if (FAILED(hr)) {
                UZ_LOG(Renderer, Error, "NRD SDK: CreateCommittedResource failed for pool texture {} (hr=0x{:08X})",
                       i, uint32_t(hr));
                return false;
            }
            const std::wstring name = (permanent ? L"NRD_Permanent_" : L"NRD_Transient_") + std::to_wstring(i);
            poolResources[i]->SetName(name.c_str());
            pool[i] = Tracked{poolResources[i].Get(), rd.Format, D3D12_RESOURCE_STATE_COMMON};
        }
        return true;
    }
    bool CreatePipelines() {
        pipelines.resize(desc->pipelinesNum);
        std::array<D3D12_STATIC_SAMPLER_DESC, 2> samplers{};
        if (desc->samplersNum != samplers.size()) {
            UZ_LOG(Renderer, Error, "NRD SDK: expected {} static samplers, NRD asks for {}", samplers.size(), desc->samplersNum);
            return false;
        }
        for (uint32_t i = 0; i < desc->samplersNum; ++i) {
            auto& s = samplers[i];
            s.Filter = desc->samplers[i] == nrd::Sampler::LINEAR_CLAMP ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
                                                                        : D3D12_FILTER_MIN_MAG_MIP_POINT;
            s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            s.MaxLOD = D3D12_FLOAT32_MAX;
            s.ShaderRegister = desc->samplersBaseRegisterIndex + i;
            s.RegisterSpace = desc->constantBufferAndSamplersSpaceIndex;
            s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        for (uint32_t i = 0; i < desc->pipelinesNum; ++i) {
            const nrd::PipelineDesc& pipelineDesc = desc->pipelines[i];
            Pipeline& pipeline = pipelines[i];
            std::array<D3D12_ROOT_PARAMETER, 3> params{};
            std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{};
            uint32_t paramCount = 0;
            pipeline.constantsParam = paramCount;
            params[paramCount].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[paramCount].Descriptor.ShaderRegister = desc->constantBufferRegisterIndex;
            params[paramCount].Descriptor.RegisterSpace = desc->constantBufferAndSamplersSpaceIndex;
            params[paramCount].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            ++paramCount;
            if (pipelineDesc.resourceRangesNum > ranges.size()) {
                UZ_LOG(Renderer, Error, "NRD SDK: pipeline {} declares {} resource ranges", i, pipelineDesc.resourceRangesNum);
                return false;
            }
            for (uint32_t r = 0; r < pipelineDesc.resourceRangesNum; ++r) {
                const nrd::ResourceRangeDesc& range = pipelineDesc.resourceRanges[r];
                const bool storage = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
                ranges[r].RangeType = storage ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                ranges[r].NumDescriptors = range.descriptorsNum;
                ranges[r].BaseShaderRegister = desc->resourcesBaseRegisterIndex;
                ranges[r].RegisterSpace = desc->resourcesSpaceIndex;
                ranges[r].OffsetInDescriptorsFromTableStart = 0;
                (storage ? pipeline.storagesParam : pipeline.texturesParam) = paramCount;
                params[paramCount].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                params[paramCount].DescriptorTable = {1, &ranges[r]};
                params[paramCount].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                ++paramCount;
            }
            D3D12_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.NumParameters = paramCount;
            rootDesc.pParameters = params.data();
            rootDesc.NumStaticSamplers = uint32_t(samplers.size());
            rootDesc.pStaticSamplers = samplers.data();
            ComPtr<ID3DBlob> serialized, errors;
            HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
            if (SUCCEEDED(hr))
                hr = dx->GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(),
                    serialized->GetBufferSize(), IID_PPV_ARGS(&pipeline.root));
            if (FAILED(hr)) {
                UZ_LOG(Renderer, Error, "NRD SDK: root signature for pipeline {} ('{}') failed: {}", i,
                       pipelineDesc.shaderIdentifier,
                       errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
                return false;
            }
            const nrd::ComputeShaderDesc& dxil = pipelineDesc.computeShaderDXIL;
            if (!dxil.bytecode || dxil.size < 4 || std::memcmp(dxil.bytecode, "DXBC", 4) != 0) {
                UZ_LOG(Renderer, Error, "NRD SDK: pipeline {} ('{}') has no embedded DXIL container", i,
                       pipelineDesc.shaderIdentifier);
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = pipeline.root.Get();
            psoDesc.CS = {dxil.bytecode, size_t(dxil.size)};
            hr = dx->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipeline.pso));
            if (FAILED(hr)) {
                UZ_LOG(Renderer, Error, "NRD SDK: compute PSO for pipeline {} ('{}') failed (hr=0x{:08X})", i,
                       pipelineDesc.shaderIdentifier, uint32_t(hr));
                return false;
            }
        }
        return true;
    }
    bool CreateRings() {
        const auto& limits = desc->descriptorPoolDesc;
        descriptorsPerFrame = std::max(limits.totalTexturesNum + limits.totalStorageTexturesNum,
            limits.setsMaxNum * (limits.perSetTexturesMaxNum + limits.perSetStorageTexturesMaxNum));
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = descriptorsPerFrame * kFrames;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        HRESULT hr = dx->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
        if (FAILED(hr)) {
            UZ_LOG(Renderer, Error, "NRD SDK: descriptor heap ({} descriptors) failed (hr=0x{:08X})",
                   heapDesc.NumDescriptors, uint32_t(hr));
            return false;
        }
        heap->SetName(L"NRD_SDK_Descriptors");
        heapIncrement = dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        constantStride = (std::max<uint32_t>(desc->constantBufferMaxDataSize, 1u) + 255u) & ~255u;
        constantsPerFrame = std::max<uint32_t>(limits.setsMaxNum, 1u);
        D3D12_HEAP_PROPERTIES upload{};
        upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = uint64_t(constantStride) * constantsPerFrame * kFrames;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc = {1, 0};
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = dx->GetDevice()->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constants));
        if (FAILED(hr)) {
            UZ_LOG(Renderer, Error, "NRD SDK: constant ring ({} bytes) failed (hr=0x{:08X})", bufferDesc.Width, uint32_t(hr));
            return false;
        }
        constants->SetName(L"NRD_SDK_Constants");
        const D3D12_RANGE noRead{0, 0};
        hr = constants->Map(0, &noRead, reinterpret_cast<void**>(&constantsMapped));
        if (FAILED(hr) || !constantsMapped) {
            UZ_LOG(Renderer, Error, "NRD SDK: mapping the constant ring failed (hr=0x{:08X})", uint32_t(hr));
            return false;
        }
        return true;
    }
    bool CreateAdapterPipelines() {
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants = {0, 0, sizeof(NrdSdkRoot) / sizeof(uint32_t)};
        static_assert(sizeof(NrdSdkRoot) / 4 <= 64);
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 1;
        rootDesc.pParameters = &parameter;
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
        ComPtr<ID3DBlob> serialized, errors;
        HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
        if (SUCCEEDED(hr))
            hr = dx->GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(),
                serialized->GetBufferSize(), IID_PPV_ARGS(&adapterRoot));
        if (FAILED(hr)) {
            UZ_LOG(Renderer, Error, "NRD SDK: adapter root signature failed (hr=0x{:08X})", uint32_t(hr));
            return false;
        }
        ShaderCompiler compiler;
        if (!compiler.Init()) return false;
        const std::array<std::pair<EngineShader, ComPtr<ID3D12PipelineState>*>, 2> shaders = {{
            {EngineShader::NrdSdkPackCS, &packPso}, {EngineShader::NrdSdkModulateCS, &modulatePso}}};
        for (const auto& [shader, pso] : shaders) {
            ShaderCompileDesc compile{};
            compile.source = EngineShaders::GetSource(shader);
            compile.sourceName = EngineShaders::GetName(shader);
            compile.stage = ShaderStageCompile::Compute;
            compile.model = ShaderModelCompile::SM6_6;
            auto result = compiler.Compile(compile);
            if (!result.success) {
                UZ_LOG(Renderer, Error, "NRD SDK: compile failed for '{}': {}", compile.sourceName, result.errors);
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = adapterRoot.Get();
            psoDesc.CS = {result.bytecode.data(), result.bytecode.size()};
            hr = dx->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(pso->GetAddressOf()));
            if (FAILED(hr)) {
                UZ_LOG(Renderer, Error, "NRD SDK: PSO for '{}' failed (hr=0x{:08X})", compile.sourceName, uint32_t(hr));
                return false;
            }
            UZ_LOG(Renderer, Info, "NRD SDK: '{}' compiled cs_6_6; PSO ready", compile.sourceName);
        }
        return true;
    }
    void ReleaseAll() {
        if (device) device->WaitIdle();
        if (instance && api) api->DestroyInstance(*instance);
        instance = nullptr;
        desc = nullptr;
        if (constants && constantsMapped) constants->Unmap(0, nullptr);
        constantsMapped = nullptr;
        constants.Reset();
        heap.Reset();
        pipelines.clear();
        packPso.Reset();
        modulatePso.Reset();
        adapterRoot.Reset();
        pool.clear();
        poolResources.clear();
        if (device) {
            for (auto* handle : {&motion, &normalRoughness, &viewZ, &diffuseInput, &specularInput,
                                 &diffuseOutput, &specularOutput}) {
                if (handle->IsValid()) device->DestroyTexture(*handle);
                *handle = {};
            }
        }
        width = height = 0;
        sdkFrameIndex = 0;
        framesSinceReset = 0;
        clearPending = true;
        havePrev = false;
    }
    ~Impl() { ReleaseAll(); }

    // Executes every nrd::DispatchDesc of this frame on the native command list.
    // Inputs arrive in the render graph's ShaderResource state and outputs in
    // UnorderedAccess; both are restored before returning so the graph's
    // state tracking stays correct. Pool states persist across frames.
    void Execute(IRHICommandList* cmd, const NrdInputs& in, const nrd::DispatchDesc* dispatches,
                 uint32_t dispatchCount) {
        auto* cmdList = static_cast<DX12CommandList*>(cmd);
        auto* native = cmdList->GetNative();
        std::array<Tracked, size_t(nrd::ResourceType::MAX_NUM)> users{};
        users[size_t(nrd::ResourceType::IN_MV)] = UserTracked(motion, kSrvState);
        users[size_t(nrd::ResourceType::IN_NORMAL_ROUGHNESS)] = UserTracked(normalRoughness, kSrvState);
        users[size_t(nrd::ResourceType::IN_VIEWZ)] = UserTracked(viewZ, kSrvState);
        users[size_t(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST)] = UserTracked(diffuseInput, kSrvState);
        users[size_t(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST)] = UserTracked(specularInput, kSrvState);
        users[size_t(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST)] =
            UserTracked(diffuseOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        users[size_t(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST)] =
            UserTracked(specularOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto initialUsers = users;

        frameSlot = (frameSlot + 1) % kFrames;
        descriptorCursor = 0;
        constantCursor = 0;
        D3D12_GPU_VIRTUAL_ADDRESS lastConstants = 0;
        ID3D12DescriptorHeap* heaps[] = {heap.Get()};
        native->SetDescriptorHeaps(1, heaps);
        const D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = uz::dx12::GetCPUStart(heap.Get());
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = uz::dx12::GetGPUStart(heap.Get());
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        uint32_t executed = 0;
        for (uint32_t d = 0; d < dispatchCount; ++d) {
            const nrd::DispatchDesc& dispatch = dispatches[d];
            const nrd::PipelineDesc& pipelineDesc = desc->pipelines[dispatch.pipelineIndex];
            const Pipeline& pipeline = pipelines[dispatch.pipelineIndex];
            if (dispatch.gridWidth == 0 || dispatch.gridHeight == 0) continue;
            if (descriptorCursor + dispatch.resourcesNum > descriptorsPerFrame
                || constantCursor >= constantsPerFrame) {
                if (!loggedResourceError)
                    UZ_LOG(Renderer, Error, "NRD SDK: per-frame descriptor/constant ring exhausted at dispatch {} ('{}')",
                           d, dispatch.name ? dispatch.name : "");
                loggedResourceError = true;
                break;
            }
            barriers.clear();
            const uint32_t first = frameSlot * descriptorsPerFrame + descriptorCursor;
            D3D12_GPU_DESCRIPTOR_HANDLE tables[2]{};
            uint32_t n = 0;
            bool resolved = true;
            for (uint32_t r = 0; r < pipelineDesc.resourceRangesNum && resolved; ++r) {
                const nrd::ResourceRangeDesc& range = pipelineDesc.resourceRanges[r];
                const bool storage = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
                tables[r].ptr = gpuBase.ptr + uint64_t(first + n) * heapIncrement;
                for (uint32_t j = 0; j < range.descriptorsNum; ++j, ++n) {
                    const nrd::ResourceDesc& resource = dispatch.resources[n];
                    Tracked* tracked = nullptr;
                    if (resource.type == nrd::ResourceType::PERMANENT_POOL)
                        tracked = &pool[resource.indexInPool];
                    else if (resource.type == nrd::ResourceType::TRANSIENT_POOL)
                        tracked = &pool[desc->permanentPoolSize + resource.indexInPool];
                    else
                        tracked = &users[size_t(resource.type)];
                    if (!tracked->resource) {
                        if (!loggedResourceError)
                            UZ_LOG(Renderer, Error, "NRD SDK: dispatch '{}' needs unsupported resource {}",
                                   dispatch.name ? dispatch.name : "", api->GetResourceTypeString(resource.type));
                        loggedResourceError = true;
                        resolved = false;
                        break;
                    }
                    const D3D12_RESOURCE_STATES wanted = storage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : kSrvState;
                    D3D12_RESOURCE_BARRIER barrier{};
                    if (tracked->state != wanted) {
                        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        barrier.Transition.pResource = tracked->resource;
                        barrier.Transition.StateBefore = tracked->state;
                        barrier.Transition.StateAfter = wanted;
                        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        barriers.push_back(barrier);
                    } else if (storage) {
                        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                        barrier.UAV.pResource = tracked->resource;
                        barriers.push_back(barrier);
                    }
                    tracked->state = wanted;
                    D3D12_CPU_DESCRIPTOR_HANDLE cpu{cpuBase.ptr + SIZE_T(first + n) * heapIncrement};
                    if (storage) {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                        uav.Format = tracked->format;
                        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                        dx->GetDevice()->CreateUnorderedAccessView(tracked->resource, nullptr, &uav, cpu);
                    } else {
                        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                        srv.Format = tracked->format;
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        srv.Texture2D.MipLevels = 1;
                        dx->GetDevice()->CreateShaderResourceView(tracked->resource, &srv, cpu);
                    }
                }
            }
            if (!resolved) break;
            descriptorCursor += n;
            if (!barriers.empty()) native->ResourceBarrier(uint32_t(barriers.size()), barriers.data());
            if (dispatch.constantBufferDataSize != 0
                && (!dispatch.constantBufferDataMatchesPreviousDispatch || lastConstants == 0)) {
                const uint32_t slot = frameSlot * constantsPerFrame + constantCursor++;
                std::memcpy(constantsMapped + size_t(slot) * constantStride, dispatch.constantBufferData,
                            std::min<uint32_t>(dispatch.constantBufferDataSize, constantStride));
                lastConstants = constants->GetGPUVirtualAddress() + uint64_t(slot) * constantStride;
            }
            native->SetComputeRootSignature(pipeline.root.Get());
            native->SetPipelineState(pipeline.pso.Get());
            if (pipelineDesc.hasConstantData && lastConstants != 0)
                native->SetComputeRootConstantBufferView(pipeline.constantsParam, lastConstants);
            for (uint32_t r = 0; r < pipelineDesc.resourceRangesNum; ++r) {
                const bool storage = pipelineDesc.resourceRanges[r].descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
                native->SetComputeRootDescriptorTable(storage ? pipeline.storagesParam : pipeline.texturesParam, tables[r]);
            }
            native->Dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
            ++executed;
        }
        // Restore the render-graph states of the user textures.
        barriers.clear();
        for (size_t i = 0; i < users.size(); ++i) {
            if (!users[i].resource || users[i].state == initialUsers[i].state) continue;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = users[i].resource;
            barrier.Transition.StateBefore = users[i].state;
            barrier.Transition.StateAfter = initialUsers[i].state;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers.push_back(barrier);
        }
        if (!barriers.empty()) native->ResourceBarrier(uint32_t(barriers.size()), barriers.data());
        cmdList->RebindBindlessHeaps();
        native->SetComputeRootSignature(dx->GetRootSignature());
        if (in.frameIndex < 3 || in.frameIndex % 32 == 0 || in.reset || clearPending)
            UZ_LOG(Renderer, Info, "NRD SDK: frame={} nrdFrame={} dispatches={}/{} descriptors={} constants={} reset={}",
                   in.frameIndex, sdkFrameIndex, executed, dispatchCount, descriptorCursor, constantCursor, in.reset);
    }
};
#else
struct NrdSdkDenoiser::Impl {
    uint32_t width = 0, height = 0;
};
#endif

NrdSdkDenoiser::NrdSdkDenoiser() : m_impl(std::make_unique<Impl>()) {}
NrdSdkDenoiser::~NrdSdkDenoiser() = default;
uint32_t NrdSdkDenoiser::Width() const { return m_impl->width; }
uint32_t NrdSdkDenoiser::Height() const { return m_impl->height; }

bool NrdSdkDenoiser::IsCompiledIn() {
#ifdef UZ_NRD_SDK_COMPILED
    return true;
#else
    return false;
#endif
}

bool NrdSdkDenoiser::Init(IRHIDevice* device, uint32_t width, uint32_t height) {
#ifdef UZ_NRD_SDK_COMPILED
    auto& p = *m_impl;
    p.ReleaseAll();
    p.device = device;
    p.dx = static_cast<DX12Device*>(device);
    p.width = width;
    p.height = height;
    p.api = LoadNrdApi();
    if (!p.api) return false;
    const nrd::LibraryDesc* library = p.api->GetLibraryDesc();
    if (!library || library->versionMajor != NRD_VERSION_MAJOR || library->versionMinor != NRD_VERSION_MINOR) {
        UZ_LOG(Renderer, Error, "NRD SDK: NRD.dll is v{}.{}.{} but the engine was built against v{}.{}; rebuild with scripts/build_nrd.ps1",
               library ? library->versionMajor : 0, library ? library->versionMinor : 0,
               library ? library->versionBuild : 0, NRD_VERSION_MAJOR, NRD_VERSION_MINOR);
        return false;
    }
    if (library->normalEncoding != nrd::NormalEncoding::RGBA16_SNORM
        || library->roughnessEncoding != nrd::RoughnessEncoding::LINEAR) {
        UZ_LOG(Renderer, Error, "NRD SDK: NRD.dll uses normal encoding {} / roughness encoding {}; nrd_sdk_pack_cs.hlsl packs RGBA16_SNORM (4) / LINEAR (1) - rebuild with scripts/build_nrd.ps1",
               uint32_t(library->normalEncoding), uint32_t(library->roughnessEncoding));
        return false;
    }
    const std::array<nrd::DenoiserDesc, 2> denoisers = {{
        {kDiffuseId, nrd::Denoiser::REBLUR_DIFFUSE}, {kSpecularId, nrd::Denoiser::RELAX_SPECULAR}}};
    nrd::InstanceCreationDesc creation{};
    creation.denoisers = denoisers.data();
    creation.denoisersNum = uint32_t(denoisers.size());
    const nrd::Result created = p.api->CreateInstance(creation, p.instance);
    if (created != nrd::Result::SUCCESS || !p.instance) {
        UZ_LOG(Renderer, Error, "NRD SDK: nrd::CreateInstance failed ({})", uint32_t(created));
        p.instance = nullptr;
        return false;
    }
    p.desc = p.api->GetInstanceDesc(*p.instance);
    if (!p.desc) {
        UZ_LOG(Renderer, Error, "NRD SDK: nrd::GetInstanceDesc returned null");
        p.ReleaseAll();
        return false;
    }
    p.motion = p.CreateUserTexture(RHIFormat::RG16Float, "NRD_SDK_MotionVectors");
    p.normalRoughness = p.CreateUserTexture(RHIFormat::RGBA16Float, "NRD_SDK_NormalRoughness");
    p.viewZ = p.CreateUserTexture(RHIFormat::R32Float, "NRD_SDK_ViewZ");
    p.diffuseInput = p.CreateUserTexture(RHIFormat::RGBA16Float, "NRD_SDK_DiffuseInput");
    p.specularInput = p.CreateUserTexture(RHIFormat::RGBA16Float, "NRD_SDK_SpecularInput");
    p.diffuseOutput = p.CreateUserTexture(RHIFormat::RGBA16Float, "NRD_SDK_DiffuseOutput");
    p.specularOutput = p.CreateUserTexture(RHIFormat::RGBA16Float, "NRD_SDK_SpecularOutput");
    for (auto handle : {p.motion, p.normalRoughness, p.viewZ, p.diffuseInput, p.specularInput,
                        p.diffuseOutput, p.specularOutput}) {
        if (!handle.IsValid()) {
            UZ_LOG(Renderer, Error, "NRD SDK: creating a {}x{} input/output texture failed", width, height);
            p.ReleaseAll();
            return false;
        }
    }
    if (!p.CreatePools() || !p.CreatePipelines() || !p.CreateRings() || !p.CreateAdapterPipelines()) {
        p.ReleaseAll();
        return false;
    }
    UZ_LOG(Renderer, Info,
           "NRD SDK v{}.{}.{} initialized at {}x{}: {} ({}) + {} ({}); permanent pool {} textures, transient pool {} textures, {} pipelines, {} dispatch sets max, constant buffer {} bytes max, {} descriptors/frame",
           library->versionMajor, library->versionMinor, library->versionBuild, width, height,
           p.api->GetDenoiserString(nrd::Denoiser::REBLUR_DIFFUSE), kDiffuseId,
           p.api->GetDenoiserString(nrd::Denoiser::RELAX_SPECULAR), kSpecularId,
           p.desc->permanentPoolSize, p.desc->transientPoolSize, p.desc->pipelinesNum,
           p.desc->descriptorPoolDesc.setsMaxNum, p.desc->constantBufferMaxDataSize, p.descriptorsPerFrame);
    return true;
#else
    (void)device; (void)width; (void)height;
    UZ_LOG(Renderer, Warning, "NRD SDK: this build has no NRD headers (runtime/thirdparty/nrd missing at build time); run scripts/setup.ps1 and rebuild");
    return false;
#endif
}

void NrdSdkDenoiser::Destroy() {
#ifdef UZ_NRD_SDK_COMPILED
    m_impl->ReleaseAll();
#endif
}

rg::RgResourceId NrdSdkDenoiser::Denoise(rg::RgBuilder& builder, const NrdInputs& in) {
#ifdef UZ_NRD_SDK_COMPILED
    auto& p = *m_impl;
    if (!p.instance || in.width != p.width || in.height != p.height) return {};
    const auto motion = builder.ImportTrackedTexture("NRD_SDK_MotionVectors", p.motion);
    const auto normalRoughness = builder.ImportTrackedTexture("NRD_SDK_NormalRoughness", p.normalRoughness);
    const auto viewZ = builder.ImportTrackedTexture("NRD_SDK_ViewZ", p.viewZ);
    const auto diffuseInput = builder.ImportTrackedTexture("NRD_SDK_DiffuseInput", p.diffuseInput);
    const auto specularInput = builder.ImportTrackedTexture("NRD_SDK_SpecularInput", p.specularInput);
    const auto diffuseOutput = builder.ImportTrackedTexture("NRD_SDK_DiffuseOutput", p.diffuseOutput);
    const auto specularOutput = builder.ImportTrackedTexture("NRD_SDK_SpecularOutput", p.specularOutput);
    rg::RgTextureDesc resolvedDesc{};
    resolvedDesc.width = in.width;
    resolvedDesc.height = in.height;
    resolvedDesc.format = RHIFormat::RGBA16Float;
    resolvedDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    const auto resolved = builder.CreateTexture("NRD_SDK_Resolved", resolvedDesc);

    const float unitsPerMeter = cv_NrdSdkUnitsPerMeter->value;
    NrdSdkRoot shared{};
    shared.width = in.width;
    shared.height = in.height;
    shared.hitDistA = 3.0f * unitsPerMeter; // nrd::ReblurHitDistanceParameters defaults are in meters
    shared.hitDistB = 0.1f;
    shared.hitDistC = 20.0f;
    const float denoisingRange = in.farPlane > 0.f ? in.farPlane : 500000.0f * unitsPerMeter;
    shared.skyViewZ = denoisingRange * 4.0f;
    shared.worldToView = in.view;
    const auto* frame = static_cast<const PerFrameCB*>(in.perFrame.cpuPtr);
    shared.inverseViewProj = frame->invViewProj;
    shared.debugSignal = uint32_t(cv_NrdSdkDebug->value);

    builder.AddPass("NRD_SDK_Pack", [=, this](rg::RgPassBuilder& pb) {
        pb.Read(in.diffuse, RHIResourceState::ShaderResource);
        pb.Read(in.albedo, RHIResourceState::ShaderResource);
        pb.Read(in.specular, RHIResourceState::ShaderResource);
        pb.Read(in.modulation, RHIResourceState::ShaderResource);
        pb.Read(in.virtualPosition, RHIResourceState::ShaderResource);
        pb.Read(in.pathGuide, RHIResourceState::ShaderResource);
        pb.Read(in.velocity, RHIResourceState::ShaderResource);
        pb.Write(motion, RHIResourceState::UnorderedAccess);
        pb.Write(normalRoughness, RHIResourceState::UnorderedAccess);
        pb.Write(viewZ, RHIResourceState::UnorderedAccess);
        pb.Write(diffuseInput, RHIResourceState::UnorderedAccess);
        pb.Write(specularInput, RHIResourceState::UnorderedAccess);
        pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto& state = *m_impl;
            auto srv = [&](rg::RgResourceId id) { return state.device->GetTextureSRVIndex(res.GetTexture(id)); };
            auto uav = [&](rg::RgResourceId id) { return state.device->GetTextureUAVIndex(res.GetTexture(id)); };
            NrdSdkRoot constants = shared;
            constants.diffuseSrv = srv(in.diffuse);
            constants.albedoSrv = srv(in.albedo);
            constants.specularSrv = srv(in.specular);
            constants.modulationSrv = srv(in.modulation);
            constants.virtualPositionSrv = srv(in.virtualPosition);
            constants.guideSrv = srv(in.pathGuide);
            constants.velocitySrv = srv(in.velocity);
            constants.motionUav = uav(motion);
            constants.normalRoughnessUav = uav(normalRoughness);
            constants.viewZUav = uav(viewZ);
            constants.diffuseInputUav = uav(diffuseInput);
            constants.specularInputUav = uav(specularInput);
            auto* native = static_cast<DX12CommandList*>(cmd)->GetNative();
            native->SetComputeRootSignature(state.adapterRoot.Get());
            native->SetComputeRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
            native->SetPipelineState(state.packPso.Get());
            native->Dispatch((in.width + 7) / 8, (in.height + 7) / 8, 1);
            native->SetComputeRootSignature(state.dx->GetRootSignature());
        });
    });

    builder.AddPass("NRD_SDK_Denoise", [=, this](rg::RgPassBuilder& pb) {
        pb.Read(motion, RHIResourceState::ShaderResource);
        pb.Read(normalRoughness, RHIResourceState::ShaderResource);
        pb.Read(viewZ, RHIResourceState::ShaderResource);
        pb.Read(diffuseInput, RHIResourceState::ShaderResource);
        pb.Read(specularInput, RHIResourceState::ShaderResource);
        pb.Write(diffuseOutput, RHIResourceState::UnorderedAccess);
        pb.Write(specularOutput, RHIResourceState::UnorderedAccess);
        pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources&) {
            auto& state = *m_impl;
            if (!state.havePrev) {
                state.prevView = in.view;
                state.prevProj = in.proj;
                state.havePrev = true;
            }
            nrd::CommonSettings common{};
            // Mat4 is row-major storage with row vectors; NRD wants column-major
            // storage with column vectors: the same 16 floats in memory.
            std::memcpy(common.viewToClipMatrix, in.proj.data(), sizeof(common.viewToClipMatrix));
            std::memcpy(common.viewToClipMatrixPrev, state.prevProj.data(), sizeof(common.viewToClipMatrixPrev));
            std::memcpy(common.worldToViewMatrix, in.view.data(), sizeof(common.worldToViewMatrix));
            std::memcpy(common.worldToViewMatrixPrev, state.prevView.data(), sizeof(common.worldToViewMatrixPrev));
            common.motionVectorScale[0] = 1.0f / float(in.width);
            common.motionVectorScale[1] = 1.0f / float(in.height);
            common.motionVectorScale[2] = 0.0f;
            common.resourceSize[0] = common.resourceSizePrev[0] = common.rectSize[0] = common.rectSizePrev[0] = uint16_t(in.width);
            common.resourceSize[1] = common.resourceSizePrev[1] = common.rectSize[1] = common.rectSizePrev[1] = uint16_t(in.height);
            common.denoisingRange = denoisingRange;
            common.disocclusionThreshold = 0.01f;
            common.splitScreen = cv_NrdSdkSplitScreen->value;
            common.frameIndex = state.sdkFrameIndex++;
            common.accumulationMode = state.clearPending ? nrd::AccumulationMode::CLEAR_AND_RESTART
                                    : in.reset ? nrd::AccumulationMode::RESTART : nrd::AccumulationMode::CONTINUE;
            state.framesSinceReset = (in.reset || state.clearPending) ? 0u : state.framesSinceReset + 1u;
            // Right after a reset the output is a 1-spp frame plus NRD's history fix, where
            // fireflies dominate (measured: reset frame MSE 106 / +68% mean without
            // suppression, 21 / +2% with it); once history exists suppression only costs energy.
            const bool shortHistory = state.framesSinceReset < uint32_t(cv_NrdSdkAntiFireflyFrames->value);
            common.isMotionVectorInWorldSpace = false;
            nrd::Result result = state.api->SetCommonSettings(*state.instance, common);
            if (result != nrd::Result::SUCCESS)
                UZ_LOG(Renderer, Error, "NRD SDK: SetCommonSettings failed ({})", uint32_t(result));
            nrd::ReblurSettings reblur{};
            reblur.hitDistanceParameters.A = shared.hitDistA;
            reblur.hitDistanceParameters.B = shared.hitDistB;
            reblur.hitDistanceParameters.C = shared.hitDistC;
            reblur.maxAccumulatedFrameNum = uint32_t(cv_NrdSdkDiffuseFrames->value);
            reblur.maxFastAccumulatedFrameNum = std::min<uint32_t>(uint32_t(cv_NrdSdkFastFrames->value),
                                                                   reblur.maxAccumulatedFrameNum);
            reblur.fastHistoryClampingSigmaScale = cv_NrdSdkClampSigma->value;
            reblur.enableAntiFirefly = shortHistory || cv_NrdSdkAntiFirefly->value >= 0.5f;
            reblur.diffusePrepassBlurRadius = cv_NrdSdkDiffusePrepass->value;
            result = state.api->SetDenoiserSettings(*state.instance, kDiffuseId, &reblur);
            if (result != nrd::Result::SUCCESS)
                UZ_LOG(Renderer, Error, "NRD SDK: SetDenoiserSettings(REBLUR_DIFFUSE) failed ({})", uint32_t(result));
            nrd::RelaxSettings relax{};
            relax.specularMaxAccumulatedFrameNum = uint32_t(cv_NrdSdkSpecularFrames->value);
            relax.specularMaxFastAccumulatedFrameNum = std::min<uint32_t>(uint32_t(cv_NrdSdkFastFrames->value),
                                                                          relax.specularMaxAccumulatedFrameNum);
            relax.fastHistoryClampingSigmaScale = cv_NrdSdkClampSigma->value;
            relax.specularPrepassBlurRadius = cv_NrdSdkSpecularPrepass->value;
            relax.specularPhiLuminance = cv_NrdSdkSpecularPhi->value;
            relax.specularMinLuminanceWeight = cv_NrdSdkSpecularMinLum->value;
            result = state.api->SetDenoiserSettings(*state.instance, kSpecularId, &relax);
            if (result != nrd::Result::SUCCESS)
                UZ_LOG(Renderer, Error, "NRD SDK: SetDenoiserSettings(RELAX_SPECULAR) failed ({})", uint32_t(result));
            const std::array<nrd::Identifier, 2> identifiers = {kDiffuseId, kSpecularId};
            const nrd::DispatchDesc* dispatches = nullptr;
            uint32_t dispatchCount = 0;
            result = state.api->GetComputeDispatches(*state.instance, identifiers.data(),
                uint32_t(identifiers.size()), dispatches, dispatchCount);
            if (result != nrd::Result::SUCCESS || !dispatches) {
                UZ_LOG(Renderer, Error, "NRD SDK: GetComputeDispatches failed ({})", uint32_t(result));
                dispatchCount = 0;
            }
            state.Execute(cmd, in, dispatches, dispatchCount);
            state.clearPending = false;
            state.prevView = in.view;
            state.prevProj = in.proj;
        });
    });

    builder.AddPass("NRD_SDK_Modulate", [=, this](rg::RgPassBuilder& pb) {
        pb.Read(in.raw, RHIResourceState::ShaderResource);
        pb.Read(in.diffuse, RHIResourceState::ShaderResource);
        pb.Read(in.albedo, RHIResourceState::ShaderResource);
        pb.Read(in.specular, RHIResourceState::ShaderResource);
        pb.Read(in.modulation, RHIResourceState::ShaderResource);
        pb.Read(in.pathGuide, RHIResourceState::ShaderResource);
        pb.Read(diffuseOutput, RHIResourceState::ShaderResource);
        pb.Read(specularOutput, RHIResourceState::ShaderResource);
        pb.Write(resolved, RHIResourceState::UnorderedAccess);
        pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto& state = *m_impl;
            auto srv = [&](rg::RgResourceId id) { return state.device->GetTextureSRVIndex(res.GetTexture(id)); };
            NrdSdkRoot constants = shared;
            constants.rawSrv = srv(in.raw);
            constants.diffuseSrv = srv(in.diffuse);
            constants.albedoSrv = srv(in.albedo);
            constants.specularSrv = srv(in.specular);
            constants.modulationSrv = srv(in.modulation);
            constants.guideSrv = srv(in.pathGuide);
            constants.diffuseOutputSrv = srv(diffuseOutput);
            constants.specularOutputSrv = srv(specularOutput);
            constants.outputUav = state.device->GetTextureUAVIndex(res.GetTexture(resolved));
            auto* native = static_cast<DX12CommandList*>(cmd)->GetNative();
            native->SetComputeRootSignature(state.adapterRoot.Get());
            native->SetComputeRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
            native->SetPipelineState(state.modulatePso.Get());
            native->Dispatch((in.width + 7) / 8, (in.height + 7) / 8, 1);
            native->SetComputeRootSignature(state.dx->GetRootSignature());
        });
    });
    return resolved;
#else
    (void)builder; (void)in;
    return {};
#endif
}
} // namespace uz
