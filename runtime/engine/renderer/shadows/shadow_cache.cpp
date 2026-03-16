/// UzEngine Static Shadow Cache — Implementation.
///
/// UE5 Reference:
/// - Cache validation: ShadowSetup.cpp:3869-3954
/// - Cache config: ShadowSetup.cpp:69-100
/// - Cached shadow map: ScenePrivate.h:2685-2722

#include "shadow_cache.h"

#include "engine_assert.h"
#include "log.h"
#include "rhi.h"

#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Shadow);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Epsilon tolerances for cache invalidation — UE5 ShadowSetup.cpp:3869-3954
// ─────────────────────────────────────────────────────────────────────────────

/// Tolerance for light direction vectors. Tight because even small angular
/// drift causes visible shadow shift at distance.
static constexpr float kDirectionEpsilon = 1e-4f;

/// Tolerance for distance/scale parameters. Looser because small changes in
/// cascade extent have minimal visual impact.
static constexpr float kDistanceEpsilon = 1e-2f;

// ─────────────────────────────────────────────────────────────────────────────
// Helper — approximate float equality
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true if `a` and `b` are within `epsilon` of each other.
static bool NearEqual(float a, float b, float epsilon) {
    return std::fabs(a - b) <= epsilon;
}

/// Returns true if two Vec3 values are within `epsilon` per-component.
static bool NearEqualVec3(const Vec3& a, const Vec3& b, float epsilon) {
    return NearEqual(a.x, b.x, epsilon)
        && NearEqual(a.y, b.y, epsilon)
        && NearEqual(a.z, b.z, epsilon);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void ShadowCacheManager::Init(IRHIDevice* device, const ShadowCacheConfig& config) {
    UZ_CHECK(device != nullptr);

    m_device = device;
    m_config = config;
    m_budgetBytes = static_cast<uint64_t>(config.cacheBudgetMB) * 1024ULL * 1024ULL;
    m_totalMemoryBytes = 0;
    m_caches.clear();

    UZ_LOG(Shadow, Info, "Shadow cache initialized: budget={}MB, enabled={}",
           config.cacheBudgetMB, config.enabled);
}

void ShadowCacheManager::Shutdown() {
    // Release all cached textures before clearing the map
    for (auto& [lightId, lightCache] : m_caches) {
        for (uint32_t i = 0; i < lightCache.cascadeCount; ++i) {
            ReleaseCachedTexture(lightCache.cascades[i]);
        }
    }
    m_caches.clear();
    m_totalMemoryBytes = 0;

    UZ_LOG(Shadow, Info, "Shadow cache shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — BuildKey
// ─────────────────────────────────────────────────────────────────────────────

ShadowCacheKey ShadowCacheManager::BuildKey(
    const ShadowCascade& cascade,
    const Vec3& lightDir,
    const Vec3& lightUp,
    uint32_t resolution)
{
    ShadowCacheKey key{};
    key.lightDirection = lightDir;
    key.lightUp = lightUp;
    key.resolution = resolution;

    // Extract projection parameters from the cascade.
    // texelSize = worldSpaceExtent / resolution, so worldSpaceExtent = texelSize * resolution.
    // subjectRadius = worldSpaceExtent / 2.
    key.subjectRadius = cascade.texelSize * static_cast<float>(resolution) * 0.5f;
    key.projectionScale = key.subjectRadius * 2.0f;

    // The min/max subject Z correspond to the near/far planes of the ortho
    // projection. We use the split distances as proxies since the ortho
    // projection is built to cover [splitNear, splitFar] in light space.
    key.minSubjectZ = cascade.splitNear;
    key.maxSubjectZ = cascade.splitFar;

    return key;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — IsCacheValid
// ─────────────────────────────────────────────────────────────────────────────

bool ShadowCacheManager::IsCacheValid(
    const ShadowCacheKey& cached,
    const ShadowCacheKey& current)
{
    // Resolution must match exactly — different texture size means incompatible cache
    if (cached.resolution != current.resolution) {
        return false;
    }

    // Light direction — tight tolerance because angular change shifts shadows
    if (!NearEqualVec3(cached.lightDirection, current.lightDirection, kDirectionEpsilon)) {
        return false;
    }

    // Light up vector — tight tolerance for the same reason
    if (!NearEqualVec3(cached.lightUp, current.lightUp, kDirectionEpsilon)) {
        return false;
    }

    // Cascade extent parameters — looser tolerance because small changes
    // in the bounding sphere have minimal visual impact
    if (!NearEqual(cached.subjectRadius, current.subjectRadius, kDistanceEpsilon)) {
        return false;
    }

    if (!NearEqual(cached.minSubjectZ, current.minSubjectZ, kDistanceEpsilon)) {
        return false;
    }

    if (!NearEqual(cached.maxSubjectZ, current.maxSubjectZ, kDistanceEpsilon)) {
        return false;
    }

    if (!NearEqual(cached.projectionScale, current.projectionScale, kDistanceEpsilon)) {
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — UpdateCache
// ─────────────────────────────────────────────────────────────────────────────

ShadowCacheMode ShadowCacheManager::UpdateCache(
    uint32_t lightId,
    uint32_t cascadeIndex,
    const ShadowCascade& cascade,
    const Vec3& lightDir,
    const Vec3& lightUp,
    uint32_t resolution,
    uint32_t currentFrame)
{
    // If caching is globally disabled, always fall back to uncached
    if (!m_config.enabled) {
        return ShadowCacheMode::Uncached;
    }

    UZ_CHECK(cascadeIndex < kMaxShadowCascades);

    const ShadowCacheKey currentKey = BuildKey(cascade, lightDir, lightUp, resolution);

    // Find or create the per-light cache entry
    auto& lightCache = m_caches[lightId];
    if (lightCache.cascadeCount <= cascadeIndex) {
        lightCache.cascadeCount = cascadeIndex + 1;
    }

    auto& data = lightCache.cascades[cascadeIndex];

    // Check if existing cache is valid
    if (data.valid && data.cachedTexture.IsValid()) {
        if (IsCacheValid(data.key, currentKey)) {
            // Cache hit — static shadow map is still good, only render dynamic
            data.lastUsedFrame = currentFrame;
            return ShadowCacheMode::MovablePrimitivesOnly;
        }

        // Cache is stale — invalidate but keep the texture allocation
        // (it will be overwritten with fresh static geometry)
        data.valid = false;
    }

    // Cache miss — need to allocate a texture if we don't have one
    if (!data.cachedTexture.IsValid()) {
        if (!AllocateCachedTexture(data, resolution)) {
            // Budget exceeded or allocation failed — fall back to uncached
            return ShadowCacheMode::Uncached;
        }
    }

    // The caller will render static geometry into this texture
    data.key = currentKey;
    data.lastUsedFrame = currentFrame;
    data.valid = false; // Will be set to true by the caller after rendering

    return ShadowCacheMode::StaticPrimitivesOnly;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — GetCachedTexture / GetCachedTextureSrvIndex
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle ShadowCacheManager::GetCachedTexture(
    uint32_t lightId,
    uint32_t cascadeIndex) const
{
    auto it = m_caches.find(lightId);
    if (it == m_caches.end()) {
        return RHITextureHandle::Invalid();
    }

    const auto& lightCache = it->second;
    if (cascadeIndex >= lightCache.cascadeCount) {
        return RHITextureHandle::Invalid();
    }

    return lightCache.cascades[cascadeIndex].cachedTexture;
}

uint32_t ShadowCacheManager::GetCachedTextureSrvIndex(
    uint32_t lightId,
    uint32_t cascadeIndex) const
{
    auto it = m_caches.find(lightId);
    if (it == m_caches.end()) {
        return UINT32_MAX;
    }

    const auto& lightCache = it->second;
    if (cascadeIndex >= lightCache.cascadeCount) {
        return UINT32_MAX;
    }

    return lightCache.cascades[cascadeIndex].srvIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — InvalidateLight / InvalidateAll / EvictStale
// ─────────────────────────────────────────────────────────────────────────────

void ShadowCacheManager::InvalidateLight(uint32_t lightId) {
    auto it = m_caches.find(lightId);
    if (it == m_caches.end()) {
        return;
    }

    auto& lightCache = it->second;
    for (uint32_t i = 0; i < lightCache.cascadeCount; ++i) {
        lightCache.cascades[i].valid = false;
    }

    UZ_LOG(Shadow, Info, "Invalidated shadow cache for light {}", lightId);
}

void ShadowCacheManager::InvalidateAll() {
    for (auto& [lightId, lightCache] : m_caches) {
        for (uint32_t i = 0; i < lightCache.cascadeCount; ++i) {
            lightCache.cascades[i].valid = false;
        }
    }

    UZ_LOG(Shadow, Info, "Invalidated all shadow caches ({} lights)", m_caches.size());
}

void ShadowCacheManager::EvictStale(uint32_t olderThanFrame) {
    // Collect light IDs to erase (can't erase during iteration of unordered_map safely
    // on all implementations, so collect first)
    std::vector<uint32_t> lightsToEvict;

    for (auto& [lightId, lightCache] : m_caches) {
        bool anyActive = false;
        for (uint32_t i = 0; i < lightCache.cascadeCount; ++i) {
            auto& data = lightCache.cascades[i];
            if (data.cachedTexture.IsValid() && data.lastUsedFrame < olderThanFrame) {
                ReleaseCachedTexture(data);
            }
            if (data.cachedTexture.IsValid()) {
                anyActive = true;
            }
        }

        // If no cascades have textures, mark the entire light for removal
        if (!anyActive) {
            lightsToEvict.push_back(lightId);
        }
    }

    for (uint32_t lightId : lightsToEvict) {
        m_caches.erase(lightId);
    }

    if (!lightsToEvict.empty()) {
        UZ_LOG(Shadow, Info, "Evicted {} stale shadow cache entries (older than frame {})",
               lightsToEvict.size(), olderThanFrame);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — private helpers
// ─────────────────────────────────────────────────────────────────────────────

bool ShadowCacheManager::AllocateCachedTexture(CachedShadowMapData& data, uint32_t resolution) {
    UZ_CHECK(m_device != nullptr);

    const uint64_t texMemory = ComputeTextureMemory(resolution);

    // Check VRAM budget before allocating
    if (m_totalMemoryBytes + texMemory > m_budgetBytes) {
        UZ_LOG(Shadow, Warning,
               "Shadow cache budget exceeded: current={}MB + new={}MB > budget={}MB",
               m_totalMemoryBytes / (1024 * 1024),
               texMemory / (1024 * 1024),
               m_budgetBytes / (1024 * 1024));
        return false;
    }

    RHITextureDesc desc{};
    desc.width     = resolution;
    desc.height    = resolution;
    desc.format    = RHIFormat::D32Float;
    desc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    desc.debugName = "CachedShadowMap";

    data.cachedTexture = m_device->CreateTexture(desc);
    if (!data.cachedTexture.IsValid()) {
        UZ_LOG(Shadow, Error,
               "Failed to allocate cached shadow texture ({}x{} D32Float)",
               resolution, resolution);
        return false;
    }

    data.srvIndex = m_device->GetTextureSRVIndex(data.cachedTexture);
    data.valid = false;
    data.key = {};

    m_totalMemoryBytes += texMemory;

    UZ_LOG(Shadow, Info,
           "Allocated cached shadow texture: {}x{} D32Float (srv={}, totalVRAM={}MB)",
           resolution, resolution, data.srvIndex,
           m_totalMemoryBytes / (1024 * 1024));

    return true;
}

void ShadowCacheManager::ReleaseCachedTexture(CachedShadowMapData& data) {
    if (!data.cachedTexture.IsValid()) {
        return;
    }

    if (m_device) {
        const uint64_t texMemory = ComputeTextureMemory(data.key.resolution);
        m_device->DestroyTexture(data.cachedTexture);

        // Guard against underflow (should not happen, but defensive)
        if (m_totalMemoryBytes >= texMemory) {
            m_totalMemoryBytes -= texMemory;
        } else {
            m_totalMemoryBytes = 0;
        }
    }

    data.cachedTexture = RHITextureHandle::Invalid();
    data.srvIndex = UINT32_MAX;
    data.valid = false;
}

uint64_t ShadowCacheManager::ComputeTextureMemory(uint32_t resolution) {
    // D32Float = 4 bytes per texel
    return static_cast<uint64_t>(resolution) * static_cast<uint64_t>(resolution) * 4ULL;
}

} // namespace uz
