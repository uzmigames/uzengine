#include "passes/realtime_denoiser.h"
#include "scene_renderer_frame.h"
#include "shader_manager.h"
#include "cb_ring_allocator.h"
#include "constant_buffers.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "root_slots.h"
#include "rhi.h"
#include <cstring>

namespace uz {
bool RealtimeDenoiser::Prepare(IRHIDevice* device, ShaderManager* shaders,
                              uint32_t width, uint32_t height) {
    if (!device || !device->IsBindless() || !width || !height) return false;
    if (m_device && (width != m_width || height != m_height)) DestroyResources();
    m_device = device;
    if (!m_temporal.IsValid()) {
        m_temporalShader = shaders->CreateShader(EngineShader::RealtimeTemporalCS, RHIShaderStage::Compute);
        m_spatialShader = shaders->CreateShader(EngineShader::RealtimeSpatialCS, RHIShaderStage::Compute);
        if (!m_temporalShader.IsValid() || !m_spatialShader.IsValid()) return false;
        RHIComputePipelineDesc desc{};
        desc.computeShader = m_temporalShader; desc.debugName = "RealtimeDenoise_Temporal";
        m_temporal = device->CreateComputePipeline(desc);
        desc.computeShader = m_spatialShader; desc.debugName = "RealtimeDenoise_Spatial";
        m_spatial = device->CreateComputePipeline(desc);
    }
    if (!m_temporal.IsValid() || !m_spatial.IsValid()) return false;
    if (m_width == width && m_height == height) return true;
    for (uint32_t i = 0; i < 2; ++i) {
        // RGBA32F signals/moments avoid fp16 HDR overflow, including squared luminance.
        Texture* textures[] = {&m_color[i], &m_moments[i], &m_geometry[i], &m_guides[i]};
        const RHIFormat formats[] = {RHIFormat::RGBA32Float, RHIFormat::RGBA32Float,
                                    RHIFormat::RGBA32Float, RHIFormat::RG32Float};
        for (uint32_t j = 0; j < 4; ++j) {
            RHITextureDesc desc{};
            desc.width = width; desc.height = height; desc.format = formats[j];
            desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
            desc.debugName = "RealtimeDenoise_History";
            textures[j]->handle = device->CreateTexture(desc);
            if (!textures[j]->handle.IsValid()) { DestroyResources(); return false; }
            textures[j]->srv = device->GetTextureSRVIndex(textures[j]->handle);
            textures[j]->uav = device->GetTextureUAVIndex(textures[j]->handle);
        }
    }
    m_width = width; m_height = height;
    return true;
}

void RealtimeDenoiser::DestroyResources() {
    if (!m_device) return;
    for (uint32_t i = 0; i < 2; ++i) {
        for (Texture* texture : {&m_color[i], &m_moments[i], &m_geometry[i], &m_guides[i]}) {
            if (texture->handle.IsValid()) m_device->DestroyTexture(texture->handle);
            *texture = {};
        }
    }
    if (m_temporal.IsValid()) m_device->DestroyPipeline(m_temporal);
    if (m_spatial.IsValid()) m_device->DestroyPipeline(m_spatial);
    if (m_temporalShader.IsValid()) m_device->DestroyShader(m_temporalShader);
    if (m_spatialShader.IsValid()) m_device->DestroyShader(m_spatialShader);
    m_temporal = {}; m_spatial = {}; m_temporalShader = {}; m_spatialShader = {};
    m_width = m_height = m_write = 0;
    m_lastFrame = UINT32_MAX; m_produced = false; m_device = nullptr;
}

void RealtimeDenoiser::Register(rg::RgBuilder& graph, FrameContext& frame, CBRingAllocator* ring,
                              rg::RgResourceId signal, uint32_t signalSrv, uint32_t signalUav,
                              uint32_t depthSrv, uint32_t normalSrv, uint32_t velocitySrv,
                              std::function<uint64_t()> epoch, const Policy& policy) {
    const uint32_t write = m_write, read = 1 - write;
    rg::RgResourceId prior[4], next[4];
    const Texture* old[] = {&m_color[read], &m_moments[read], &m_geometry[read], &m_guides[read]};
    const Texture* fresh[] = {&m_color[write], &m_moments[write], &m_geometry[write], &m_guides[write]};
    for (uint32_t i = 0; i < 4; ++i) {
        prior[i] = graph.ImportTrackedTexture("RealtimeDenoise_Previous", old[i]->handle);
        next[i] = graph.ImportTrackedTexture("RealtimeDenoise_Current", fresh[i]->handle);
    }
    rg::RgTextureDesc scratchDesc{};
    scratchDesc.width = m_width; scratchDesc.height = m_height;
    scratchDesc.format = RHIFormat::RGBA32Float;
    scratchDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    const auto scratch = graph.CreateTexture("RealtimeDenoise_Reconstruction", scratchDesc);
    scratchDesc.format = RHIFormat::RG32Float;
    const auto diagnostics = graph.CreateTexture("RealtimeDenoise_ClampAndRejectedTaps", scratchDesc);
    using Root = RealtimeDenoiseRoot;
    using Config = RealtimeDenoiseConfig;
    const Root root{signalSrv, depthSrv, normalSrv, velocitySrv,
                    m_color[read].srv, m_moments[read].srv, m_geometry[read].srv, m_guides[read].srv,
                    m_color[write].uav, m_moments[write].uav, m_geometry[write].uav, m_guides[write].uav,
                    0, m_width, m_height, 0, 0};
    m_produced = false;
    graph.AddPass("RealtimeDenoise_Temporal", [=, this, &frame](rg::RgPassBuilder& pb) {
        pb.Read(signal, RHIResourceState::ShaderResource);
        pb.Read(frame.depthRes, RHIResourceState::ShaderResource);
        pb.Read(frame.normalRes, RHIResourceState::ShaderResource);
        pb.Read(frame.velocityRes, RHIResourceState::ShaderResource);
        for (uint32_t i = 0; i < 4; ++i) {
            pb.Read(prior[i], RHIResourceState::ShaderResource);
            pb.Write(next[i], RHIResourceState::UnorderedAccess);
        }
        pb.Write(scratch, RHIResourceState::UnorderedAccess);
        pb.Write(diagnostics, RHIResourceState::UnorderedAccess);
        pb.SetExecutionCallback([=, this, &frame](IRHICommandList* cmd, const rg::RgPassResources& resources) {
            Root rc = root;
            rc.outputUav = m_device->GetTextureUAVIndex(resources.GetTexture(scratch));
            rc.diagnosticsUav = m_device->GetTextureUAVIndex(resources.GetTexture(diagnostics));
            // TAA-off reports a content FrameGap every frame; it is not a gap in
            // this client's own history. Explicit scene/quality/failure resets are.
            const bool reset = !frame.temporal.matrixValid
                || (frame.temporal.contentReason != TemporalReset::None
                    && frame.temporal.contentReason != TemporalReset::FrameGap);
            rc.historyValid = RealtimeHistoryReusable(m_lastFrame,frame.frameIndex,m_epoch,epoch(),reset);
            Config config{{m_previousJitterX,m_previousJitterY},{frame.jitterX,frame.jitterY},
                          policy.minCurrentWeight,policy.normalPower,policy.spatialRadiusScale,policy.mirrorRoughness};
            auto alloc = ring->Allocate(sizeof(config));
            if (!alloc.cpuPtr) return;
            std::memcpy(alloc.cpuPtr,&config,sizeof(config));
            cmd->SetPipeline(m_temporal);
            cmd->SetRootConstants(kSlotRootConstants,reinterpret_cast<const uint32_t*>(&rc),sizeof(rc)/4);
            cmd->SetRootCBV(kSlotPerFrame,frame.perFrameAlloc.bufferHandle,frame.perFrameAlloc.offsetBytes,
                            frame.perFrameAlloc.cpuPtr,sizeof(PerFrameCB));
            cmd->SetRootCBV(kSlotPerObject,alloc.bufferHandle,alloc.offsetBytes,alloc.cpuPtr,sizeof(config));
            cmd->Dispatch((m_width+7)/8,(m_height+7)/8,1);
            m_produced = true;
        });
    });
    graph.AddPass("RealtimeDenoise_Spatial", [=, this, &frame](rg::RgPassBuilder& pb) {
        pb.Read(scratch,RHIResourceState::ShaderResource);
        pb.Read(frame.depthRes,RHIResourceState::ShaderResource);
        pb.Read(frame.normalRes,RHIResourceState::ShaderResource);
        pb.Write(signal,RHIResourceState::UnorderedAccess);
        pb.SetExecutionCallback([=, this, &frame](IRHICommandList* cmd, const rg::RgPassResources& resources) {
            if (!m_produced) return;
            Root rc = root;
            rc.inputSrv = m_device->GetTextureSRVIndex(resources.GetTexture(scratch));
            rc.outputUav = signalUav;
            Config config{{m_previousJitterX,m_previousJitterY},{frame.jitterX,frame.jitterY},
                          policy.minCurrentWeight,policy.normalPower,policy.spatialRadiusScale,policy.mirrorRoughness};
            auto alloc = ring->Allocate(sizeof(config));
            if (!alloc.cpuPtr) { m_produced = false; return; }
            std::memcpy(alloc.cpuPtr,&config,sizeof(config));
            cmd->SetPipeline(m_spatial);
            cmd->SetRootConstants(kSlotRootConstants,reinterpret_cast<const uint32_t*>(&rc),sizeof(rc)/4);
            cmd->SetRootCBV(kSlotPerFrame,frame.perFrameAlloc.bufferHandle,frame.perFrameAlloc.offsetBytes,
                            frame.perFrameAlloc.cpuPtr,sizeof(PerFrameCB));
            cmd->SetRootCBV(kSlotPerObject,alloc.bufferHandle,alloc.offsetBytes,alloc.cpuPtr,sizeof(config));
            cmd->Dispatch((m_width+7)/8,(m_height+7)/8,1);
            m_lastFrame = frame.frameIndex; m_epoch = epoch(); m_write = read;
            m_previousJitterX = frame.jitterX; m_previousJitterY = frame.jitterY;
        });
    });
}
} // namespace uz
