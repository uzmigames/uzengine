/// UzEngine Shadow Renderer — Implementation.
///
/// Builds the per-frame ShadowConfig with UE5-parity defaults, computes
/// cascade splits via `ComputeCascades`, and uploads ShadowDataCB.

#include "shadow_renderer.h"

#include "camera.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Shadow);

#include <cmath>
#include <cstring>

namespace uz {

ShadowFrameData SetupShadowFrame(
    const Camera& camera,
    const Vec3& sunDir,
    float perLightShadowBias,
    float perLightShadowSlopeBias,
    float lightSourceAngleDeg,
    uint32_t shadowAtlasSrv,
    uint32_t shadowResolution,
    CBRingAllocator& cbRing)
{
    ShadowFrameData result{};

    // ── Shadow config — UE5 parity parameters ────────────────────────────
    ShadowConfig& cfg = result.config;
    cfg.cascadeCount                = 4;
    cfg.resolution                  = shadowResolution;
    cfg.maxDistance                  = 40000.0f;  // 400m — UE5 default (DLC.cpp:1017)
    cfg.cascadeDistributionExponent = 3.0f;      // UE5 default (DLC.cpp:1029)
    cfg.cascadeTransitionFraction   = 0.1f;      // 10% blend overlap (DLC.cpp:1030)
    cfg.shadowDistanceFadeoutFraction = 0.1f;    // 10% fade on last cascade (DLC.cpp:1031)
    cfg.normalBias                  = 0.0f;      // UE5: NO normal bias in pixel shader. All bias in shadow VS via rasterizer state.
    cfg.csmDepthBias                = 0.0f;      // UE5: bias applied in shadow VS via rasterizer, NOT in pixel shader sampling
    cfg.slopeScaleDepthBias         = 3.0f * perLightShadowSlopeBias;  // UE5: CSMSlopeBias * per-light ShadowSlopeBias
    cfg.cascadeBiasDistribution     = 1.0f;      // Fully world-scaled (ShadowSetup.cpp:1846)
    cfg.receiverBias                = 0.9f;      // UE5 default (ShadowRendering.cpp:76)

    // ── Compute cascade splits and light-space matrices ──────────────────
    // lightDir for ComputeCascades is the direction the light shines toward
    // (opposite of sun direction). sunDir points TOWARD the sun, so we negate it.
    const Vec3 lightShineDir = Vec3{-sunDir.x, -sunDir.y, -sunDir.z};
    ComputeCascades(camera.GetView(), camera.GetProjection(), lightShineDir,
                    cfg, result.cascades);

    // DEBUG: log cascade split distances and bias to verify correctness
    static bool loggedOnce = false;
    if (!loggedOnce) {
        for (uint32_t i = 0; i < cfg.cascadeCount; ++i) {
            UZ_LOG(Shadow, Info,
                "Cascade[{}]: splitNear={:.1f} splitFar={:.1f} fadeStart={:.1f} "
                "texelSize={:.4f} depthBias={:.6f} normalBias={:.4f}",
                i,
                result.cascades[i].splitNear,
                result.cascades[i].splitFar,
                result.cascades[i].splitFarFadeStart,
                result.cascades[i].texelSize,
                result.cascades[i].computedDepthBias,
                result.cascades[i].computedNormalBias);
        }
        loggedOnce = true;
    }

    // ── Upload ShadowDataCB — UE5 parity ─────────────────────────────────
    ShadowDataCB shadowData{};
    for (uint32_t i = 0; i < cfg.cascadeCount; ++i) {
        shadowData.cascadeViewProj[i] = result.cascades[i].viewProj;
    }

    // Extended far splits (include blend overlap)
    shadowData.cascadeSplits = {
        result.cascades[0].splitFar,
        result.cascades[1].splitFar,
        result.cascades[2].splitFar,
        result.cascades[3].splitFar
    };

    // Original un-extended far splits (blend start points)
    shadowData.cascadeFadeStarts = {
        result.cascades[0].splitFarFadeStart,
        result.cascades[1].splitFarFadeStart,
        result.cascades[2].splitFarFadeStart,
        result.cascades[3].splitFarFadeStart
    };

    // Per-cascade computed biases
    shadowData.cascadeDepthBias = {
        result.cascades[0].computedDepthBias,
        result.cascades[1].computedDepthBias,
        result.cascades[2].computedDepthBias,
        result.cascades[3].computedDepthBias
    };
    shadowData.cascadeNormalBias = {
        result.cascades[0].computedNormalBias,
        result.cascades[1].computedNormalBias,
        result.cascades[2].computedNormalBias,
        result.cascades[3].computedNormalBias
    };

    shadowData.shadowAtlasIndex = shadowAtlasSrv;
    shadowData.cascadeCount     = cfg.cascadeCount;
    shadowData.shadowMapSize    = static_cast<float>(shadowResolution);
    shadowData.receiverBias     = cfg.receiverBias;

    // Last cascade fade — compute world-space fade start/end
    const float lastCascadeFar  = result.cascades[cfg.cascadeCount - 1].splitFarFadeStart;
    const float lastCascadeNear = result.cascades[cfg.cascadeCount - 1].splitNear;
    const float fadeExtension   = (lastCascadeFar - lastCascadeNear) * cfg.shadowDistanceFadeoutFraction;
    shadowData.shadowFadeStart  = lastCascadeFar - fadeExtension;
    shadowData.shadowFadeEnd    = lastCascadeFar;
    shadowData.filterMethod     = cfg.filterMethod;

    // PCSS parameters
    static constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    shadowData.tanLightSourceAngle = std::tan(lightSourceAngleDeg * kDegToRad);
    shadowData.maxSoftKernelSize   = cfg.maxSoftKernelSize;

    result.cbAllocation = cbRing.Allocate(sizeof(ShadowDataCB));
    std::memcpy(result.cbAllocation.cpuPtr, &shadowData, sizeof(ShadowDataCB));

    return result;
}

} // namespace uz
