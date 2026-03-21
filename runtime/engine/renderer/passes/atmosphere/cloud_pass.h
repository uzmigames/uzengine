/// UzEngine Cloud Pass — STUB (reimplementing from scratch)
#pragma once

#include "engine/rhi/rhi_types.h"
#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class CBRingAllocator;

struct CloudPassInput {
    float    invViewProj[16];
    float    prevViewProj[16];
    float    cameraPos[3];
    float    sunDir[3];
    float    sunColor[3];
    float    ambientColor[3];
    float    time;
    float    coverage;
    float    density;
    float    extinction;
    float    scatteringAlbedo;
    float    phaseG;
    float    phaseG2;
    float    phaseBlend;
    float    layerBottomKm;
    float    layerHeightKm;
    float    windVelocity[3];
    float    detailWindScale;
    float    ambientMultiplier;
    uint32_t primarySteps;
    uint32_t lightSteps;
    uint32_t depthSrv;
    float    globalCoverage;
    float    temporalBlendFactor;
};

class CloudPass {
public:
    CloudPass() = default;
    ~CloudPass() = default;

    CloudPass(const CloudPass&) = delete;
    CloudPass& operator=(const CloudPass&) = delete;
    CloudPass(CloudPass&&) = delete;
    CloudPass& operator=(CloudPass&&) = delete;

    [[nodiscard]] bool Init(IRHIDevice*, ShaderManager*, uint32_t, uint32_t) { return false; }
    void Shutdown() {}
    void OnResize(uint32_t, uint32_t) {}
    void LoadNoiseTextures(IRHICommandList*) {}
    void Execute(IRHICommandList*, CBRingAllocator&, const CloudPassInput&) {}
    [[nodiscard]] uint32_t GetCloudOutputSrv() const noexcept { return UINT32_MAX; }
    [[nodiscard]] RHITextureHandle GetCloudOutputTexture() const noexcept { return {}; }
    [[nodiscard]] RHITextureHandle GetDebugRaymarchOutput(uint32_t) const noexcept { return {}; }
    [[nodiscard]] uint32_t GetDebugRaymarchOutputSrv(uint32_t) const noexcept { return UINT32_MAX; }
    [[nodiscard]] bool IsInitialized() const noexcept { return false; }
    [[nodiscard]] bool HasNoiseTextures() const noexcept { return false; }
};

} // namespace uz
