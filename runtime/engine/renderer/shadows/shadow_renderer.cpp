/// UzEngine Shadow Renderer — Implementation.
///
/// Builds the per-frame ShadowConfig with UE5-parity defaults, computes
/// cascade splits via `ComputeCascades`, and uploads ShadowDataCB.

#include "shadow_renderer.h"

#include "camera.h"
#include "shadow_pass.h"
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
    CBRingAllocator& cbRing,
    uint32_t farShadowCascadeCount,
    float farShadowDistance,
    int32_t farShadowLodBias,
    bool useShadowCache,
    uint32_t staticShadowAtlasSrv)
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
    cfg.filterMethod                = 2;         // PCSS — contact hardening (ShadowRendering.cpp:180-186)
    cfg.maxSoftKernelSize           = 40.0f;     // UE5 default (ShadowRendering.cpp:182)

    // ── Far shadow cascades — wired from DirectionalLightComponent ───────
    // @source UE5 DirectionalLightComponent.cpp:1023,1076 — FarShadowDistance, FarShadowCascadeCount
    cfg.farShadowCascadeCount       = farShadowCascadeCount;
    cfg.farShadowDistance           = farShadowDistance;
    cfg.farShadowLodBias            = farShadowLodBias;

    // ── Compute cascade splits and light-space matrices ──────────────────
    // lightDir for ComputeCascades is the direction the light shines toward
    // (opposite of sun direction). sunDir points TOWARD the sun, so we negate it.
    const Vec3 lightShineDir = Vec3{-sunDir.x, -sunDir.y, -sunDir.z};
    ComputeCascades(camera.GetView(), camera.GetProjection(), lightShineDir,
                    cfg, result.cascades, &result.totalCascadeCount);

    // DEBUG: log cascade split distances and bias to verify correctness
    static bool loggedOnce = false;
    if (!loggedOnce) {
        for (uint32_t i = 0; i < result.totalCascadeCount; ++i) {
            UZ_LOG(Shadow, Info,
                "Cascade[{}]{}: splitNear={:.1f} splitFar={:.1f} fadeStart={:.1f} "
                "texelSize={:.4f} depthBias={:.6f} normalBias={:.4f}",
                i,
                result.cascades[i].bFarShadowCascade ? " (far)" : "",
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
    const uint32_t totalCascades = result.totalCascadeCount;

    ShadowDataCB shadowData{};
    for (uint32_t i = 0; i < totalCascades; ++i) {
        shadowData.cascadeViewProj[i]  = result.cascades[i].viewProj;
        shadowData.cascadeSplits[i]    = result.cascades[i].splitFar;
        shadowData.cascadeFadeStarts[i] = result.cascades[i].splitFarFadeStart;
        shadowData.cascadeDepthBias[i] = result.cascades[i].computedDepthBias;
        shadowData.cascadeNormalBias[i] = result.cascades[i].computedNormalBias;
    }

    shadowData.shadowAtlasIndex = shadowAtlasSrv;
    shadowData.cascadeCount     = totalCascades;
    shadowData.shadowMapSize    = static_cast<float>(shadowResolution);
    shadowData.receiverBias     = cfg.receiverBias;

    // Last cascade fade — compute world-space fade start/end using the
    // last cascade in the full chain (could be a far cascade)
    const float lastCascadeFar  = result.cascades[totalCascades - 1].splitFarFadeStart;
    const float lastCascadeNear = result.cascades[totalCascades - 1].splitNear;
    const float fadeExtension   = (lastCascadeFar - lastCascadeNear) * cfg.shadowDistanceFadeoutFraction;
    shadowData.shadowFadeStart  = lastCascadeFar - fadeExtension;
    shadowData.shadowFadeEnd    = lastCascadeFar;
    shadowData.filterMethod     = cfg.filterMethod;

    // PCSS parameters
    static constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    shadowData.tanLightSourceAngle = std::tan(lightSourceAngleDeg * kDegToRad);
    shadowData.maxSoftKernelSize   = cfg.maxSoftKernelSize;

    // Atlas grid layout — needed by the shader to compute cascade UV offsets
    shadowData.atlasColumns = GetAtlasColumns(totalCascades);

    // Shadow cache compositing — @source UE5 ShadowSetup.cpp:3846-3970
    // When active, the pixel shader samples both static (cached) and dynamic
    // (per-frame) shadow atlases and takes min(static, dynamic).
    shadowData.useShadowCache         = useShadowCache ? 1u : 0u;
    shadowData.staticShadowAtlasIndex = staticShadowAtlasSrv;

    result.cbAllocation = cbRing.Allocate(sizeof(ShadowDataCB));
    std::memcpy(result.cbAllocation.cpuPtr, &shadowData, sizeof(ShadowDataCB));

    return result;
}

} // namespace uz
