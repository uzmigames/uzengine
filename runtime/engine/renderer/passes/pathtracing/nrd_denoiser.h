#pragma once

#include "render_graph_types.h"
#include "constant_buffers.h"
#include <memory>

namespace uz {
class IRHIDevice;
namespace rg { class RgBuilder; }

// Separate from the unchanged 24-DWORD PathTracerRoot. Only DXR signal
// extraction consumes b2; the denoiser has its own compute root signature.
struct NrdSignalRoot {
    uint32_t enabled, diffuseUav, fractionUav, albedoUav;
    uint32_t specularUav, modulationUav, virtualPositionUav, guideUav;
};

struct NrdRoot {
    uint32_t rawSrv, diffuseSrv, albedoSrv, depthSrv;
    uint32_t normalSrv, velocitySrv, historySrv, guideSrv;
    uint32_t outputUav, guideUav, width, height;
    uint32_t historyValid, inputSrv, stepWidth, remodulate;
};
static_assert(sizeof(NrdRoot) == 64);
static_assert(sizeof(NrdSignalRoot) == 32);

struct NrdStage2Root {
    uint32_t specularSrv, modulationSrv, virtualPositionSrv, channel;
    uint32_t filteredDiffuseSrv, flags, pathGuideSrv, debugSignal;
    Mat4 previousViewProj;
};
static_assert(sizeof(NrdStage2Root) == 96);

struct NrdInputs {
    rg::RgResourceId raw, diffuse, albedo, depth, normal, velocity;
    rg::RgResourceId specular, modulation, virtualPosition, pathGuide;
    CBAllocation perFrame;
    uint32_t width, height, frameIndex;
    bool reset;
};

// Separate diffuse/specular temporal and edge-aware a-trous reconstruction.
// Primary emission and background remain the unmodified residual.
// This is the native NRD-style baseline, not the NVIDIA SDK dispatch adapter.
class NrdDenoiser {
public:
    NrdDenoiser();
    ~NrdDenoiser();
    bool Init(IRHIDevice* device);
    rg::RgResourceId Denoise(rg::RgBuilder& builder, const NrdInputs& inputs);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace uz
