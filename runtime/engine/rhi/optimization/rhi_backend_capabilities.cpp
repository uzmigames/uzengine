/// RHI backend capability detection and feature-tier computation.

#include "rhi_backend_capabilities.h"

#include "log.h"

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Tier computation
// ─────────────────────────────────────────────────────────────────────────────

void BackendCapabilities::ComputeTier()
{
    if (meshShaders && bindlessResources && maxShaderModel >= 66) {
        tier = FeatureTier::Tier3;
    } else if (bindlessResources || (indirectDraw && maxShaderModel >= 60)) {
        tier = FeatureTier::Tier2;
    } else {
        tier = FeatureTier::Tier1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the tier name as a string literal for logging.
static const char* TierToString(FeatureTier t)
{
    switch (t) {
    case FeatureTier::Tier1: return "Tier1 (Baseline)";
    case FeatureTier::Tier2: return "Tier2 (Mid-tier)";
    case FeatureTier::Tier3: return "Tier3 (High-end)";
    }
    return "Unknown";
}

void BackendCapabilities::LogCapabilities() const
{
    UZ_LOG(RHI, Info, "Backend capabilities — tier={}, SM={}.{}",
           TierToString(tier),
           maxShaderModel / 10,
           maxShaderModel % 10);

    UZ_LOG(RHI, Info, "  Descriptors: bindless={}, indexing={}",
           bindlessResources, descriptorIndexing);
    UZ_LOG(RHI, Info, "  Geometry:    meshShaders={}, indirectDraw={}, indirectDispatch={}",
           meshShaders, indirectDraw, indirectDispatch);
    UZ_LOG(RHI, Info, "  RayTracing:  rt={}, inline={}",
           rayTracing, inlineRayTracing);
    UZ_LOG(RHI, Info, "  Compute:     async={}, waveIntrinsics={}",
           asyncCompute, waveIntrinsics);
    UZ_LOG(RHI, Info, "  Memory:      sparse={}, placed={}",
           sparseResources, placedResources);
}

} // namespace uz
