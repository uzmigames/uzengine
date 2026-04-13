/// UzEngine Shadow Renderer — Implementation.
///
/// Builds the per-frame ShadowConfig with industry-standard defaults, computes
/// cascade splits via `ComputeCascades`, and uploads ShadowDataCB.

#include "shadow_renderer.h"

#include "camera.h"
#include "shadow_pass.h"
#include "log.h"
#include "cvar.h"

UZ_DECLARE_LOG_CATEGORY(Shadow);

#include <cmath>
#include <cstring>

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_ShadowAtlasRes       = UZ_CVAR_INT("r.Shadow.AtlasResolution", 2048, "Main shadow atlas resolution", 512, 8192);
static auto* cv_LocalShadowAtlasSize = UZ_CVAR_INT("r.Shadow.LocalAtlasSize", 4096, "Local light shadow atlas resolution", 1024, 8192);
static auto* cv_LocalShadowTileSize  = UZ_CVAR_INT("r.Shadow.LocalTileSize", 1024, "Per-light shadow tile size", 256, 2048);
static auto* cv_PCSSSearchSamples    = UZ_CVAR_INT("r.Shadow.PCSS.SearchSamples", 16, "PCSS blocker search sample count", 8, 32);
static auto* cv_PCSSFilterSamples    = UZ_CVAR_INT("r.Shadow.PCSS.Samples", 32, "PCSS PCF filter sample count", 16, 64);
static auto* cv_PCSSMaxSharpness     = UZ_CVAR_FLOAT("r.Shadow.PCSS.MaxSharpness", 4.0f, "PCSS post-PCF sharpening limit", 1.f, 8.f);
static auto* cv_ContactShadowSteps   = UZ_CVAR_INT("r.ContactShadow.Steps", 12, "Contact shadow ray march steps", 4, 24);
static auto* cv_ContactShadowThick   = UZ_CVAR_FLOAT("r.ContactShadow.Thickness", 0.01f, "Contact shadow geometry thickness", 0.0001f, 0.1f);

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

    // ── Shadow config — standard parameters ────────────────────────────
    ShadowConfig& cfg = result.config;
    cfg.cascadeCount                = 4;
    cfg.resolution                  = shadowResolution;
    cfg.maxDistance                  = 40000.0f;  // 400m — reference default (DLC.cpp:1017)
    cfg.cascadeDistributionExponent = 2.0f;      // lower = cascade 0 covers further = circle boundary farther from camera
    cfg.cascadeTransitionFraction   = 0.1f;      // 10% blend overlap (DLC.cpp:1030)
    cfg.shadowDistanceFadeoutFraction = 0.1f;    // 10% fade on last cascade (DLC.cpp:1031)
    // Bias values must be NON-ZERO to avoid total acne / total shadowing.
    // The previous "all bias in rasterizer state" pattern only works when
    // the slope-scale rasterizer bias is actually wired through the shadow
    // VS PSO — which it is, but it's insufficient on its own for cm-scale
    // interior scenes (Sponza-class). Restore proper depth + normal bias.
    // @ref Lengyel "Foundations of Game Engine Development" §10.5 — bias
    // values around 1-10 cm world-space normal bias and 5-15 normalized
    // depth bias are typical for architectural scenes at cm scale.
    cfg.normalBias                  = 2.0f;      // World-space cm
    cfg.csmDepthBias                = 10.0f;     // Normalized depth bias
    cfg.slopeScaleDepthBias         = 3.0f * perLightShadowSlopeBias;  // Equivalent: CSMSlopeBias * per-light ShadowSlopeBias
    cfg.cascadeBiasDistribution     = 1.0f;      // Fully world-scaled (ShadowSetup.cpp:1846)
    cfg.receiverBias                = 1.0f;      // 1.0 = full shadow strength (no attenuation)
    cfg.filterMethod                = 1;         // PCSS variable penumbra with cascade-normalized kernel
    cfg.maxSoftKernelSize           = 40.0f;     // default (ShadowRendering.cpp:182)

    // ── Far shadow cascades — wired from DirectionalLightComponent ───────
    // FarShadowDistance, FarShadowCascadeCount — from DirectionalLightComponent
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

    // ── Upload ShadowDataCB — standard ─────────────────────────────────
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

    // ── PCSS parameters — standard ─────────────────────────────────────
    // @ref Fernando 2005 — PCSS: TanLightSourceAngle = tan(0.5 * deg2rad(LightSourceAngle))
    //   Half-angle because LightSourceAngle is the angular diameter (full apex angle).
    //   The sun's angular diameter is ~0.5357 degrees.
    // PCSSParameters.x = TanLightSourceAngle * SZ / SW
    //   SZ = MaxSubjectZ - MinSubjectZ (depth range of shadow ortho frustum)
    //   SW = 2.0 * ShadowBounds.W (world width of shadow ortho frustum)
    //   This scales the tangent from world-space to shadow tile UV/depth space.
    //   In UzEngine, both SZ and SW equal 2*radius, so SZ/SW = 1.0.
    static constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float tanHalfAngle = std::tan(0.5f * lightSourceAngleDeg * kDegToRad);
    // For our ortho CSM, SZ/SW = (2*radius)/(2*radius) = 1.0, so no additional scaling.
    shadowData.tanLightSourceAngle = tanHalfAngle;

    // MaxKernelSize / float(ResolutionX) — convert from texels to tile UV space
    //   Convert from texels to tile UV space [0,1].
    //   Default: r.Shadow.MaxSoftKernelSize = 40 (ShadowRendering.cpp:188-192).
    shadowData.maxSoftKernelSize = cfg.maxSoftKernelSize / static_cast<float>(shadowResolution);

    // Atlas grid layout — needed by the shader to compute cascade UV offsets
    shadowData.atlasColumns = GetAtlasColumns(totalCascades);

    // Shadow cache compositing — static + dynamic atlas min-compositing
    // When active, the pixel shader samples both static (cached) and dynamic
    // (per-frame) shadow atlases and takes min(static, dynamic).
    shadowData.useShadowCache         = useShadowCache ? 1u : 0u;
    shadowData.staticShadowAtlasIndex = staticShadowAtlasSrv;

    result.cbAllocation = cbRing.Allocate(sizeof(ShadowDataCB));
    std::memcpy(result.cbAllocation.cpuPtr, &shadowData, sizeof(ShadowDataCB));

    return result;
}

} // namespace uz
