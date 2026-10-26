#pragma once

#include "rhi_types.h"
#include "render_graph_types.h"
#include <cstdint>
#include <functional>

namespace uz {
class IRHIDevice;
class ShaderManager;
class CBRingAllocator;
struct FrameContext;
namespace rg { class RgBuilder; }

struct RealtimeDenoiseRoot {
    uint32_t inputSrv, depthSrv, normalSrv, velocitySrv;
    uint32_t historySrv, momentsSrv, geometrySrv, guideSrv;
    uint32_t historyUav, momentsUav, geometryUav, guideUav;
    uint32_t outputUav, width, height, historyValid, diagnosticsUav;
};
static_assert(sizeof(RealtimeDenoiseRoot) == 17 * sizeof(uint32_t));
static_assert(sizeof(RealtimeDenoiseRoot) <= 24 * sizeof(uint32_t));
struct RealtimeDenoiseConfig {
    float previousJitter[2], currentJitter[2];
    float minCurrentWeight, normalPower, spatialRadiusScale, mirrorRoughness;
};
static_assert(sizeof(RealtimeDenoiseConfig) == 32);

inline bool RealtimeHistoryReusable(uint32_t previousFrame, uint32_t frame,
                                    uint64_t previousEpoch, uint64_t epoch, bool reset) {
    return previousFrame != UINT32_MAX && previousFrame + 1 == frame
        && previousEpoch == epoch && !reset;
}

/// Signal-local histories with reusable receiver guides and reconstruction policy.
/// Stage 1 has exactly one client: full-resolution standalone RT radiance.
class RealtimeDenoiser {
public:
    struct Policy {
        float minCurrentWeight = 0.05f;
        float normalPower = 128.f;
        float spatialRadiusScale = 1.f;
        float mirrorRoughness = 0.04f;
    };
    bool Prepare(IRHIDevice* device, ShaderManager* shaders, uint32_t width, uint32_t height);
    void Register(rg::RgBuilder& graph, FrameContext& frame, CBRingAllocator* ring,
                  rg::RgResourceId signal, uint32_t signalSrv, uint32_t signalUav,
                  uint32_t depthSrv, uint32_t normalSrv, uint32_t velocitySrv,
                  std::function<uint64_t()> epoch, const Policy& policy);
    void DestroyResources();
    bool HasProducedResult() const { return m_produced; }
private:
    struct Texture {
        RHITextureHandle handle;
        uint32_t srv = UINT32_MAX, uav = UINT32_MAX;
    };
    IRHIDevice* m_device = nullptr;
    Texture m_color[2], m_moments[2], m_geometry[2], m_guides[2];
    RHIShaderHandle m_temporalShader, m_spatialShader;
    RHIPipelineHandle m_temporal, m_spatial;
    uint32_t m_width = 0, m_height = 0, m_write = 0, m_lastFrame = UINT32_MAX;
    uint64_t m_epoch = 0;
    float m_previousJitterX = 0.f, m_previousJitterY = 0.f;
    bool m_produced = false;
};
} // namespace uz
