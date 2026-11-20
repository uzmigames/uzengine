#pragma once

#include "nrd_denoiser.h"
#include <memory>

namespace uz {

// Root constants of the NRD SDK adapter's own compute dispatches (pack and
// modulate). Twin of cbuffer NrdSdkRoot in nrd_sdk_common.hlsli. 56 DWORDs:
// fits the 64-DWORD root budget without a constant buffer view.
struct NrdSdkRoot {
    uint32_t rawSrv, diffuseSrv, albedoSrv, specularSrv;
    uint32_t modulationSrv, virtualPositionSrv, guideSrv, velocitySrv;
    uint32_t motionUav, normalRoughnessUav, viewZUav, diffuseInputUav;
    uint32_t specularInputUav, outputUav, width, height;
    uint32_t diffuseOutputSrv, specularOutputSrv, debugSignal, sdkFlags;
    float hitDistA, hitDistB, hitDistC, skyViewZ;
    Mat4 worldToView;
    Mat4 inverseViewProj;
};
static_assert(sizeof(NrdSdkRoot) == 224);

// NVIDIA NRD SDK adapter (REBLUR_DIFFUSE + RELAX_SPECULAR, NRD v4.17) for the
// stage-2 signal decomposition of the DXR path tracer. The MSVC-built NRD.dll
// (scripts/build_nrd.ps1) is loaded at runtime like DLSS/Brixelizer; the
// engine never links it. Init creates the NRD instance, translates its
// permanent/transient pools and compiles its embedded DXIL into compute
// PSOs; Denoise packs the signal, runs every nrd::DispatchDesc through the
// native DX12 command list and recombines the result. Opt in with r.NRD=1
// plus r.NRD.SDK=1 (--denoiser nrd-sdk); the default path never touches it.
class NrdSdkDenoiser {
public:
    NrdSdkDenoiser();
    ~NrdSdkDenoiser();
    // True when the engine was built with the NRD headers (UZ_NRD_SDK_ENABLED).
    static bool IsCompiledIn();
    bool Init(IRHIDevice* device, uint32_t width, uint32_t height);
    rg::RgResourceId Denoise(rg::RgBuilder& builder, const NrdInputs& inputs);
    void Destroy();
    uint32_t Width() const;
    uint32_t Height() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace uz
