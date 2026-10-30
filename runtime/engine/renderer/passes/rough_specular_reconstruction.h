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
namespace rg { class RgBuilder; class RgStateRegistry; }

/// Opt-in rough-only reconstruction. The legacy denoisers still own mirror pixels.
class RoughSpecularReconstruction {
public:
    bool Prepare(IRHIDevice* device, ShaderManager* shaders, uint32_t width, uint32_t height);
    void Register(rg::RgBuilder& graph, FrameContext& frame, CBRingAllocator* ring,
                  rg::RgResourceId signal, uint32_t signalSrv, uint32_t depthSrv,
                  uint32_t normalSrv, uint32_t velocitySrv, uint32_t hitDistanceSrv,
                  std::function<uint64_t()> epoch);
    void Apply(rg::RgBuilder& graph, FrameContext& frame,
               rg::RgResourceId signal, RHITextureHandle signalTexture, uint32_t signalUav,
               uint32_t normalSrv, rg::RgStateRegistry* states);
    void DestroyResources();
private:
    struct Texture {
        RHITextureHandle handle;
        uint32_t srv = UINT32_MAX, uav = UINT32_MAX;
    };
    IRHIDevice* m_device = nullptr;
    Texture m_color[2], m_moments[2], m_geometry[2], m_guides[2];
    RHIShaderHandle m_reconstructShader, m_applyShader;
    RHIPipelineHandle m_reconstruct, m_apply;
    uint32_t m_width = 0, m_height = 0, m_write = 0, m_lastFrame = UINT32_MAX;
    uint32_t m_registeredFrame = UINT32_MAX, m_result = 0;
    uint64_t m_epoch = 0;
    float m_previousJitterX = 0.f, m_previousJitterY = 0.f;
    bool m_produced = false;
};
} // namespace uz
