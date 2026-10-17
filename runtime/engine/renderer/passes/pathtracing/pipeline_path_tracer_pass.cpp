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

namespace uz {
struct PipelinePathTracerPass::Impl {
    IRHIDevice* device = nullptr;
    RHIBufferHandle sbt;
    std::array<RHIBufferHandle, 3> materials{};
    std::array<uint32_t, 3> capacity{};
    std::array<RHIBufferHandle, 3> lights{};
    std::array<uint32_t, 3> lightCapacity{};
#ifdef _WIN32
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12StateObject> pipeline;
#endif
    bool ready = false;
    bool logged = false;

    ~Impl() {
        if (!device) return;
        device->WaitIdle();
        for (auto buffer : materials)
            if (buffer.IsValid()) device->DestroyBuffer(buffer);
        for (auto buffer : lights)
            if (buffer.IsValid()) device->DestroyBuffer(buffer);
        if (sbt.IsValid()) device->DestroyBuffer(sbt);
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
    D3D12_ROOT_PARAMETER params[4]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = {0, 0, kRootDwords};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    for (uint32_t i = 0; i < 2; ++i) {
        params[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2 + i].DescriptorTable.NumDescriptorRanges = 1;
        params[2 + i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 4;
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
           "32-byte PathPayload, 4-byte ShadowPayload, depth=2 (primary+shadow), SBT ready");
    return true;
#else
    return false;
#endif
}

bool PipelinePathTracerPass::AddPass(rg::RgBuilder& builder, rg::RgResourceId hdr,
        CBAllocation perFrame, RTManager& rt, IRHICommandList* buildCmd,
        std::span<const DrawCommand> draws, std::span<const GPUMesh> meshes,
        uint32_t envSrv, uint32_t frameIndex, uint32_t width, uint32_t height, World* world) {
    auto& p = *m_impl;
    if (!p.ready || !rt.IsEnabled()) return false;
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
        mat.roughnessMultiplier = draw.roughnessMultiplier;
        mat.metallicMultiplier = draw.metallicMultiplier;
        mat.uvScale[0] = draw.uvScaleX; mat.uvScale[1] = draw.uvScaleY;
        mat.uvOffsetValue[0] = draw.uvOffsetX; mat.uvOffsetValue[1] = draw.uvOffsetY;
        mat.emission[0] = draw.emissiveColorR * draw.emissiveIntensity;
        mat.emission[1] = draw.emissiveColorG * draw.emissiveIntensity;
        mat.emission[2] = draw.emissiveColorB * draw.emissiveIntensity;
        mat.emissiveSrv = srv(draw.emissiveSrvIndex);
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
    p.device->UnmapBuffer(p.lights[slot]);
    const uint32_t lightSrv = p.device->GetBufferSRVIndex(p.lights[slot]);

    rg::RgTextureDesc radianceDesc{};
    radianceDesc.width = width; radianceDesc.height = height;
    radianceDesc.format = RHIFormat::RGBA16Float;
    radianceDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    const auto radiance = builder.CreateTexture("PipelinePathTracer_Radiance", radianceDesc);
    const uint32_t materialSrv = p.device->GetBufferSRVIndex(p.materials[slot]);
    const uint32_t tlasSrv = rt.GetTLASSRVIndex();
    builder.AddPass("PipelinePathTracer", [=, this](rg::RgPassBuilder& pb) {
        pb.Write(radiance, RHIResourceState::UnorderedAccess);
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
            const PipelinePathRoot constants{
                materialSrv, envSrv, frameIndex, instanceCount > 0 ? 1u : 0u,
                lightSrv, lightCount
            };
            native->SetComputeRoot32BitConstants(0, kRootDwords, &constants, 0);
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
            // Restore compute root signature before any later RHI compute
            // pass; graphics roots and the RHI's pipeline tracking were untouched.
            native->SetComputeRootSignature(dx->GetRootSignature());
            if (!state.logged && instanceCount > 0) {
                UZ_LOG(Renderer, Info, "PipelinePathTracer: DispatchRays {}x{}, {} TLAS instances, "
                       "single surface bounce + shadow rays, {} directional lights, env SRV={}",
                       width, height, instanceCount, lightCount, envSrv);
                state.logged = true;
            }
#endif
        });
    });
    builder.AddPass("PipelinePathTracer_Resolve", [=](rg::RgPassBuilder& pb) {
        pb.Read(radiance, RHIResourceState::CopySrc);
        pb.Write(hdr, RHIResourceState::CopyDst);
        pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources& res) {
            cmd->CopyTexture(res.GetTexture(radiance), res.GetTexture(hdr));
        });
    });
    return true;
}
} // namespace uz
