#include "nrd_denoiser.h"
#include "render_graph_builder.h"
#include "rhi.h"
#include "log.h"
#include <array>
#ifdef _WIN32
#include "engine/rhi/dx12/dx12_device.h"
#include "engine/rhi/dx12/dx12_command_list.h"
#include "engine_shaders.h"
#include "shader_compiler.h"
#endif

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {
struct NrdDenoiser::Impl {
    IRHIDevice* device = nullptr;
    std::array<RHITextureHandle, 2> history{}, guides{};
    uint32_t width = 0, height = 0, lastFrame = UINT32_MAX;
#ifdef _WIN32
    ComPtr<ID3D12RootSignature> root;
    std::array<ComPtr<ID3D12PipelineState>, 2> pipelines;
#endif
    ~Impl() {
        if (!device) return;
        device->WaitIdle();
        for (auto textures : {history, guides})
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
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants = {0, 0, sizeof(NrdRoot) / sizeof(uint32_t)};
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
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
            UZ_LOG(Renderer, Error, "NRD stage 1: compile failed for '{}': {}",
                   shader.sourceName, result.errors);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = p.root.Get();
        pipeline.CS = {result.bytecode.data(), result.bytecode.size()};
        hr = dx->GetDevice()->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&p.pipelines[i]));
        if (FAILED(hr)) return false;
        UZ_LOG(Renderer, Info, "NRD stage 1: '{}' compiled cs_6_6; PSO ready", shader.sourceName);
    }
    return true;
#else
    return false;
#endif
}

rg::RgResourceId NrdDenoiser::Denoise(rg::RgBuilder& builder, const NrdInputs& in) {
    auto& p = *m_impl;
    bool reset = in.reset || in.frameIndex != p.lastFrame + 1;
    if (p.width != in.width || p.height != in.height) {
        p.device->WaitIdle();
        RHITextureDesc desc{};
        desc.width = in.width; desc.height = in.height;
        desc.format = RHIFormat::RGBA32Float;
        desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        for (auto* textures : {&p.history, &p.guides}) {
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
    const auto history = builder.ImportTrackedTexture("NRD_History", p.history[slot]);
    const auto previous = builder.ImportTrackedTexture("NRD_Previous", p.history[slot ^ 1u]);
    const auto guide = builder.ImportTrackedTexture("NRD_Guide", p.guides[slot]);
    const auto previousGuide = builder.ImportTrackedTexture("NRD_PreviousGuide", p.guides[slot ^ 1u]);
    rg::RgTextureDesc scratch{};
    scratch.width = in.width; scratch.height = in.height;
    scratch.format = RHIFormat::RGBA16Float;
    scratch.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    const std::array<rg::RgResourceId, 3> outputs = {
        builder.CreateTexture("NRD_Atrous1", scratch),
        builder.CreateTexture("NRD_Atrous2", scratch),
        builder.CreateTexture("NRD_Modulated", scratch)
    };
    for (uint32_t pass = 0; pass < 4; ++pass) {
        const auto output = pass == 0 ? history : outputs[pass - 1];
        const auto input = pass <= 1 ? history : outputs[pass - 2];
        builder.AddPass(pass == 0 ? "NRD_Reproject" : "NRD_Atrous", [=, this](rg::RgPassBuilder& pb) {
            pb.Read(in.raw, RHIResourceState::ShaderResource);
            pb.Read(in.diffuse, RHIResourceState::ShaderResource);
            pb.Read(in.albedo, RHIResourceState::ShaderResource);
            pb.Read(in.depth, RHIResourceState::ShaderResource);
            pb.Read(in.normal, RHIResourceState::ShaderResource);
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
                native->SetPipelineState(state.pipelines[pass == 0 ? 0 : 1].Get());
                native->Dispatch((in.width + 7) / 8, (in.height + 7) / 8, 1);
                native->SetComputeRootSignature(static_cast<DX12Device*>(state.device)->GetRootSignature());
                if (in.frameIndex < 3 || in.frameIndex % 32 == 0 || reset)
                    UZ_LOG(Renderer, Info, "NRD stage 1: frame={} pass={} history={} step={}",
                        in.frameIndex, pass == 0 ? "reproject" : "a-trous", !reset, constants.stepWidth);
#endif
            });
        });
    }
    return outputs.back();
}
} // namespace uz
