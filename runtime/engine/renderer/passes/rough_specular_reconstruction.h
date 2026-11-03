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
    /// Motion-aware reconstruction (docs/analysis/lumen-radiance-cache-stability/09): the
    /// UZ_ROUGH_MOTION_AWARE=1 permutation and its tunables, read from the CVars each frame.
    struct MotionAware {
        bool enabled = false;
        float fireflyK = 8.f, shortHistory = 8.f, parallaxPower = 2.f, curvatureGate = 1.f;
        float debugMode = 0.f;   ///< diagnostics in place of the colour (the .Debug CVar), 0 = off
    };
    /// `motionAware` compiles the permutation lazily; on failure it logs once and the OFF pipeline runs.
    bool Prepare(IRHIDevice* device, ShaderManager* shaders, uint32_t width, uint32_t height,
                 bool motionAware);
    void Register(rg::RgBuilder& graph, FrameContext& frame, CBRingAllocator* ring,
                  rg::RgResourceId signal, uint32_t signalSrv, uint32_t depthSrv,
                  uint32_t normalSrv, uint32_t velocitySrv, uint32_t hitDistanceSrv,
                  std::function<uint64_t()> epoch, const MotionAware& motionAware);
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
    RHIShaderHandle m_reconstructShader, m_applyShader, m_reconstructMotionAwareShader;
    RHIPipelineHandle m_reconstruct, m_apply, m_reconstructMotionAware;
    uint32_t m_width = 0, m_height = 0, m_write = 0, m_lastFrame = UINT32_MAX;
    uint32_t m_registeredFrame = UINT32_MAX, m_result = 0;
    uint64_t m_epoch = 0;
    float m_previousJitterX = 0.f, m_previousJitterY = 0.f;
    float m_previousCameraPos[3] = {0.f, 0.f, 0.f};
    bool m_produced = false;
    bool m_motionAwareFailed = false;
};
} // namespace uz
