#include "pipeline_path_tracer_pass.h"

#include "draw_list.h"
#include "gpu_mesh.h"
#include "vertex.h"
#include "raytracing/rt_manager.h"
#include "render_graph_builder.h"
#include "rhi.h"
#include "log.h"
#include "world.h"
#include "light_component.h"
#include "scene_component.h"
#include "color_utils.h"
#include "cvar.h"
#include "path_tracer_pass.h"
#include "light_bvh.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include "engine/rhi/dx12/dx12_device.h"
#include "engine/rhi/dx12/dx12_command_list.h"
#include "engine_shaders.h"
#include "shader_compiler.h"
#endif

UZ_DECLARE_LOG_CATEGORY(Renderer);

static auto* cv_RestirDI = UZ_CVAR_BOOL("r.ReSTIR.DI", false,
    "Opt-in primary-hit ReSTIR DI in the DXR path tracer");
static auto* cv_RestirTemporal = UZ_CVAR_BOOL("r.ReSTIR.Temporal", true,
    "Reuse validated previous-frame direct-light reservoirs");
static auto* cv_RestirSpatial = UZ_CVAR_BOOL("r.ReSTIR.Spatial", true,
    "Reuse four spatial neighbors with source-target MIS correction");
static auto* cv_RestirReadback = UZ_CVAR_BOOL("r.ReSTIR.Readback", false,
    "Diagnostic reservoir GPU readback (synchronizes the GPU)");
static auto* cv_GIRestir = UZ_CVAR_BOOL("r.PathTracer.GIReSTIR", false,
    "Opt-in one-bounce indirect ReSTIR (secondary-vertex reservoir reuse) in the DXR path tracer, read at path-tracer init; needs NEESamples 1, no NRD/volume/layered");
static auto* cv_GIRestirTemporal = UZ_CVAR_BOOL("r.PathTracer.GIReSTIR.Temporal", true,
    "Reuse validated previous-frame one-bounce reservoirs (a recursive average of the pixel's own samples: lower per-frame noise, no accumulated-mean gain)");
static auto* cv_GIRestirSpatial = UZ_CVAR_BOOL("r.PathTracer.GIReSTIR.Spatial", true,
    "Reuse four spatial one-bounce neighbors with source-target MIS correction (the accumulated-variance win)");
static auto* cv_NeeSamples = UZ_CVAR_INT("r.PathTracer.NEESamples", 4,
    "Directional visibility samples per vertex (1 for equal-ray ReSTIR comparison)", 1, 32);
static auto* cv_FrostbiteDiffuse = UZ_CVAR_BOOL("r.PathTracer.FrostbiteDiffuse", false,
    "Opt-in raster-matched diffuse lobe (EvalFrostbiteDiffuse) instead of the energy-compensated Lambert, read at path-tracer init, so the reference differs from raster in transport only");
static auto* cv_Nrd = UZ_CVAR_BOOL("r.NRD", false,
    "Opt-in diffuse/specular temporal/a-trous denoiser for the DXR path tracer");
static auto* cv_HairMultiScatter = UZ_CVAR_BOOL("r.Hair.MultiScatter", false,
    "Opt-in biased dual-scattering tail replacement for aligned hair under distant illumination");
static auto* cv_HairExplicitBounces = UZ_CVAR_INT("r.Hair.ExplicitBounces", 20,
    "Consecutive hair vertices traced before replacing the tail (larger reduces closure bias)", 1, 127);
static auto* cv_HairMaxBounces = UZ_CVAR_INT("r.Hair.MaxBounces", 0,
    "Opt-in hair scene depth including the explicit reference (0 uses ordinary depth, at least K+1 when dual is on)", 0, 128);
static auto* cv_HairDensity = UZ_CVAR_FLOAT("r.Hair.Density", 0.7f,
    "Dual-scattering local backscatter density (Zinke 2008)", 0.f, 1.f);
static auto* cv_Layered = UZ_CVAR_BOOL("r.PathTracer.LayeredMaterial", false,
    "Opt-in DXR dielectric coat over the unchanged base BRDF");
static auto* cv_CoatWeight = UZ_CVAR_FLOAT("r.PathTracer.Coat.Weight", 1.f,
    "Global stage-1 coat coverage; zero exactly preserves base transport", 0.f, 1.f);
static auto* cv_CoatRoughness = UZ_CVAR_FLOAT("r.PathTracer.Coat.Roughness", 0.1f,
    "Global coat perceptual GGX roughness (engine alpha floor applies)", 0.f, 1.f);
static auto* cv_CoatIOR = UZ_CVAR_FLOAT("r.PathTracer.Coat.IOR", 1.5f,
    "Global coat index of refraction", 1.f, 3.f);
static auto* cv_CoatThickness = UZ_CVAR_FLOAT("r.PathTracer.Coat.Thickness", 1.f,
    "Normal-incidence round-trip coat optical distance in cm (Snell approximation)", 0.f, 1e6f);
static auto* cv_CoatSigmaAR = UZ_CVAR_FLOAT("r.PathTracer.Coat.SigmaAR", 0.f,
    "Coat red absorption coefficient per cm; zero preserves stage 1", 0.f, 1e6f);
static auto* cv_CoatSigmaAG = UZ_CVAR_FLOAT("r.PathTracer.Coat.SigmaAG", 0.f,
    "Coat green absorption coefficient per cm", 0.f, 1e6f);
static auto* cv_CoatSigmaAB = UZ_CVAR_FLOAT("r.PathTracer.Coat.SigmaAB", 0.f,
    "Coat blue absorption coefficient per cm", 0.f, 1e6f);
static auto* cv_StackDepth = UZ_CVAR_INT("r.PathTracer.Stack.Depth", 0,
    "Opt-in refracted stack: 0/1 preserves legacy coat, 2..4 adjacent dielectric interfaces", 0, 4);
