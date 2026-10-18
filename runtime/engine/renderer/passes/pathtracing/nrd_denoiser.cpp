#include "nrd_denoiser.h"
#include "render_graph_builder.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"
#include <array>
#ifdef _WIN32
#include "engine/rhi/dx12/dx12_device.h"
#include "engine/rhi/dx12/dx12_command_list.h"
#include "engine_shaders.h"
#include "shader_compiler.h"
#endif

UZ_DECLARE_LOG_CATEGORY(Renderer);
static auto* cv_NrdSpecular = UZ_CVAR_BOOL("r.NRD.Specular", true,
    "Separate roughness-aware specular reconstruction; false reproduces stage 1");
static auto* cv_NrdClamp = UZ_CVAR_BOOL("r.NRD.HistoryClamp", true,
    "Clamp temporal history to the current spatial mean/variance box");
static auto* cv_NrdDisocclusion = UZ_CVAR_BOOL("r.NRD.DisocclusionFix", true,
    "Widen spatial reconstruction for rejected temporal history");
static auto* cv_NrdDebug = UZ_CVAR_INT("r.NRD.DebugSignal", 0,
    "NRD output diagnostic: 0 composite, 1 raw, 2 residual, 3 specular, 4 diffuse", 0, 4);

namespace uz {
struct NrdDenoiser::Impl {
    IRHIDevice* device = nullptr;
    std::array<RHITextureHandle, 2> history{}, guides{}, specularHistory{};
    Mat4 previousViewProj;
    uint32_t previousFlags = 0;
    uint32_t width = 0, height = 0, lastFrame = UINT32_MAX;
#ifdef _WIN32
    ComPtr<ID3D12RootSignature> root;
    std::array<ComPtr<ID3D12PipelineState>, 2> pipelines;
#endif
    ~Impl() {
        if (!device) return;
        device->WaitIdle();
        for (auto textures : {history, guides, specularHistory})
            for (auto texture : textures)
                if (texture.IsValid()) device->DestroyTexture(texture);
    }
};

NrdDenoiser::NrdDenoiser() : m_impl(std::make_unique<Impl>()) {}
NrdDenoiser::~NrdDenoiser() = default;

bool NrdDenoiser::Init(IRHIDevice* device) {
#ifdef _WIN32
    auto& p = *m_impl;
    p.device = device;
    auto* dx = static_cast<DX12Device*>(device);
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants = {0, 0, sizeof(NrdRoot) / sizeof(uint32_t)};
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants = {1, 0, sizeof(NrdStage2Root) / sizeof(uint32_t)};
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister = 2;
    static_assert((sizeof(NrdRoot) + sizeof(NrdStage2Root)) / 4 + 2 <= 64);
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3;
    desc.pParameters = parameters;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                            &serialized, &errors);
    if (SUCCEEDED(hr))
        hr = dx->GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(&p.root));
    if (FAILED(hr)) return false;
    ShaderCompiler compiler;
    if (!compiler.Init()) return false;
    const EngineShader shaders[] = {EngineShader::NrdReprojectCS, EngineShader::NrdAtrousCS};
    for (uint32_t i = 0; i < 2; ++i) {
        ShaderCompileDesc shader{};
        shader.source = EngineShaders::GetSource(shaders[i]);
        shader.sourceName = EngineShaders::GetName(shaders[i]);
        shader.stage = ShaderStageCompile::Compute;
        shader.model = ShaderModelCompile::SM6_6;
        auto result = compiler.Compile(shader);
        if (!result.success) {
            UZ_LOG(Renderer, Error, "NRD native: compile failed for '{}': {}",
                   shader.sourceName, result.errors);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = p.root.Get();
        pipeline.CS = {result.bytecode.data(), result.bytecode.size()};
        hr = dx->GetDevice()->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&p.pipelines[i]));
        if (FAILED(hr)) return false;
        UZ_LOG(Renderer, Info, "NRD native: '{}' compiled cs_6_6; PSO ready", shader.sourceName);
    }
    return true;
