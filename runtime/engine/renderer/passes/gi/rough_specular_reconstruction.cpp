#include "passes/rough_specular_reconstruction.h"
#include "passes/realtime_denoiser.h"
#include "scene_renderer_frame.h"
#include "shader_manager.h"
#include "cb_ring_allocator.h"
#include "constant_buffers.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "render_graph_states.h"
#include "root_slots.h"
#include "rhi.h"
#include <cstring>

namespace uz {
namespace {
struct RoughSpecularRoot {
    uint32_t inputSrv, depthSrv, normalSrv, velocitySrv;
    uint32_t hitDistanceSrv, historySrv, momentsSrv, geometrySrv;
    uint32_t guidesSrv, colorUav, momentsUav, geometryUav;
    uint32_t guidesUav, width, height, historyValid;
    uint32_t outputUav;
};
static_assert(sizeof(RoughSpecularRoot) == 17 * sizeof(uint32_t));
struct RoughSpecularConfig {
    float previousJitter[2], currentJitter[2];
};
}

bool RoughSpecularReconstruction::Prepare(IRHIDevice* device, ShaderManager* shaders,
                                          uint32_t width, uint32_t height) {
    if (!device || !device->IsBindless() || !width || !height) return false;
    if (m_device && (m_width != width || m_height != height)) DestroyResources();
    m_device = device;
    if (!m_reconstruct.IsValid()) {
        m_reconstructShader = shaders->CreateShader(EngineShader::RoughSpecularReconstructCS, RHIShaderStage::Compute);
        m_applyShader = shaders->CreateShader(EngineShader::RoughSpecularApplyCS, RHIShaderStage::Compute);
        if (!m_reconstructShader.IsValid() || !m_applyShader.IsValid()) return false;
        RHIComputePipelineDesc desc{};
        desc.computeShader = m_reconstructShader; desc.debugName = "RoughSpecular_Reconstruct";
        m_reconstruct = device->CreateComputePipeline(desc);
        desc.computeShader = m_applyShader; desc.debugName = "RoughSpecular_Apply";
        m_apply = device->CreateComputePipeline(desc);
    }
    if (!m_reconstruct.IsValid() || !m_apply.IsValid()) return false;
    if (m_width == width && m_height == height) return true;
    for (uint32_t i = 0; i < 2; ++i) {
        for (Texture* t : {&m_color[i], &m_moments[i], &m_geometry[i], &m_guides[i]}) {
            RHITextureDesc desc{};
            desc.width = width; desc.height = height;
            desc.format = RHIFormat::RGBA32Float;
            desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
            desc.debugName = "RoughSpecular_HistoryAndGuides";
            t->handle = device->CreateTexture(desc);
            if (!t->handle.IsValid()) { DestroyResources(); return false; }
            t->srv = device->GetTextureSRVIndex(t->handle);
            t->uav = device->GetTextureUAVIndex(t->handle);
        }
    }
    m_width = width; m_height = height;
    return true;
}

void RoughSpecularReconstruction::DestroyResources() {
    if (!m_device) return;
    for (uint32_t i = 0; i < 2; ++i) {
        for (Texture* t : {&m_color[i], &m_moments[i], &m_geometry[i], &m_guides[i]}) {
            if (t->handle.IsValid()) m_device->DestroyTexture(t->handle);
            *t = {};
        }
    }
    if (m_reconstruct.IsValid()) m_device->DestroyPipeline(m_reconstruct);
    if (m_apply.IsValid()) m_device->DestroyPipeline(m_apply);
    if (m_reconstructShader.IsValid()) m_device->DestroyShader(m_reconstructShader);
    if (m_applyShader.IsValid()) m_device->DestroyShader(m_applyShader);
    m_reconstruct = {}; m_apply = {}; m_reconstructShader = {}; m_applyShader = {};
    m_width = m_height = m_write = 0;
    m_lastFrame = m_registeredFrame = UINT32_MAX;
    m_produced = false; m_device = nullptr;
}

void RoughSpecularReconstruction::Register(rg::RgBuilder& graph, FrameContext& frame,
    CBRingAllocator* ring, rg::RgResourceId signal, uint32_t signalSrv, uint32_t depthSrv,
    uint32_t normalSrv, uint32_t velocitySrv, uint32_t hitDistanceSrv,
    std::function<uint64_t()> epoch) {
    const uint32_t write = m_write, read = 1 - write;
    m_result = write;
    m_registeredFrame = frame.frameIndex;
    m_produced = false;
    const Texture* prior[] = {&m_color[read], &m_moments[read], &m_geometry[read], &m_guides[read]};
    const Texture* next[] = {&m_color[write], &m_moments[write], &m_geometry[write], &m_guides[write]};
    rg::RgResourceId oldIds[4], newIds[4];
    for (uint32_t i = 0; i < 4; ++i) {
        oldIds[i] = graph.ImportTrackedTexture("RoughSpecular_Previous", prior[i]->handle);
        newIds[i] = graph.ImportTrackedTexture("RoughSpecular_Current", next[i]->handle);
    }
    const RoughSpecularRoot root{signalSrv, depthSrv, normalSrv, velocitySrv, hitDistanceSrv,
        m_color[read].srv, m_moments[read].srv, m_geometry[read].srv, m_guides[read].srv,
        m_color[write].uav, m_moments[write].uav, m_geometry[write].uav, m_guides[write].uav,
        m_width, m_height, 0, 0};
    graph.AddPass("RoughSpecular_Reconstruct", [=, this, &frame](rg::RgPassBuilder& pb) {
        // SSR owns manual barriers for its raw signal and hit-distance buffers.
        // Only the standalone unified route imports the signal into the graph.
        if (signal.IsValid()) pb.Read(signal, RHIResourceState::ShaderResource);
        pb.Read(frame.depthRes, RHIResourceState::ShaderResource);
        pb.Read(frame.normalRes, RHIResourceState::ShaderResource);
        pb.Read(frame.velocityRes, RHIResourceState::ShaderResource);
        for (uint32_t i = 0; i < 4; ++i) {
            pb.Read(oldIds[i], RHIResourceState::ShaderResource);
            pb.Write(newIds[i], RHIResourceState::UnorderedAccess);
        }
        pb.SetExecutionCallback([=, this, &frame](IRHICommandList* cmd, const rg::RgPassResources&) {
            RoughSpecularRoot rc = root;
            const uint64_t currentEpoch = epoch();
            const bool reset = !frame.temporal.matrixValid
                || (frame.temporal.contentReason != TemporalReset::None
                    && frame.temporal.contentReason != TemporalReset::FrameGap);
            rc.historyValid = RealtimeHistoryReusable(m_lastFrame, frame.frameIndex, m_epoch, currentEpoch, reset);
            RoughSpecularConfig config{{m_previousJitterX, m_previousJitterY}, {frame.jitterX, frame.jitterY}};
            auto alloc = ring->Allocate(sizeof(config));
            if (!alloc.cpuPtr) return;
            std::memcpy(alloc.cpuPtr, &config, sizeof(config));
            cmd->SetPipeline(m_reconstruct);
            cmd->SetRootConstants(kSlotRootConstants, reinterpret_cast<const uint32_t*>(&rc), sizeof(rc)/4);
            cmd->SetRootCBV(kSlotPerFrame, frame.perFrameAlloc.bufferHandle, frame.perFrameAlloc.offsetBytes,
                           frame.perFrameAlloc.cpuPtr, sizeof(PerFrameCB));
            cmd->SetRootCBV(kSlotPerObject, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr, sizeof(config));
            cmd->Dispatch((m_width+7)/8, (m_height+7)/8, 1);
            m_produced = true;
            m_lastFrame = frame.frameIndex; m_epoch = currentEpoch;
            m_previousJitterX = frame.jitterX; m_previousJitterY = frame.jitterY;
            m_write = read;
        });
    });
}

void RoughSpecularReconstruction::Apply(rg::RgBuilder& graph, FrameContext& frame,
    rg::RgResourceId signal, RHITextureHandle signalTexture, uint32_t signalUav,
    uint32_t normalSrv, rg::RgStateRegistry* states) {
    if (m_registeredFrame != frame.frameIndex) return;
    const auto color = graph.ImportTrackedTexture("RoughSpecular_Result", m_color[m_result].handle);
    const auto guides = graph.ImportTrackedTexture("RoughSpecular_ResultGuides", m_guides[m_result].handle);
    const uint32_t result = m_result;
    graph.AddPass("RoughSpecular_Apply", [=, this, &frame](rg::RgPassBuilder& pb) {
        pb.Read(color, RHIResourceState::ShaderResource);
        pb.Read(guides, RHIResourceState::ShaderResource);
        pb.Read(frame.depthRes, RHIResourceState::ShaderResource);
        pb.Read(frame.normalRes, RHIResourceState::ShaderResource);
        if (signal.IsValid()) pb.Write(signal, RHIResourceState::UnorderedAccess);
        pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources&) {
            if (!m_produced) return;
            if (!signal.IsValid()) states->Transition(cmd, signalTexture, RHIResourceState::UnorderedAccess);
            RoughSpecularRoot rc{};
            rc.historySrv = m_color[result].srv; rc.guidesSrv = m_guides[result].srv;
            rc.normalSrv = normalSrv; rc.outputUav = signalUav;
            rc.width = m_width; rc.height = m_height;
            cmd->SetPipeline(m_apply);
            cmd->SetRootConstants(kSlotRootConstants, reinterpret_cast<const uint32_t*>(&rc), sizeof(rc)/4);
            cmd->Dispatch((m_width+7)/8, (m_height+7)/8, 1);
            if (!signal.IsValid()) states->Transition(cmd, signalTexture, RHIResourceState::ShaderResource);
        });
    });
}
} // namespace uz