static uz::CVar* cv_StackRoughness[] = {
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer2.Roughness", .2f, "Second interface GGX roughness", 0.f, 1.f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer3.Roughness", .3f, "Third interface GGX roughness", 0.f, 1.f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer4.Roughness", .4f, "Fourth interface GGX roughness", 0.f, 1.f)};
static uz::CVar* cv_StackIOR[] = {
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer2.IOR", 1.33f, "Second medium IOR (not air-relative F0)", 1.f, 3.f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer3.IOR", 1.5f, "Third medium IOR", 1.f, 3.f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer4.IOR", 1.33f, "Fourth medium IOR", 1.f, 3.f)};
static uz::CVar* cv_StackThickness[] = {
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer2.Thickness", 1.f, "Second film round-trip normal distance in cm", 0.f, 1e6f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer3.Thickness", 1.f, "Third film round-trip normal distance in cm", 0.f, 1e6f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer4.Thickness", 1.f, "Fourth film round-trip normal distance in cm", 0.f, 1e6f)};
static uz::CVar* cv_StackSigmaAR[] = {
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer2.SigmaAR", 0.f, "Second film red absorption per cm", 0.f, 1e6f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer3.SigmaAR", 0.f, "Third film red absorption per cm", 0.f, 1e6f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer4.SigmaAR", 0.f, "Fourth film red absorption per cm", 0.f, 1e6f)};
static uz::CVar* cv_StackSigmaAG[] = {
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer2.SigmaAG", 0.f, "Second film green absorption per cm", 0.f, 1e6f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer3.SigmaAG", 0.f, "Third film green absorption per cm", 0.f, 1e6f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer4.SigmaAG", 0.f, "Fourth film green absorption per cm", 0.f, 1e6f)};
static uz::CVar* cv_StackSigmaAB[] = {
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer2.SigmaAB", 0.f, "Second film blue absorption per cm", 0.f, 1e6f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer3.SigmaAB", 0.f, "Third film blue absorption per cm", 0.f, 1e6f),
    UZ_CVAR_FLOAT("r.PathTracer.Stack.Layer4.SigmaAB", 0.f, "Fourth film blue absorption per cm", 0.f, 1e6f)};
static auto* cv_Volume = UZ_CVAR_BOOL("r.PathTracer.Volume", false,
    "Opt-in spherical participating medium in DXR transport");
static auto* cv_VolumeVariation = UZ_CVAR_FLOAT("r.PathTracer.Volume.Variation", 0.f,
    "Linear density variation along normalized (X+Y); zero is exactly homogeneous", 0.f, 1.f);
static auto* cv_VolumeA = UZ_CVAR_FLOAT("r.PathTracer.Volume.SigmaA", 0.001f,
    "Homogeneous absorption per centimeter", 0.f, 10.f);
static auto* cv_VolumeS = UZ_CVAR_FLOAT("r.PathTracer.Volume.SigmaS", 0.003f,
    "Homogeneous scattering per centimeter", 0.f, 10.f);
static auto* cv_VolumeG = UZ_CVAR_FLOAT("r.PathTracer.Volume.G", 0.f,
    "Henyey-Greenstein anisotropy", -0.99f, 0.99f);
static auto* cv_VolumeRadius = UZ_CVAR_FLOAT("r.PathTracer.Volume.Radius", 500.f,
    "Homogeneous sphere radius in centimeters", 0.f, 1e6f);
static auto* cv_VolumeX = UZ_CVAR_FLOAT("r.PathTracer.Volume.CenterX", 0.f,
    "Medium sphere center X", -1e6f, 1e6f);
static auto* cv_VolumeY = UZ_CVAR_FLOAT("r.PathTracer.Volume.CenterY", 0.f,
    "Medium sphere center Y", -1e6f, 1e6f);
static auto* cv_VolumeZ = UZ_CVAR_FLOAT("r.PathTracer.Volume.CenterZ", 150.f,
    "Medium sphere center Z", -1e6f, 1e6f);

namespace uz {
struct PipelinePathTracerPass::Impl {
    IRHIDevice* device = nullptr;
    RHIBufferHandle sbt;
    std::array<RHIBufferHandle, 3> materials{};
    std::array<uint32_t, 3> capacity{};
    std::array<RHIBufferHandle, 3> lights{};
    std::array<uint32_t, 3> lightCapacity{};
    LightBVH lightBVH;
    std::unique_ptr<NrdDenoiser> denoiser;
    std::array<RHIBufferHandle, 3> bvhNodes{}, bvhLights{}, bvhConfigs{};
    std::array<uint32_t, 3> bvhNodeCapacity{}, bvhLightCapacity{}, bvhConfigCapacity{};
    bool bvhLogged = false;
#ifdef _WIN32
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12StateObject> pipeline;
    ComPtr<ID3D12PipelineState> restirInitialPSO, restirTemporalPSO, restirSpatialPSO;
    ComPtr<ID3D12PipelineState> giTemporalPSO, giSpatialPSO, giResolvePSO;
#endif
    bool ready = false;
    bool logged = false;
    bool giCompiled = false; // r.PathTracer.GIReSTIR at Init: the DXR libraries carry the GI code.
    bool frostbiteDiffuse = false; // r.PathTracer.FrostbiteDiffuse at Init: every PT shader uses the raster diffuse lobe.
    RHITextureHandle accumulation, compensation;
    uint32_t width = 0, height = 0, samples = 0, lastFrame = UINT32_MAX;
    uint32_t resetGeneration = 0;
    uint64_t previousSignature = 0;
    uint64_t previousDenoiserSignature = 0;
    RHIBufferHandle restirSurface, restirInitial;
    std::array<RHIBufferHandle, 2> reservoirs{};
    RHIBufferHandle giInitial;
    std::array<RHIBufferHandle, 2> giReservoirs{};
    RHIBufferHandle readback;
    bool readbackPending = false;
    uint32_t readbackFrame = 0;

    ~Impl() {
        if (!device) return;
        device->WaitIdle();
        for (auto buffer : materials)
            if (buffer.IsValid()) device->DestroyBuffer(buffer);
        for (auto buffer : lights)
            if (buffer.IsValid()) device->DestroyBuffer(buffer);
        for (auto buffers : {bvhNodes, bvhLights, bvhConfigs})
            for (auto buffer : buffers)
                if (buffer.IsValid()) device->DestroyBuffer(buffer);
        if (sbt.IsValid()) device->DestroyBuffer(sbt);
        if (accumulation.IsValid()) device->DestroyTexture(accumulation);
        if (compensation.IsValid()) device->DestroyTexture(compensation);
        if (restirSurface.IsValid()) device->DestroyBuffer(restirSurface);
        if (restirInitial.IsValid()) device->DestroyBuffer(restirInitial);
        for (auto buffer : reservoirs)
            if (buffer.IsValid()) device->DestroyBuffer(buffer);
        if (giInitial.IsValid()) device->DestroyBuffer(giInitial);
        for (auto buffer : giReservoirs)
            if (buffer.IsValid()) device->DestroyBuffer(buffer);
        if (readback.IsValid()) device->DestroyBuffer(readback);
    }
};

PipelinePathTracerPass::PipelinePathTracerPass() : m_impl(std::make_unique<Impl>()) {}
PipelinePathTracerPass::~PipelinePathTracerPass() = default;

bool PipelinePathTracerPass::Init(IRHIDevice* device) {
    auto& p = *m_impl;
    if (p.ready) return true;
    if (!device || device->GetBackend() != RHIBackend::DX12
        || !device->IsBindless() || !device->SupportsRayTracing()) {
        UZ_LOG(Renderer, Error, "PipelinePathTracer: --path-trace requires DX12, SM6.6 bindless and DXR");
        return false;
    }
#ifdef _WIN32
    p.device = device;
    auto* dx = static_cast<DX12Device*>(device);
    // This pass alone needs a register(t0) table. Do not extend the shared
    // graphics signature: its 24-DWORD root constants and all consumers stay unchanged.
    static_assert(kRootDwords <= 24);
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER params[7]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = {0, 0, kRootDwords};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    for (uint32_t i = 0; i < 2; ++i) {
        params[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2 + i].DescriptorTable.NumDescriptorRanges = 1;
        params[2 + i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[4].Constants = {2, 0, sizeof(NrdSignalRoot) / sizeof(uint32_t)};
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[5].Constants = {3, 0, sizeof(PathVolumeRoot) / sizeof(uint32_t)};
    // GI-ReSTIR gets its own b4 block; PathTracerRoot keeps its 24 DWORDs.
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[6].Constants = {4, 0, sizeof(RestirGIRoot) / sizeof(uint32_t)};
    static_assert(kRootDwords + 2 + 2 + sizeof(NrdSignalRoot)/4 + sizeof(PathVolumeRoot)/4
                  + sizeof(RestirGIRoot)/4 <= 64);
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 7;
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &serialized, &errors);
    if (SUCCEEDED(hr))
        hr = dx->GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(),
                serialized->GetBufferSize(), IID_PPV_ARGS(&p.root));
    if (FAILED(hr)) {
        UZ_LOG(Renderer, Error, "PipelinePathTracer: root signature failed hr=0x{:08X}", uint32_t(hr));
        return false;
    }

    ShaderCompiler compiler;
    if (!compiler.Init()) return false;
    // Like the GI code below: OFF preprocesses the committed text unchanged, so the
    // default transport is byte-identical; ON swaps EvaluatePBR's diffuse lobe everywhere.
    p.frostbiteDiffuse = cv_FrostbiteDiffuse->value >= 0.5f;
    if (p.frostbiteDiffuse)
        UZ_LOG(Renderer, Info, "PipelinePathTracer: r.PathTracer.FrostbiteDiffuse on: raster-matched Frostbite diffuse lobe");
    const EngineShader restirShaders[] = {
        EngineShader::RestirInitialCS, EngineShader::RestirTemporalCS, EngineShader::RestirSpatialCS};
    for (uint32_t i = 0; i < 3; ++i) {
        ShaderCompileDesc desc{};
        desc.source = EngineShaders::GetSource(restirShaders[i]);
        desc.sourceName = EngineShaders::GetName(restirShaders[i]);
        desc.stage = ShaderStageCompile::Compute;
        desc.model = ShaderModelCompile::SM6_6;
        if (p.frostbiteDiffuse) desc.defines.push_back("UZ_PT_FROSTBITE_DIFFUSE=1");
        auto result = compiler.Compile(desc);
        if (!result.success) {
            UZ_LOG(Renderer, Error, "ReSTIR DI: compile failed for '{}': {}", desc.sourceName, result.errors);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = p.root.Get();
        pd.CS = {result.bytecode.data(), result.bytecode.size()};
        auto** destination = i == 0 ? p.restirInitialPSO.GetAddressOf()
            : i == 1 ? p.restirTemporalPSO.GetAddressOf() : p.restirSpatialPSO.GetAddressOf();
        hr = dx->GetDevice()->CreateComputePipelineState(&pd, IID_PPV_ARGS(destination));
        if (FAILED(hr)) return false;
        UZ_LOG(Renderer, Info, "ReSTIR DI: '{}' compiled cs_6_6; PSO ready", desc.sourceName);
    }
    // Only an opted-in init compiles the GI code; the plain DXR libraries are
    // preprocessed from the committed text and produce byte-identical output.
    p.giCompiled = cv_GIRestir->value >= 0.5f;
    const EngineShader giShaders[] = {
        EngineShader::RestirGiTemporalCS, EngineShader::RestirGiSpatialCS, EngineShader::RestirGiResolveCS};
    for (uint32_t i = 0; i < 3 && p.giCompiled; ++i) {
        ShaderCompileDesc desc{};
        desc.source = EngineShaders::GetSource(giShaders[i]);
        desc.sourceName = EngineShaders::GetName(giShaders[i]);
        desc.stage = ShaderStageCompile::Compute;
        desc.model = ShaderModelCompile::SM6_6;
        if (p.frostbiteDiffuse) desc.defines.push_back("UZ_PT_FROSTBITE_DIFFUSE=1");
        auto result = compiler.Compile(desc);
        if (!result.success) {
            UZ_LOG(Renderer, Error, "GI-ReSTIR: compile failed for '{}': {}", desc.sourceName, result.errors);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = p.root.Get();
        pd.CS = {result.bytecode.data(), result.bytecode.size()};
        auto** destination = i == 0 ? p.giTemporalPSO.GetAddressOf()
            : i == 1 ? p.giSpatialPSO.GetAddressOf() : p.giResolvePSO.GetAddressOf();
        hr = dx->GetDevice()->CreateComputePipelineState(&pd, IID_PPV_ARGS(destination));
        if (FAILED(hr)) return false;
        UZ_LOG(Renderer, Info, "GI-ReSTIR: '{}' compiled cs_6_6; PSO ready", desc.sourceName);
    }
    const EngineShader shaders[] = {
        EngineShader::PathtracerRaygen, EngineShader::PathtracerMiss, EngineShader::PathtracerClosesthit
    };
    std::array<std::vector<uint8_t>, 3> bytecodes;
    for (uint32_t i = 0; i < 3; ++i) {
        ShaderCompileDesc desc{};
        desc.source = EngineShaders::GetSource(shaders[i]); // uzembed expands includes
        desc.sourceName = EngineShaders::GetName(shaders[i]);
        desc.stage = ShaderStageCompile::Library;
        desc.model = ShaderModelCompile::SM6_6;
        if (p.giCompiled) desc.defines.push_back("UZ_GI_RESTIR=1");
        if (p.frostbiteDiffuse) desc.defines.push_back("UZ_PT_FROSTBITE_DIFFUSE=1");
        auto result = compiler.Compile(desc);
        if (!result.success) {
            UZ_LOG(Renderer, Error, "PipelinePathTracer: compile failed for '{}': {}",
                   desc.sourceName, result.errors);
            return false;
        }
        bytecodes[i] = std::move(result.bytecode);
        UZ_LOG(Renderer, Info, "PipelinePathTracer: '{}' main compiled lib_6_6", desc.sourceName);
    }

    // Each source retains the prescribed 'main' entry; rename its export
    // when linking the three libraries into one state object.
    D3D12_EXPORT_DESC exports[4]{};
    exports[0].Name = L"PathRayGen"; exports[0].ExportToRename = L"main";
    exports[1].Name = L"PathMiss"; exports[1].ExportToRename = L"main";
    exports[2].Name = L"ShadowMiss";
    exports[3].Name = L"PathClosestHit"; exports[3].ExportToRename = L"main";
    D3D12_DXIL_LIBRARY_DESC libs[3]{};
    D3D12_STATE_SUBOBJECT objects[7]{};
    for (uint32_t i = 0; i < 3; ++i) {
        libs[i].DXILLibrary = {bytecodes[i].data(), bytecodes[i].size()};
        libs[i].NumExports = i == 1 ? 2 : 1;
        libs[i].pExports = &exports[i == 2 ? 3 : i];
        objects[i] = {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &libs[i]};
    }
    D3D12_HIT_GROUP_DESC hitGroup{};
    hitGroup.HitGroupExport = L"PathHitGroup";
    hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroup.ClosestHitShaderImport = L"PathClosestHit";
    objects[3] = {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroup};
    D3D12_RAYTRACING_SHADER_CONFIG config{kPayloadBytes, 8};
    objects[4] = {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &config};
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{kRecursionDepth};
    objects[5] = {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig};
    D3D12_GLOBAL_ROOT_SIGNATURE globalRoot{p.root.Get()};
    objects[6] = {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRoot};
    D3D12_STATE_OBJECT_DESC stateDesc{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, 7, objects};
    ComPtr<ID3D12Device5> device5;
    hr = dx->GetDevice()->QueryInterface(IID_PPV_ARGS(&device5));
    if (SUCCEEDED(hr)) hr = device5->CreateStateObject(&stateDesc, IID_PPV_ARGS(&p.pipeline));
    if (FAILED(hr)) {
        UZ_LOG(Renderer, Error, "PipelinePathTracer: CreateStateObject failed hr=0x{:08X}", uint32_t(hr));
        return false;
    }
    ComPtr<ID3D12StateObjectProperties> properties;
    hr = p.pipeline.As(&properties);
    if (FAILED(hr)) return false;
    RHIBufferDesc sbtDesc{};
    sbtDesc.sizeBytes = 192;
    sbtDesc.usage = RHIBufferUsage::ShaderBindingTable;
    sbtDesc.memory = RHIMemoryType::Upload;
    sbtDesc.debugName = "PipelinePathTracer_SBT";
    p.sbt = device->CreateBuffer(sbtDesc);
    if (!p.sbt.IsValid()) return false;
    auto* mapped = static_cast<uint8_t*>(device->MapBuffer(p.sbt));
    if (!mapped) return false;
    std::memset(mapped, 0, 192);
    const wchar_t* names[] = {L"PathRayGen", L"PathMiss", L"ShadowMiss", L"PathHitGroup"};
    const uint32_t offsets[] = {0, 64, 96, 128};
    for (uint32_t i = 0; i < 4; ++i) {
        auto* identifier = properties->GetShaderIdentifier(names[i]);
        if (!identifier) { device->UnmapBuffer(p.sbt); return false; }
        std::memcpy(mapped + offsets[i], identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    }
    device->UnmapBuffer(p.sbt);
    p.ready = true;
    UZ_LOG(Renderer, Info,
           "PipelinePathTracer: RT-PSO CreateStateObject succeeded; 3 lib_6_6 libraries, "
           "{}-byte PathPayload, 4-byte ShadowPayload, depth=2 (iterative+shadow), SBT ready",
           kPayloadBytes);
    return true;
#else
    return false;
#endif
}

bool PipelinePathTracerPass::AddPass(rg::RgBuilder& builder, rg::RgResourceId hdr,
        CBAllocation perFrame, RTManager& rt, IRHICommandList* buildCmd,
        std::span<const DrawCommand> draws, std::span<const GPUMesh> meshes,
        uint32_t envSrv, uint32_t frameIndex, uint32_t width, uint32_t height, World* world,
        const Mat4& unjitteredViewProj, uint32_t sceneDirtyFlags,
        const NrdInputs& denoiserGuides) {
    auto& p = *m_impl;
    if (!p.ready || !rt.IsEnabled()) return false;
    if (p.readbackPending) {
        p.device->WaitIdle();
        auto* data = static_cast<const RestirReservoir*>(p.device->MapBuffer(p.readback));
        if (data) {
            for (uint32_t i = 0; i < 20; ++i)
                UZ_LOG(Renderer, Info, "ReSTIR readback frame={} probe={} light={} M={} W={} sum={}",
                    p.readbackFrame, i, data[i].sample.lightIndex, data[i].M, data[i].W, data[i].weightSum);
            p.device->UnmapBuffer(p.readback);
        }
        p.readbackPending = false;
    }
    std::vector<PipelinePathMaterial> entries;
    // Match TLASManager::Rebuild's order and inclusion rules, using its
    // cached BLAS manager rather than constructing another TLAS.
    const uint32_t instanceCount = rt.GetTLASInstanceCount();
    entries.reserve(instanceCount);
    auto srv = [](uint32_t index) { return index == 0 ? UINT32_MAX : index; };
    for (const auto& draw : draws) {
        if (entries.size() == instanceCount) break;
        if (!IsGIOccluder(draw) || draw.meshId >= meshes.size()) continue;
        const auto& mesh = meshes[draw.meshId];
        if (!mesh.IsValid()) continue;
        auto blas = rt.GetBLASManager().GetOrBuildBLAS(draw.meshId, mesh, buildCmd);
        if (!blas.IsValid() || p.device->GetBLASGPUAddress(blas) == 0) continue;
        const bool packed = mesh.vertexStride == sizeof(VertexPacked);
        if (!packed && mesh.vertexStride != sizeof(Vertex) && mesh.vertexStride != sizeof(SkinnedVertex)) {
            UZ_LOG(Renderer, Error, "PipelinePathTracer: unsupported vertex stride {}", mesh.vertexStride);
            return false;
        }
        PipelinePathMaterial mat{};
        mat.tint[0] = draw.baseColorTintR;
        mat.tint[1] = draw.baseColorTintG;
        mat.tint[2] = draw.baseColorTintB;
        mat.albedoSrv = srv(draw.albedoSrvIndex);
        mat.vbSrv = p.device->GetBufferRawSRVIndex(mesh.vertexBuffer);
        mat.ibSrv = mesh.IsIndexed() ? p.device->GetBufferRawSRVIndex(mesh.indexBuffer) : UINT32_MAX;
        mat.vertexStride = mesh.vertexStride;
        mat.uvOffset = packed ? offsetof(VertexPacked, texcoord) : offsetof(Vertex, texcoord);
        mat.normalOffset = packed ? offsetof(VertexPacked, packedNormal) : offsetof(Vertex, normal);
        mat.normalFormat = packed ? 1u : 0u;
        mat.indexFormat = mesh.indexFormat == RHIIndexFormat::Uint16 ? 2u : 4u;
        mat.roughnessSrv = srv(draw.roughnessSrvIndex);
        mat.metallicSrv = srv(draw.metallicSrvIndex);
        mat.materialFlags = draw.materialFlags;
        mat.lightSourceIndex = uint32_t(&draw - draws.data());
        mat.coatWeight = cv_Layered->value >= 0.5f ? cv_CoatWeight->value : 0.f;
        mat.coatRoughness = cv_CoatRoughness->value;
        mat.coatIOR = cv_CoatIOR->value;
        mat.coatThickness = cv_CoatThickness->value;
        mat.coatSigmaA[0] = cv_CoatSigmaAR->value;
        mat.coatSigmaA[1] = cv_CoatSigmaAG->value;
        mat.coatSigmaA[2] = cv_CoatSigmaAB->value;
        mat.stackDepth = cv_Layered->value >= 0.5f ? uint32_t(cv_StackDepth->value) : 0u;
        for (int layer = 0; layer < 3; ++layer) {
            mat.stackRoughness[layer] = cv_StackRoughness[layer]->value;
            mat.stackIOR[layer] = cv_StackIOR[layer]->value;
            mat.stackThickness[layer] = cv_StackThickness[layer]->value;
            mat.stackSigmaAR[layer] = cv_StackSigmaAR[layer]->value;
            mat.stackSigmaAG[layer] = cv_StackSigmaAG[layer]->value;
            mat.stackSigmaAB[layer] = cv_StackSigmaAB[layer]->value;
        }
        // Hair has no dielectric coat. Reuse its dormant material slots instead
        // of adding hair fields or growing the 24-DWORD PathTracerRoot.
        if (((draw.materialFlags >> 24u) & 15u) == 6u) {
            mat.coatWeight = cv_HairMultiScatter->value >= 0.5f ? 1.f : 0.f;
            mat.coatRoughness = cv_HairExplicitBounces->value;
            mat.coatIOR = cv_HairDensity->value;
            mat.stackDepth = 0u;
        }
        mat.roughnessMultiplier = draw.roughnessMultiplier;
        mat.metallicMultiplier = draw.metallicMultiplier;
        mat.uvScale[0] = draw.uvScaleX; mat.uvScale[1] = draw.uvScaleY;
        mat.uvOffsetValue[0] = draw.uvOffsetX; mat.uvOffsetValue[1] = draw.uvOffsetY;
        const Vec3 emission = PathMaterialEmission(draw);
        mat.emission[0] = emission.x;
        mat.emission[1] = emission.y;
        mat.emission[2] = emission.z;
        mat.emissiveSrv = draw.materialGraphPipeline.IsValid() ? UINT32_MAX : srv(draw.emissiveSrvIndex);
        entries.push_back(mat);
    }
    if (entries.size() != instanceCount) {
        UZ_LOG(Renderer, Error, "PipelinePathTracer: material/TLAS instance count mismatch {}/{}",
               entries.size(), instanceCount);
        return false;
    }
    const uint32_t slot = frameIndex % 3;
    const uint32_t needed = std::max(instanceCount, 1u);
    if (p.capacity[slot] < needed) {
        if (p.materials[slot].IsValid()) {
            p.device->WaitIdle();
            p.device->DestroyBuffer(p.materials[slot]);
        }
        RHIBufferDesc desc{};
        desc.sizeBytes = uint64_t(needed) * sizeof(PipelinePathMaterial);
        desc.stride = sizeof(PipelinePathMaterial);
        desc.memory = RHIMemoryType::Upload;
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.debugName = "PipelinePathTracer_Materials";
        p.materials[slot] = p.device->CreateBuffer(desc);
        p.capacity[slot] = p.materials[slot].IsValid() ? needed : 0;
    }
    if (!p.materials[slot].IsValid()) return false;
    auto* mapped = p.device->MapBuffer(p.materials[slot]);
    if (!mapped) return false;
    if (!entries.empty()) std::memcpy(mapped, entries.data(), entries.size() * sizeof(PipelinePathMaterial));
    p.device->UnmapBuffer(p.materials[slot]);

    std::vector<PipelinePathDirectionalLight> directionalLights;
    if (world) {
        auto view = world->GetRegistry().view<DirectionalLightComponent>();
        for (auto entity : view) {
            const auto& light = view.get<DirectionalLightComponent>(entity);
            const auto* scene = world->GetRegistry().try_get<SceneComponent>(entity);
            const Vec3 direction = scene
                ? (scene->GetRotation() * Vec3::Forward()).Normalized()
                : light.GetDirection().Normalized();
            Vec3 color = light.color;
            if (light.useTemperature) color = color * KelvinToRgb(light.temperature);
            color = color * light.intensity;
            directionalLights.push_back({
                {direction.x, direction.y, direction.z},
                light.lightSourceAngle * (3.14159265359f / 360.f),
                {color.x, color.y, color.z}, 0
            });
        }
    }
    const uint32_t lightCount = static_cast<uint32_t>(directionalLights.size());
    uint32_t bvhConfigSrv = 0;
    const bool lightBVHEnabled = CVarRegistry::Get().GetBool("r.LightBVH", false);
    const uint32_t bvhUniform = CVarRegistry::Get().GetBool("r.LightBVH.Uniform", false);
    if (lightBVHEnabled) {
        if (p.lightBVH.Gather(draws, meshes, world)) {
            UZ_LOG(Renderer, Info, "LightBVH: Morton build {} lights, {} nodes, root power={}",
                p.lightBVH.lights.size(), p.lightBVH.nodes.size(),
                p.lightBVH.nodes.empty() ? 0.f : p.lightBVH.nodes[0].totalEmissionPower);
        }
        if (!p.lightBVH.lights.empty()) {
            auto upload = [&](RHIBufferHandle& buffer, uint32_t& capacity, const void* data,
                              uint32_t count, uint32_t stride, const char* name) {
                if (capacity < count) {
                    if (buffer.IsValid()) {
                        p.device->WaitIdle();
                        p.device->DestroyBuffer(buffer);
                    }
                    RHIBufferDesc desc{};
                    desc.sizeBytes = uint64_t(count)*stride; desc.stride = stride;
                    desc.memory = RHIMemoryType::Upload;
                    desc.usage = RHIBufferUsage::StructuredBuffer; desc.debugName = name;
                    buffer = p.device->CreateBuffer(desc);
                    capacity = buffer.IsValid() ? count : 0;
                }
                if (!buffer.IsValid()) return false;
                void* destination = p.device->MapBuffer(buffer);
                if (!destination) return false;
                std::memcpy(destination,data,size_t(count)*stride);
                p.device->UnmapBuffer(buffer);
                return true;
            };
            if (!upload(p.bvhNodes[slot],p.bvhNodeCapacity[slot],p.lightBVH.nodes.data(),
                        uint32_t(p.lightBVH.nodes.size()),sizeof(LightBVHNode),"LightBVH_Nodes")
                || !upload(p.bvhLights[slot],p.bvhLightCapacity[slot],p.lightBVH.lights.data(),
                        uint32_t(p.lightBVH.lights.size()),sizeof(LightBVHLight),"LightBVH_Lights"))
                return false;
            LightBVHConfig config{p.device->GetBufferSRVIndex(p.bvhNodes[slot]),
                p.device->GetBufferSRVIndex(p.bvhLights[slot]),
                uint32_t(p.lightBVH.lights.size()), bvhUniform};
            if (!upload(p.bvhConfigs[slot],p.bvhConfigCapacity[slot],&config,1,
                        sizeof(config),"LightBVH_Config")) return false;
            bvhConfigSrv = p.device->GetBufferSRVIndex(p.bvhConfigs[slot])+1u;
            if (!p.bvhLogged) {
                UZ_LOG(Renderer, Info, "LightBVH: GPU hierarchy uploaded; {} traversal in path-tracer NEE and opt-in ReSTIR RIS",
                    bvhUniform ? "uniform reference" : "stochastic");
                p.bvhLogged = true;
            }
        }
    }
    // The existing 32-byte light record has an unused DWORD. Point it at a
    // pass-private structured config instead of growing the 24-DWORD root.
    if (!directionalLights.empty()) directionalLights[0].padding = bvhConfigSrv;
    const uint32_t lightNeeded = std::max(lightCount, 1u);
    if (p.lightCapacity[slot] < lightNeeded) {
        if (p.lights[slot].IsValid()) {
            p.device->WaitIdle();
            p.device->DestroyBuffer(p.lights[slot]);
        }
        RHIBufferDesc desc{};
        desc.sizeBytes = uint64_t(lightNeeded) * sizeof(PipelinePathDirectionalLight);
        desc.stride = sizeof(PipelinePathDirectionalLight);
        desc.memory = RHIMemoryType::Upload;
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.debugName = "PipelinePathTracer_DirectionalLights";
        p.lights[slot] = p.device->CreateBuffer(desc);
        p.lightCapacity[slot] = p.lights[slot].IsValid() ? lightNeeded : 0;
    }
    if (!p.lights[slot].IsValid()) return false;
    mapped = p.device->MapBuffer(p.lights[slot]);
    if (!mapped) return false;
    if (lightCount)
        std::memcpy(mapped, directionalLights.data(), lightCount * sizeof(PipelinePathDirectionalLight));
    else {
        PipelinePathDirectionalLight dummy{};
        dummy.padding = bvhConfigSrv;
        std::memcpy(mapped,&dummy,sizeof(dummy));
    }
    p.device->UnmapBuffer(p.lights[slot]);
    const uint32_t lightSrv = p.device->GetBufferSRVIndex(p.lights[slot]);

    // Hash only transport inputs, never timeParams or the raster TAA jitter.
    uint64_t signature = 14695981039346656037ull;
    auto hash = [&](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) signature = (signature ^ bytes[i]) * 1099511628211ull;
    };
    hash(&envSrv, sizeof(envSrv));
    for (const auto& draw : draws) {
        hash(&draw.worldMatrix, sizeof(draw.worldMatrix));
        hash(&draw.meshId, sizeof(draw.meshId));
    }
    hash(entries.data(), entries.size() * sizeof(PipelinePathMaterial));
    // Descriptor indices rotate across frame slots and are not transport inputs.
    for (const auto& light : directionalLights)
        hash(&light, offsetof(PipelinePathDirectionalLight,padding));
    hash(&lightBVHEnabled,sizeof(lightBVHEnabled));
    hash(&bvhUniform,sizeof(bvhUniform));
    const auto& cv = CVarRegistry::Get();
    uint32_t maxBounces = std::clamp(cv.GetInt("r.PathTracer.MaxBounces", 4), 1, 16);
    const bool hairScene = std::any_of(entries.begin(), entries.end(), [](const PipelinePathMaterial& mat) {
        return ((mat.materialFlags >> 24u) & 15u) == 6u;
    });
    if (cv_HairMaxBounces->value > 0.f && hairScene) {
        maxBounces = std::clamp(int(cv_HairMaxBounces->value), 1, 128);
    } else if (hairScene && cv_HairMultiScatter->value >= 0.5f) {
        // K explicit vertices need one more intersection for the replacement.
        // Otherwise enabling the closure at K >= the ordinary depth is a no-op.
        maxBounces = std::max(maxBounces, uint32_t(cv_HairExplicitBounces->value) + 1u);
    }
    const uint32_t rrEnabled = cv.GetBool("r.PathTracer.RussianRoulette", true);
    const uint32_t rrMinBounce = std::clamp(cv.GetInt("r.PathTracer.RussianRouletteMinBounce", 2), 0, 16);
    PathVolumeRoot volume{};
    volume.volumeSigmaA = cv_VolumeA->value;
    volume.volumeSigmaS = cv_VolumeS->value;
    volume.volumeG = cv_VolumeG->value;
    volume.volumeRadius = cv_VolumeRadius->value;
    volume.volumeCenterX = cv_VolumeX->value;
    volume.volumeCenterY = cv_VolumeY->value;
    volume.volumeCenterZ = cv_VolumeZ->value;
    volume.volumeVariation = cv_VolumeVariation->value;
    volume.volumeEnabled = cv_Volume->value >= 0.5f
        && volume.volumeRadius > 0.f && volume.volumeSigmaA + volume.volumeSigmaS > 0.f;
    // Surface-only reservoir validation and diffuse denoiser guides do not
    // describe stochastic medium vertices. Use ordinary unbiased NEE here.
    // Stage-1 reservoirs contain only the base material. Coated vertices use
    // matched layered NEE instead of reusing an incompatible base-only target.
    const bool layered = cv_Layered->value >= 0.5f && cv_CoatWeight->value > 0.f;
    const uint32_t restirEnabled = cv_RestirDI->value >= 0.5f && !volume.volumeEnabled && !layered;
    const uint32_t restirTemporal = cv_RestirTemporal->value >= 0.5f;
    const uint32_t restirSpatial = cv_RestirSpatial->value >= 0.5f;
    const uint32_t neeSamples = std::clamp(cv.GetInt("r.PathTracer.NEESamples", 4), 1, 32);
    const bool denoise = cv_Nrd->value >= 0.5f && !volume.volumeEnabled
        && !(layered && cv_StackDepth->value >= 2.f);
    // Stage-1 one-bounce reservoirs hold one base-material NEE sample per
    // vertex, and the canonical weight 1/p_A reproduces the plain estimator
    // only without roulette before the second vertex.
    const bool giRestir = p.giCompiled && cv_GIRestir->value >= 0.5f && !volume.volumeEnabled && !layered && !denoise
        && neeSamples == 1u && maxBounces >= 2u && (rrEnabled == 0u || rrMinBounce >= 2u)
        && (bvhConfigSrv != 0u || lightCount <= 1u);
    const uint32_t giTemporal = cv_GIRestirTemporal->value >= 0.5f;
    const uint32_t giSpatial = cv_GIRestirSpatial->value >= 0.5f;
    hash(&maxBounces, sizeof(maxBounces));
    hash(&rrEnabled, sizeof(rrEnabled));
    hash(&rrMinBounce, sizeof(rrMinBounce));
    hash(&restirEnabled, sizeof(restirEnabled));
    hash(&restirTemporal, sizeof(restirTemporal));
    hash(&restirSpatial, sizeof(restirSpatial));
    hash(&neeSamples, sizeof(neeSamples));
    hash(&giRestir, sizeof(giRestir));
    hash(&giTemporal, sizeof(giTemporal));
    hash(&giSpatial, sizeof(giSpatial));
    hash(&volume, sizeof(volume));
    const auto* pf = static_cast<const PerFrameCB*>(perFrame.cpuPtr);
    hash(&pf->skyLightIntensity, sizeof(float) * 4);
    const uint64_t denoiserSignature = signature;
    hash(&unjitteredViewProj, sizeof(unjitteredViewProj));
    if (p.width != width || p.height != height) {
        p.device->WaitIdle();
        if (p.restirSurface.IsValid()) p.device->DestroyBuffer(p.restirSurface);
        if (p.restirInitial.IsValid()) p.device->DestroyBuffer(p.restirInitial);
        for (auto& buffer : p.reservoirs) {
            if (buffer.IsValid()) p.device->DestroyBuffer(buffer);
            buffer = {};
        }
        p.restirSurface = {}; p.restirInitial = {};
        if (p.giInitial.IsValid()) p.device->DestroyBuffer(p.giInitial);
        for (auto& buffer : p.giReservoirs) {
            if (buffer.IsValid()) p.device->DestroyBuffer(buffer);
            buffer = {};
        }
        p.giInitial = {};
        if (p.accumulation.IsValid()) p.device->DestroyTexture(p.accumulation);
        if (p.compensation.IsValid()) p.device->DestroyTexture(p.compensation);
        RHITextureDesc desc{};
        desc.width = width; desc.height = height;
        desc.format = RHIFormat::RGBA32Float;
        desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        desc.debugName = "PipelinePathTracer_Accumulation";
        p.accumulation = p.device->CreateTexture(desc);
        desc.debugName = "PipelinePathTracer_Compensation";
        p.compensation = p.device->CreateTexture(desc);
        p.width = width; p.height = height; p.samples = 0;
    }
    if (!p.accumulation.IsValid() || !p.compensation.IsValid()) return false;
    if (restirEnabled && !p.restirSurface.IsValid()) {
        RHIBufferDesc desc{};
        desc.sizeBytes = uint64_t(width) * height * sizeof(RestirSurface);
        desc.stride = sizeof(RestirSurface);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.debugName = "ReSTIR_PrimarySurface";
        p.restirSurface = p.device->CreateBuffer(desc);
        desc.stride = sizeof(RestirReservoir);
        desc.sizeBytes = uint64_t(width) * height * desc.stride;
        desc.debugName = "ReSTIR_Initial";
        p.restirInitial = p.device->CreateBuffer(desc);
        for (auto& buffer : p.reservoirs) {
            desc.debugName = "ReSTIR_Temporal";
            buffer = p.device->CreateBuffer(desc);
        }
        if (!p.restirSurface.IsValid() || !p.restirInitial.IsValid()
            || !p.reservoirs[0].IsValid() || !p.reservoirs[1].IsValid()) return false;
        p.samples = 0;
    }
    if (giRestir && !p.giInitial.IsValid()) {
        RHIBufferDesc desc{};
        desc.stride = sizeof(RestirGIReservoir);
        desc.sizeBytes = uint64_t(width) * height * desc.stride;
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.debugName = "GIReSTIR_Initial";
        p.giInitial = p.device->CreateBuffer(desc);
        for (auto& buffer : p.giReservoirs) {
            desc.debugName = "GIReSTIR_Temporal";
            buffer = p.device->CreateBuffer(desc);
        }
        if (!p.giInitial.IsValid() || !p.giReservoirs[0].IsValid() || !p.giReservoirs[1].IsValid()) return false;
        p.samples = 0;
    }
    const uint32_t resetGeneration = GetPathTracerResetGeneration();
    const bool denoiserReset = frameIndex != p.lastFrame + 1
        || resetGeneration != p.resetGeneration || !cv.GetBool("r.PathTracer.Accumulate", true)
        || denoiserSignature != p.previousDenoiserSignature || sceneDirtyFlags != 0;
    if (signature != p.previousSignature || sceneDirtyFlags != 0 ||
        frameIndex != p.lastFrame + 1 || resetGeneration != p.resetGeneration ||
        !cv.GetBool("r.PathTracer.Accumulate", true)) {
        if (p.samples > 0)
            UZ_LOG(Renderer, Info, "PipelinePathTracer: accumulation reset after {} samples", p.samples);
        p.samples = 0;
    }
    p.previousSignature = signature;
    p.previousDenoiserSignature = denoiserSignature;
    p.lastFrame = frameIndex;
    p.resetGeneration = resetGeneration;
    const uint32_t accumulatedFrames = p.samples++;
    const auto accumulation = builder.ImportTrackedTexture("PipelinePathTracer_Accumulation", p.accumulation);
    const auto compensation = builder.ImportTrackedTexture("PipelinePathTracer_Compensation", p.compensation);

    rg::RgTextureDesc radianceDesc{};
    radianceDesc.width = width; radianceDesc.height = height;
    radianceDesc.format = RHIFormat::RGBA16Float;
    radianceDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    const auto radiance = builder.CreateTexture("PipelinePathTracer_Radiance", radianceDesc);
    rg::RgResourceId diffuse, fraction, signalAlbedo, specular, modulation, virtualPosition, pathGuide;
    if (denoise) {
        if (!p.denoiser) {
            p.denoiser = std::make_unique<NrdDenoiser>();
            if (!p.denoiser->Init(p.device)) {
                p.denoiser.reset();
                return false;
            }
        }
        diffuse = builder.CreateTexture("NRD_DiffuseRadiance", radianceDesc);
        fraction = builder.CreateTexture("NRD_PrimaryDiffuseFraction", radianceDesc);
        signalAlbedo = builder.CreateTexture("NRD_PrimaryAlbedo", radianceDesc);
        specular = builder.CreateTexture("NRD_SpecularRadiance", radianceDesc);
        modulation = builder.CreateTexture("NRD_SpecularModulation", radianceDesc);
        // World-space virtual points need float32 precision at large distances.
        auto virtualDesc = radianceDesc;
        virtualDesc.format = RHIFormat::RGBA32Float;
        virtualPosition = builder.CreateTexture("NRD_VirtualPosition", virtualDesc);
        pathGuide = builder.CreateTexture("NRD_PrimaryGuide", virtualDesc);
    }
    const uint32_t materialSrv = p.device->GetBufferSRVIndex(p.materials[slot]);
    const uint32_t tlasSrv = rt.GetTLASSRVIndex();
    rg::RgResourceId surface, initial, current, previous;
    const auto currentBuffer = p.reservoirs[frameIndex & 1u];
    const auto previousBuffer = p.reservoirs[(frameIndex & 1u) ^ 1u];
    PipelinePathRoot restirConstants{};
    restirConstants.materialTableSrv = materialSrv;
    restirConstants.envMapSrv = envSrv;
    restirConstants.sampleIndex = accumulatedFrames;
    restirConstants.hasGeometry = instanceCount > 0 ? 1u : 0u;
    restirConstants.directionalLightsSrv = lightSrv;
    restirConstants.directionalLightCount = lightCount;
    restirConstants.maxBounces = maxBounces;
    restirConstants.rrEnabled = rrEnabled;
    restirConstants.rrMinBounce = rrMinBounce;
    restirConstants.restirEnabled = restirEnabled;
    restirConstants.restirWidth = width; restirConstants.restirHeight = height;
    restirConstants.restirHistory = accumulatedFrames > 0 ? 1u : 0u;
    restirConstants.neeSamples = neeSamples;
    restirConstants.restirTemporal = restirTemporal;
    restirConstants.restirLightCount = bvhConfigSrv != 0u ? uint32_t(p.lightBVH.lights.size()) : lightCount;
    const bool restirBVH = restirEnabled && bvhConfigSrv != 0u;
    RestirGIRoot giConstants{};
    rg::RgResourceId giInitial, giCurrent, giPrevious, giPartial;
    const auto giCurrentBuffer = p.giReservoirs[frameIndex & 1u];
    const auto giPreviousBuffer = p.giReservoirs[(frameIndex & 1u) ^ 1u];
    if (giRestir) {
        giInitial = builder.ImportTrackedBuffer("GIReSTIR_Initial", p.giInitial);
        giCurrent = builder.ImportTrackedBuffer("GIReSTIR_Current", giCurrentBuffer);
        giPrevious = builder.ImportTrackedBuffer("GIReSTIR_Previous", giPreviousBuffer);
        // The path's radiance without its one-bounce term, resolved after reuse.
        auto partialDesc = radianceDesc;
        partialDesc.format = RHIFormat::RGBA32Float;
        giPartial = builder.CreateTexture("PipelinePathTracer_GIPartial", partialDesc);
        giConstants.giRestirEnabled = 1u;
        giConstants.giRestirInitialUav = p.device->GetBufferUAVIndex(p.giInitial);
        giConstants.giRestirCurrentUav = p.device->GetBufferUAVIndex(giCurrentBuffer);
        giConstants.giRestirPreviousUav = p.device->GetBufferUAVIndex(giPreviousBuffer);
        giConstants.giRestirTemporal = giTemporal;
        giConstants.giRestirSpatial = giSpatial;
    } else if (cv_GIRestir->value >= 0.5f && accumulatedFrames == 0) {
        UZ_LOG(Renderer, Info, "GI-ReSTIR: requested but inactive (compiled at init={}, NEESamples={}, maxBounces={}, RR={} from {}, volume={}, layered={}, NRD={}, BVH={}, directional lights={}); plain transport retained",
            p.giCompiled, neeSamples, maxBounces, rrEnabled, rrMinBounce, volume.volumeEnabled, layered, denoise, bvhConfigSrv != 0u, lightCount);
    }
    const RestirGIRoot giInactive{};
    if (restirEnabled) {
        surface = builder.ImportTrackedBuffer("ReSTIR_Surface", p.restirSurface);
        initial = builder.ImportTrackedBuffer("ReSTIR_Initial", p.restirInitial);
        current = builder.ImportTrackedBuffer("ReSTIR_Current", currentBuffer);
        previous = builder.ImportTrackedBuffer("ReSTIR_Previous", previousBuffer);
        restirConstants.restirSurfaceUav = p.device->GetBufferUAVIndex(p.restirSurface);
        restirConstants.restirInitialUav = p.device->GetBufferUAVIndex(p.restirInitial);
        restirConstants.restirCurrentUav = p.device->GetBufferUAVIndex(currentBuffer);
        restirConstants.restirPreviousUav = p.device->GetBufferUAVIndex(previousBuffer);
        builder.AddPass("ReSTIR_PrimaryHit", [=, this](rg::RgPassBuilder& pb) {
            pb.Write(surface, RHIResourceState::UnorderedAccess);
            pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources&) {
#ifdef _WIN32
                auto* dx = static_cast<DX12Device*>(m_impl->device);
                ComPtr<ID3D12GraphicsCommandList4> native;
                if (FAILED(static_cast<DX12CommandList*>(cmd)->GetNative()->QueryInterface(IID_PPV_ARGS(&native)))) return;
                auto constants = restirConstants;
                constants.restirMode = 1;
                native->SetComputeRootSignature(m_impl->root.Get());
                native->SetComputeRoot32BitConstants(0, kRootDwords, &constants, 0);
                const NrdSignalRoot signal{};
                native->SetComputeRoot32BitConstants(4, sizeof(signal) / 4, &signal, 0);
                native->SetComputeRoot32BitConstants(5, sizeof(volume) / 4, &volume, 0);
                native->SetComputeRoot32BitConstants(6, sizeof(giInactive) / 4, &giInactive, 0);
                native->SetComputeRootConstantBufferView(1, m_impl->device->GetBufferGPUAddress(perFrame.bufferHandle) + perFrame.offsetBytes);
                auto& heap = dx->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                native->SetComputeRootDescriptorTable(2, heap.GpuHandle(tlasSrv));
                // The extraction branch never writes radiance, but the table must be bound.
                native->SetComputeRootDescriptorTable(3, heap.GpuHandle(m_impl->device->GetTextureUAVIndex(m_impl->accumulation)));
                native->SetPipelineState1(m_impl->pipeline.Get());
                const uint64_t address = m_impl->device->GetBufferGPUAddress(m_impl->sbt);
                D3D12_DISPATCH_RAYS_DESC dispatch{};
                dispatch.RayGenerationShaderRecord = {address, 32};
                dispatch.MissShaderTable = {address + 64, 64, 32};
                dispatch.HitGroupTable = {address + 128, 32, 32};
                dispatch.Width = width; dispatch.Height = height; dispatch.Depth = 1;
                native->DispatchRays(&dispatch);
                native->SetComputeRootSignature(dx->GetRootSignature());
#endif
            });
        });
        for (uint32_t stage = 0; stage < (restirSpatial ? 3u : 2u); ++stage) {
            builder.AddPass(stage == 2 ? "ReSTIR_Spatial" : stage == 1 ? "ReSTIR_Temporal" : "ReSTIR_Initial",
                [=, this](rg::RgPassBuilder& pb) {
                pb.Read(surface, RHIResourceState::UnorderedAccess);
                if (stage == 2) {
                    pb.Read(current, RHIResourceState::UnorderedAccess);
                    pb.Write(initial, RHIResourceState::UnorderedAccess);
                } else if (stage == 1) {
                    pb.Read(initial, RHIResourceState::UnorderedAccess);
                    pb.Read(previous, RHIResourceState::UnorderedAccess);
                    pb.Write(current, RHIResourceState::UnorderedAccess);
                } else pb.Write(initial, RHIResourceState::UnorderedAccess);
                pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources&) {
#ifdef _WIN32
                    auto* native = static_cast<DX12CommandList*>(cmd)->GetNative();
                    native->SetComputeRootSignature(m_impl->root.Get());
                    native->SetComputeRoot32BitConstants(0, kRootDwords, &restirConstants, 0);
                    native->SetComputeRoot32BitConstants(6, sizeof(giInactive) / 4, &giInactive, 0);
                    native->SetComputeRootConstantBufferView(1, m_impl->device->GetBufferGPUAddress(perFrame.bufferHandle) + perFrame.offsetBytes);
                    auto* dx = static_cast<DX12Device*>(m_impl->device);
                    auto& heap = dx->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    native->SetComputeRootDescriptorTable(2, heap.GpuHandle(tlasSrv));
                    native->SetPipelineState(stage == 2 ? m_impl->restirSpatialPSO.Get()
                        : stage == 1 ? m_impl->restirTemporalPSO.Get() : m_impl->restirInitialPSO.Get());
                    native->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                    native->SetComputeRootSignature(static_cast<DX12Device*>(m_impl->device)->GetRootSignature());
                    if (accumulatedFrames == 0 || accumulatedFrames == 15)
                        UZ_LOG(Renderer, Info, "ReSTIR DI: {} dispatched {}x{}, history={}, 32 candidates, 20-frame M cap",
                            stage == 2 ? "spatial (4 neighbors, source-target MIS)" : stage == 1 ? "temporal"
                                : restirBVH ? "initial BVH-sourced RIS (joint source PDF, visibility-aware reuse)" : "initial",
                            (width + 7) / 8, (height + 7) / 8, restirConstants.restirHistory);
#endif
                });
            });
        }
    }
    builder.AddPass("PipelinePathTracer", [=, this](rg::RgPassBuilder& pb) {
        if (restirEnabled) pb.Read(restirSpatial ? initial : current, RHIResourceState::UnorderedAccess);
        pb.Write(radiance, RHIResourceState::UnorderedAccess);
        pb.Write(accumulation, RHIResourceState::UnorderedAccess);
        pb.Write(compensation, RHIResourceState::UnorderedAccess);
        if (giRestir) {
            pb.Write(giInitial, RHIResourceState::UnorderedAccess);
            pb.Write(giPartial, RHIResourceState::UnorderedAccess);
        }
        if (denoise) {
            pb.Write(diffuse, RHIResourceState::UnorderedAccess);
            pb.Write(fraction, RHIResourceState::UnorderedAccess);
            pb.Write(signalAlbedo, RHIResourceState::UnorderedAccess);
            pb.Write(specular, RHIResourceState::UnorderedAccess);
            pb.Write(modulation, RHIResourceState::UnorderedAccess);
            pb.Write(virtualPosition, RHIResourceState::UnorderedAccess);
            pb.Write(pathGuide, RHIResourceState::UnorderedAccess);
        }
        pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources& res) {
#ifdef _WIN32
            auto& state = *m_impl;
            auto* dx = static_cast<DX12Device*>(state.device);
            ComPtr<ID3D12GraphicsCommandList4> native;
            auto hr = static_cast<DX12CommandList*>(cmd)->GetNative()->QueryInterface(IID_PPV_ARGS(&native));
            if (FAILED(hr)) {
                UZ_LOG(Renderer, Error, "PipelinePathTracer: command list DXR interface failed hr=0x{:08X}", uint32_t(hr));
                return;
            }
            auto& heap = dx->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            native->SetComputeRootSignature(state.root.Get());
            auto constants = restirConstants;
            // Spatial output reuses the initial buffer, never the temporal history.
            if (restirEnabled && restirSpatial)
                constants.restirCurrentUav = constants.restirInitialUav;
            constants.accumulationUav = state.device->GetTextureUAVIndex(res.GetTexture(accumulation));
            constants.compensationUav = state.device->GetTextureUAVIndex(res.GetTexture(compensation));
            constants.accumulatedFrames = accumulatedFrames;
            native->SetComputeRoot32BitConstants(0, kRootDwords, &constants, 0);
            NrdSignalRoot signal{};
            signal.enabled = denoise;
            if (denoise) {
                signal.diffuseUav = state.device->GetTextureUAVIndex(res.GetTexture(diffuse));
                signal.fractionUav = state.device->GetTextureUAVIndex(res.GetTexture(fraction));
                signal.albedoUav = state.device->GetTextureUAVIndex(res.GetTexture(signalAlbedo));
                signal.specularUav = state.device->GetTextureUAVIndex(res.GetTexture(specular));
                signal.modulationUav = state.device->GetTextureUAVIndex(res.GetTexture(modulation));
                signal.virtualPositionUav = state.device->GetTextureUAVIndex(res.GetTexture(virtualPosition));
                signal.guideUav = state.device->GetTextureUAVIndex(res.GetTexture(pathGuide));
            }
            native->SetComputeRoot32BitConstants(4, sizeof(signal) / 4, &signal, 0);
            native->SetComputeRoot32BitConstants(5, sizeof(volume) / 4, &volume, 0);
            auto gi = giConstants;
            if (giRestir) gi.giRestirPartialUav = state.device->GetTextureUAVIndex(res.GetTexture(giPartial));
            native->SetComputeRoot32BitConstants(6, sizeof(gi) / 4, &gi, 0);
            if (accumulatedFrames == 0 && volume.volumeEnabled)
                UZ_LOG(Renderer, Info, "PathVolume: sphere sigmaA={} sigmaS={} g={} radius={} variation={}; delta free flight + ratio shadow tracking (exact homogeneous limit), HG + NEE; surface ReSTIR/NRD bypassed",
                    volume.volumeSigmaA,volume.volumeSigmaS,volume.volumeG,volume.volumeRadius,volume.volumeVariation);
            if (accumulatedFrames == 0 && layered && cv_StackDepth->value < 2.f)
                UZ_LOG(Renderer, Info, "PathLayered: coat weight={} roughness={} IOR={} thickness={} sigmaA=({},{},{}); Snell optical-distance approximation; base-only ReSTIR bypassed",
                    cv_CoatWeight->value,cv_CoatRoughness->value,cv_CoatIOR->value,
                    cv_CoatThickness->value,cv_CoatSigmaAR->value,cv_CoatSigmaAG->value,cv_CoatSigmaAB->value);
            if (accumulatedFrames == 0 && layered && cv_StackDepth->value >= 2.f)
                UZ_LOG(Renderer, Info, "PathStack: depth={}; adjacent-IOR GGX random walk, exact Fresnel/TIR, radiance and PDF Jacobians, unbiased internal roulette; last-exit analytic NEE, continuation-only area/environment; NRD bypassed",
                    cv_StackDepth->value);
            if (accumulatedFrames == 0 && hairScene)
                UZ_LOG(Renderer, Info, "HairDual: enabled={} K={} density={} maxBounces={}; biased passive tail replacement, distant lighting only",
                    cv_HairMultiScatter->value,cv_HairExplicitBounces->value,cv_HairDensity->value,maxBounces);
            native->SetComputeRootConstantBufferView(1,
                state.device->GetBufferGPUAddress(perFrame.bufferHandle) + perFrame.offsetBytes);
            native->SetComputeRootDescriptorTable(2, heap.GpuHandle(tlasSrv));
            native->SetComputeRootDescriptorTable(3,
                heap.GpuHandle(state.device->GetTextureUAVIndex(res.GetTexture(radiance))));
            native->SetPipelineState1(state.pipeline.Get());
            const uint64_t address = state.device->GetBufferGPUAddress(state.sbt);
            D3D12_DISPATCH_RAYS_DESC dispatch{};
            dispatch.RayGenerationShaderRecord = {address, 32};
            dispatch.MissShaderTable = {address + 64, 64, 32};
            dispatch.HitGroupTable = {address + 128, 32, 32};
            dispatch.Width = width; dispatch.Height = height; dispatch.Depth = 1;
            native->DispatchRays(&dispatch);
            if (accumulatedFrames == 0 || accumulatedFrames == 15 || accumulatedFrames == 95 || accumulatedFrames == 199)
                UZ_LOG(Renderer, Info, "PipelinePathTracer: accumulation samples={}, maxBounces={}, RR={} from {}",
                       accumulatedFrames + 1, maxBounces, rrEnabled, rrMinBounce);
            // Restore compute root signature before any later RHI compute
            // pass; graphics roots and the RHI's pipeline tracking were untouched.
            native->SetComputeRootSignature(dx->GetRootSignature());
            if (!state.logged && instanceCount > 0) {
                UZ_LOG(Renderer, Info, "PipelinePathTracer: DispatchRays {}x{}, {} TLAS instances, "
                       "{} surface bounces + shadow rays, {} directional lights, env SRV={}",
                       width, height, instanceCount, maxBounces, lightCount, envSrv);
                state.logged = true;
            }
#endif
        });
    });
    if (giRestir) {
        const uint32_t stages = giSpatial ? 3u : 2u;
        for (uint32_t stage = 0; stage < stages; ++stage) {
            const bool resolve = stage + 1u == stages;
            const bool spatial = giSpatial && stage == 1u;
            builder.AddPass(resolve ? "GIReSTIR_Resolve" : spatial ? "GIReSTIR_Spatial" : "GIReSTIR_Temporal",
                [=, this](rg::RgPassBuilder& pb) {
                if (resolve) {
                    // Spatial output reuses the initial buffer, never the temporal history.
                    pb.Read(giSpatial ? giInitial : giCurrent, RHIResourceState::UnorderedAccess);
                    pb.Read(giPartial, RHIResourceState::UnorderedAccess);
                    pb.Write(radiance, RHIResourceState::UnorderedAccess);
                    pb.Write(accumulation, RHIResourceState::UnorderedAccess);
                    pb.Write(compensation, RHIResourceState::UnorderedAccess);
                } else if (spatial) {
                    pb.Read(giCurrent, RHIResourceState::UnorderedAccess);
                    pb.Write(giInitial, RHIResourceState::UnorderedAccess);
                } else {
                    pb.Read(giInitial, RHIResourceState::UnorderedAccess);
                    pb.Read(giPrevious, RHIResourceState::UnorderedAccess);
                    pb.Write(giCurrent, RHIResourceState::UnorderedAccess);
                }
                pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources& res) {
#ifdef _WIN32
                    auto& state = *m_impl;
                    auto* native = static_cast<DX12CommandList*>(cmd)->GetNative();
                    auto* dx = static_cast<DX12Device*>(state.device);
                    auto& heap = dx->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    native->SetComputeRootSignature(state.root.Get());
                    auto constants = restirConstants;
                    constants.accumulationUav = state.device->GetTextureUAVIndex(res.GetTexture(accumulation));
                    constants.compensationUav = state.device->GetTextureUAVIndex(res.GetTexture(compensation));
                    constants.accumulatedFrames = accumulatedFrames;
                    native->SetComputeRoot32BitConstants(0, kRootDwords, &constants, 0);
                    native->SetComputeRootConstantBufferView(1,
                        state.device->GetBufferGPUAddress(perFrame.bufferHandle) + perFrame.offsetBytes);
                    native->SetComputeRootDescriptorTable(2, heap.GpuHandle(tlasSrv));
                    const NrdSignalRoot signal{};
                    native->SetComputeRoot32BitConstants(4, sizeof(signal) / 4, &signal, 0);
                    native->SetComputeRoot32BitConstants(5, sizeof(volume) / 4, &volume, 0);
                    auto gi = giConstants;
                    gi.giRestirPartialUav = state.device->GetTextureUAVIndex(res.GetTexture(giPartial));
                    if (resolve) {
                        if (giSpatial) gi.giRestirCurrentUav = gi.giRestirInitialUav;
                        gi.giRestirRadianceUav = state.device->GetTextureUAVIndex(res.GetTexture(radiance));
                    }
                    native->SetComputeRoot32BitConstants(6, sizeof(gi) / 4, &gi, 0);
                    native->SetPipelineState(resolve ? state.giResolvePSO.Get()
                        : spatial ? state.giSpatialPSO.Get() : state.giTemporalPSO.Get());
                    native->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                    native->SetComputeRootSignature(dx->GetRootSignature());
                    if (accumulatedFrames == 0 || accumulatedFrames == 15)
                        UZ_LOG(Renderer, Info, "GI-ReSTIR: {} dispatched {}x{}, history={}, one canonical candidate, 20-frame M cap, visibility-aware reuse",
                            resolve ? "resolve" : spatial ? "spatial (4 neighbors, source-target MIS)" : "temporal",
                            (width + 7) / 8, (height + 7) / 8, restirConstants.restirHistory);
#endif
                });
            });
        }
    }
    if (restirEnabled && cv_RestirReadback->value >= 0.5f) {
        if (!p.readback.IsValid()) {
            RHIBufferDesc desc{};
            desc.sizeBytes = sizeof(RestirReservoir) * 20;
            desc.memory = RHIMemoryType::Readback;
            desc.debugName = "ReSTIR_DiagnosticReadback";
            p.readback = p.device->CreateBuffer(desc);
        }
        const auto readback = builder.ImportTrackedBuffer("ReSTIR_Readback", p.readback);
        builder.AddPass("ReSTIR_Readback", [=, this](rg::RgPassBuilder& pb) {
            pb.Read(current, RHIResourceState::CopySrc);
            pb.Write(readback, RHIResourceState::CopyDst);
            pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources&) {
                cmd->CopyBuffer(currentBuffer, 0, m_impl->readback, 0, sizeof(RestirReservoir) * 10);
                for (uint32_t i = 0; i < 10; ++i) {
                    const uint32_t pixel = height / 2 * width + width * (i + 1) / 11;
                    cmd->CopyBuffer(currentBuffer, uint64_t(pixel) * sizeof(RestirReservoir),
                        m_impl->readback, uint64_t(i + 10) * sizeof(RestirReservoir), sizeof(RestirReservoir));
                }
                m_impl->readbackPending = true;
                m_impl->readbackFrame = accumulatedFrames + 1;
            });
        });
    }
    auto resolved = radiance;
    if (denoise) {
        auto inputs = denoiserGuides;
        inputs.raw = radiance; inputs.diffuse = diffuse; inputs.albedo = signalAlbedo;
        inputs.specular = specular; inputs.modulation = modulation; inputs.virtualPosition = virtualPosition;
        inputs.pathGuide = pathGuide;
        inputs.width = width; inputs.height = height; inputs.frameIndex = frameIndex;
        inputs.perFrame = perFrame;
        // Stage 2 validates moving views per pixel. Stage 1 retains its camera-cut reset.
        inputs.reset = inputs.reset || (cv.GetBool("r.NRD.Specular", true)
            ? denoiserReset : accumulatedFrames == 0);
        resolved = p.denoiser->Denoise(builder, inputs);
        if (!resolved.IsValid()) return false;
    }
    builder.AddPass("PipelinePathTracer_Resolve", [=](rg::RgPassBuilder& pb) {
        pb.Read(resolved, RHIResourceState::CopySrc);
        pb.Write(hdr, RHIResourceState::CopyDst);
        pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources& res) {
            cmd->CopyTexture(res.GetTexture(resolved), res.GetTexture(hdr));
        });
    });
    return true;
}
} // namespace uz