#else
    return false;
#endif
}

rg::RgResourceId NrdDenoiser::Denoise(rg::RgBuilder& builder, const NrdInputs& in) {
    auto& p = *m_impl;
    const bool stage2 = cv_NrdSpecular->value >= .5f;
    const uint32_t flags = stage2 ? 1u | (cv_NrdClamp->value >= .5f ? 2u : 0u)
        | (cv_NrdDisocclusion->value >= .5f ? 4u : 0u) : 0u;
    bool reset = in.reset || in.frameIndex != p.lastFrame + 1 || flags != p.previousFlags;
    p.previousFlags = flags;
    const auto* frame = static_cast<const PerFrameCB*>(in.perFrame.cpuPtr);
    NrdStage2Root stageConstants{};
    stageConstants.previousViewProj = p.previousViewProj;
    stageConstants.flags = flags;
    stageConstants.debugSignal = uint32_t(cv_NrdDebug->value);
    p.previousViewProj = frame->viewProj;
    if (p.width != in.width || p.height != in.height) {
        p.device->WaitIdle();
        RHITextureDesc desc{};
        desc.width = in.width; desc.height = in.height;
        desc.format = RHIFormat::RGBA32Float;
        desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        for (auto* textures : {&p.history, &p.guides, &p.specularHistory}) {
            for (auto& texture : *textures) {
                if (texture.IsValid()) p.device->DestroyTexture(texture);
                desc.debugName = textures == &p.history ? "NRD_History" : "NRD_GuideHistory";
                texture = p.device->CreateTexture(desc);
                if (!texture.IsValid()) return {};
            }
        }
        p.width = in.width; p.height = in.height;
        reset = true;
    }
    p.lastFrame = in.frameIndex;
    const uint32_t slot = in.frameIndex & 1u;
    const std::array<rg::RgResourceId, 2> histories = {
        builder.ImportTrackedTexture("NRD_History", p.history[slot]),
        builder.ImportTrackedTexture("NRD_SpecularHistory", p.specularHistory[slot])};
    const std::array<rg::RgResourceId, 2> previousHistories = {
        builder.ImportTrackedTexture("NRD_Previous", p.history[slot ^ 1u]),
        builder.ImportTrackedTexture("NRD_PreviousSpecular", p.specularHistory[slot ^ 1u])};
    const auto guide = builder.ImportTrackedTexture("NRD_Guide", p.guides[slot]);
    const auto previousGuide = builder.ImportTrackedTexture("NRD_PreviousGuide", p.guides[slot ^ 1u]);
    rg::RgTextureDesc scratch{};
    scratch.width = in.width; scratch.height = in.height;
    scratch.format = RHIFormat::RGBA16Float;
    scratch.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    const std::array<rg::RgResourceId, 6> outputs = {
        builder.CreateTexture("NRD_Atrous1", scratch),
        builder.CreateTexture("NRD_Atrous2", scratch),
        builder.CreateTexture("NRD_Modulated", scratch),
        builder.CreateTexture("NRD_SpecularAtrous1", scratch),
        builder.CreateTexture("NRD_SpecularAtrous2", scratch),
        builder.CreateTexture("NRD_SpecularModulated", scratch)
    };
    for (uint32_t dispatch = 0; dispatch < (stage2 ? 8u : 4u); ++dispatch) {
        const uint32_t channel = dispatch / 4, pass = dispatch % 4;
        const auto history = histories[channel];
        const auto previous = previousHistories[channel];
        const auto output = pass == 0 ? history : outputs[channel * 3 + pass - 1];
        const auto input = pass <= 1 ? history : outputs[channel * 3 + pass - 2];
        builder.AddPass(channel == 0 ? (pass == 0 ? "NRD_Reproject" : "NRD_Atrous")
            : (pass == 0 ? "NRD_SpecularReproject" : "NRD_SpecularAtrous"), [=, this](rg::RgPassBuilder& pb) {
            pb.Read(in.raw, RHIResourceState::ShaderResource);
            pb.Read(in.diffuse, RHIResourceState::ShaderResource);
            pb.Read(in.albedo, RHIResourceState::ShaderResource);
            pb.Read(in.depth, RHIResourceState::ShaderResource);
            pb.Read(in.normal, RHIResourceState::ShaderResource);
            pb.Read(in.specular, RHIResourceState::ShaderResource);
            pb.Read(in.modulation, RHIResourceState::ShaderResource);
            pb.Read(in.virtualPosition, RHIResourceState::ShaderResource);
            pb.Read(in.pathGuide, RHIResourceState::ShaderResource);
            if (channel != 0u && pass == 3)
                pb.Read(outputs[2], RHIResourceState::ShaderResource);
            if (pass == 0) {
                pb.Read(in.velocity, RHIResourceState::ShaderResource);
                if (!reset) {
                    pb.Read(previous, RHIResourceState::ShaderResource);
                    pb.Read(previousGuide, RHIResourceState::ShaderResource);
                }
                pb.Write(guide, RHIResourceState::UnorderedAccess);
            } else pb.Read(input, RHIResourceState::ShaderResource);
            pb.Write(output, RHIResourceState::UnorderedAccess);
            pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources& res) {
#ifdef _WIN32
                auto& state = *m_impl;
                auto srv = [&](rg::RgResourceId id) {
                    return state.device->GetTextureSRVIndex(res.GetTexture(id));
                };
                NrdRoot constants{};
                auto extra = stageConstants;
                extra.specularSrv = srv(in.specular);
                extra.modulationSrv = srv(in.modulation);
                extra.virtualPositionSrv = srv(in.virtualPosition);
                extra.pathGuideSrv = srv(in.pathGuide);
                extra.channel = channel;
                if (channel != 0u && pass == 3) extra.filteredDiffuseSrv = srv(outputs[2]);
                constants.rawSrv = srv(in.raw); constants.diffuseSrv = srv(in.diffuse);
                constants.albedoSrv = srv(in.albedo); constants.depthSrv = srv(in.depth);
                constants.normalSrv = srv(in.normal);
                constants.width = in.width; constants.height = in.height;
                constants.outputUav = state.device->GetTextureUAVIndex(res.GetTexture(output));
                if (pass == 0) {
                    constants.velocitySrv = srv(in.velocity);
                    constants.historyValid = !reset;
                    if (!reset) {
                        constants.historySrv = srv(previous);
                        constants.guideSrv = srv(previousGuide);
                    }
                    constants.guideUav = state.device->GetTextureUAVIndex(res.GetTexture(guide));
                } else {
                    constants.inputSrv = srv(input);
                    constants.stepWidth = 1u << (pass - 1);
                    constants.remodulate = pass == 3;
                }
                auto* native = static_cast<DX12CommandList*>(cmd)->GetNative();
                native->SetComputeRootSignature(state.root.Get());
                native->SetComputeRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
                native->SetComputeRoot32BitConstants(1, sizeof(extra) / 4, &extra, 0);
                native->SetComputeRootConstantBufferView(2,
                    state.device->GetBufferGPUAddress(in.perFrame.bufferHandle) + in.perFrame.offsetBytes);
                native->SetPipelineState(state.pipelines[pass == 0 ? 0 : 1].Get());
                native->Dispatch((in.width + 7) / 8, (in.height + 7) / 8, 1);
                native->SetComputeRootSignature(static_cast<DX12Device*>(state.device)->GetRootSignature());
                if (in.frameIndex < 3 || in.frameIndex % 32 == 0 || reset)
                    UZ_LOG(Renderer, Info, "NRD stage {}: frame={} pass={} channel={} history={} step={} flags={}",
                        stage2 ? 2 : 1, in.frameIndex, pass == 0 ? "reproject" : "a-trous",
                        channel == 0 ? "diffuse" : "specular", !reset, constants.stepWidth, flags);
#endif
            });
        });
    }
    return outputs[stage2 ? 5 : 2];
}
} // namespace uz
