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
};

struct NrdRoot {
    uint32_t rawSrv, diffuseSrv, albedoSrv, depthSrv;
    uint32_t normalSrv, velocitySrv, historySrv, guideSrv;
    uint32_t outputUav, guideUav, width, height;
    uint32_t historyValid, inputSrv, stepWidth, remodulate;
};
static_assert(sizeof(NrdRoot) == 64);
static_assert(sizeof(NrdSignalRoot) == 16);

struct NrdInputs {
    rg::RgResourceId raw, diffuse, albedo, depth, normal, velocity;
    CBAllocation perFrame;
    uint32_t width, height, frameIndex;
    bool reset;
};

// Stage 1: diffuse demodulation, validated temporal reuse, and edge-aware
// a-trous reconstruction. Specular/emission remain the unmodified residual.
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
