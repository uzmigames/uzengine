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
    key.cascadeCenter = cascade.center;
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

/// Returns true if the cached shadow map can be used for CSM scrolling.
///
/// Scrolling is possible when the light direction, up vector, subject radius,
/// and resolution all match — but the cascade center has moved (camera movement).
/// This is a weaker check than IsCacheValid (which also checks center equality).
///
/// @source UE5 ShadowSetup.cpp:3862-3873 — bIsValidCachedViewDependentWholeSceneShadow
static bool IsCacheValidForScrolling(
    const ShadowCacheKey& cached,
    const ShadowCacheKey& current)
{
    if (cached.resolution != current.resolution) return false;
    if (!NearEqualVec3(cached.lightDirection, current.lightDirection, kDirectionEpsilon)) return false;
    if (!NearEqualVec3(cached.lightUp, current.lightUp, kDirectionEpsilon)) return false;
    if (!NearEqual(cached.subjectRadius, current.subjectRadius, kDistanceEpsilon)) return false;
    // Note: we intentionally do NOT check center — center movement is what triggers scrolling
    return true;
}

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
    auto& scrolling = lightCache.scrollingData[cascadeIndex];
    scrolling.active = false; // Reset scrolling state

    // Check if existing cache is valid
    if (data.valid && data.cachedTexture.IsValid()) {
        if (IsCacheValid(data.key, currentKey)) {
            // Cache hit — static shadow map is still good, only render dynamic
            data.lastUsedFrame = currentFrame;
            return ShadowCacheMode::MovablePrimitivesOnly;
        }

        // Cache is stale for exact match — check if scrolling is possible.
        // Scrolling requires: same light dir, up, radius, resolution, but different center.
        // Also requires the extra static subjects from previous scrolling to be within budget.
        /// @source UE5 ShadowSetup.cpp:3884-3919
        if (IsCacheValidForScrolling(data.key, currentKey)
            && data.lastFrameExtraStaticShadowSubjects <= kMaxCSMScrollingExtraStaticSubjects)
        {
            // Compute overlap between old and new cascade bounds in light space
            scrolling = ComputeScrollingData(
                data.key, currentKey,
                data.cachedPreShadowTranslation, currentKey.cascadeCenter,
                lightDir);

            if (scrolling.overlapRatio > m_config.scrollingOverlapThreshold) {
                // Sufficient overlap — use CSM scrolling mode
                scrolling.active = true;
                data.lastUsedFrame = currentFrame;

                // Update the cached center to the new position.
                // The key is NOT fully updated yet — we keep the old key except for
                // the center, because the next frame's IsCacheValid will need the
                // updated center for the exact-match check.
                data.cachedPreShadowTranslation = currentKey.cascadeCenter;
                data.key.cascadeCenter = currentKey.cascadeCenter;
                data.key.minSubjectZ = currentKey.minSubjectZ;
                data.key.maxSubjectZ = currentKey.maxSubjectZ;

                return ShadowCacheMode::CSMScrolling;
            }
        }

        // Cache is stale and scrolling not possible — invalidate but keep the texture
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
    data.cachedPreShadowTranslation = currentKey.cascadeCenter;
    data.lastUsedFrame = currentFrame;
    data.valid = false; // Will be set to true by the caller after rendering
    data.staticPrimitiveMap.clear();
    data.lastFrameExtraStaticShadowSubjects = 0;

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

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — Static primitive tracking
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ShadowSetup.cpp:2364 — StaticShadowSubjectPersistentPrimitiveIdMap[...] = true
void ShadowCacheManager::MarkStaticPrimitiveRendered(
    uint32_t lightId, uint32_t cascadeIndex,
    uint32_t primitiveIndex, uint32_t totalPrimitives)
{
    auto it = m_caches.find(lightId);
    if (it == m_caches.end()) return;

    auto& lightCache = it->second;
    if (cascadeIndex >= lightCache.cascadeCount) return;

    auto& data = lightCache.cascades[cascadeIndex];

    // Resize the bitset if needed (lazy initialization)
    if (data.staticPrimitiveMap.size() < totalPrimitives) {
        data.staticPrimitiveMap.resize(totalPrimitives, false);
    }

    if (primitiveIndex < data.staticPrimitiveMap.size()) {
        data.staticPrimitiveMap[primitiveIndex] = true;
    }
}

/// @source UE5 ShadowSetup.cpp:5002 — StaticShadowSubjectPersistentPrimitiveIdMap[...]
bool ShadowCacheManager::IsStaticPrimitiveRendered(
    uint32_t lightId, uint32_t cascadeIndex,
    uint32_t primitiveIndex) const
{
    auto it = m_caches.find(lightId);
    if (it == m_caches.end()) return false;

    const auto& lightCache = it->second;
    if (cascadeIndex >= lightCache.cascadeCount) return false;

    const auto& data = lightCache.cascades[cascadeIndex];
    if (primitiveIndex >= data.staticPrimitiveMap.size()) return false;

    return data.staticPrimitiveMap[primitiveIndex];
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — CSM Scrolling data access
// ─────────────────────────────────────────────────────────────────────────────

const CSMScrollingData ShadowCacheManager::s_inactiveScrollingData{};

const CSMScrollingData& ShadowCacheManager::GetScrollingData(
    uint32_t lightId, uint32_t cascadeIndex) const
{
    auto it = m_caches.find(lightId);
    if (it == m_caches.end()) return s_inactiveScrollingData;

    const auto& lightCache = it->second;
    if (cascadeIndex >= lightCache.cascadeCount) return s_inactiveScrollingData;

    return lightCache.scrollingData[cascadeIndex];
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — CSM Scrolling computation
// ─────────────────────────────────────────────────────────────────────────────

/// Computes CSM scrolling overlap and culling plane data.
///
/// Projects cascade centers into light space, computes AABB overlap ratio,
/// and generates edge-strip culling planes for the non-overlapped regions.
///
/// @source UE5 ShadowSetup.cpp:3893-3931 — overlap ratio computation
/// @source UE5 ShadowSetup.cpp:5600-5668 — UV regions + culling planes
CSMScrollingData ShadowCacheManager::ComputeScrollingData(
    const ShadowCacheKey& cachedKey,
    const ShadowCacheKey& currentKey,
    const Vec3& cachedCenter,
    const Vec3& currentCenter,
    const Vec3& lightDir) const
{
    CSMScrollingData result{};

    // Build a light-space basis from the light direction.
    // @source UE5 ShadowSetup.cpp:3893-3896 — FaceDirection + FindBestAxisVectors
    const Vec3 fwd = lightDir.Normalized();

    // Choose a stable right and up axis for the light-space basis.
    // We pick the world axis least aligned with the light direction.
    Vec3 rightAxis;
    if (std::fabs(fwd.x) < 0.9f) {
        rightAxis = Vec3{1.f, 0.f, 0.f};
    } else {
        rightAxis = Vec3{0.f, 1.f, 0.f};
    }
    const Vec3 lightUp = fwd.Cross(rightAxis).Normalized();
    const Vec3 lightRight = lightUp.Cross(fwd).Normalized();

    // Project cascade centers into light space (2D: right and up axes).
    // @source UE5 ShadowSetup.cpp:3901-3904
    const Vec3 projCurrent = Vec3{
        currentCenter.Dot(lightRight),
        currentCenter.Dot(lightUp),
        currentCenter.Dot(fwd)
    };
    const Vec3 projCached = Vec3{
        cachedCenter.Dot(lightRight),
        cachedCenter.Dot(lightUp),
        cachedCenter.Dot(fwd)
    };
    const Vec3 projCenterDelta = projCached - projCurrent;

    // Build 2D AABBs in light space for current and cached viewports.
    // Current viewport is centered at origin; cached is offset by delta.
    // @source UE5 ShadowSetup.cpp:3906-3907
    const float currentRadius = currentKey.subjectRadius;
    const float cachedRadius = cachedKey.subjectRadius;

    // Current AABB: [-currentRadius, -currentRadius] to [currentRadius, currentRadius]
    const float curMinX = -currentRadius;
    const float curMinY = -currentRadius;
    const float curMaxX =  currentRadius;
    const float curMaxY =  currentRadius;

    // Cached AABB: offset by the center delta
    const float cacMinX = projCenterDelta.x - cachedRadius;
    const float cacMinY = projCenterDelta.y - cachedRadius;
    const float cacMaxX = projCenterDelta.x + cachedRadius;
    const float cacMaxY = projCenterDelta.y + cachedRadius;

    // Compute AABB intersection (overlap).
    // @source UE5 ShadowSetup.cpp:3909
    const float ovMinX = std::max(curMinX, cacMinX);
    const float ovMinY = std::max(curMinY, cacMinY);
    const float ovMaxX = std::min(curMaxX, cacMaxX);
    const float ovMaxY = std::min(curMaxY, cacMaxY);

    // Check for zero or negative overlap
    if (ovMaxX <= ovMinX || ovMaxY <= ovMinY) {
        result.overlapRatio = 0.f;
        return result;
    }

    // Overlap area ratio = overlap area / cached area
    // @source UE5 ShadowSetup.cpp:3911-3913
    const float overlapArea = (ovMaxX - ovMinX) * (ovMaxY - ovMinY);
    const float cachedArea = cachedRadius * cachedRadius * 4.0f;
    result.overlapRatio = (cachedArea > 1e-6f) ? (overlapArea / cachedArea) : 0.f;

    // Compute UV regions for the overlap on both the cached and current shadow maps.
    // UV is in [0,1] normalized texture space.
    // @source UE5 ShadowSetup.cpp:5618-5639

    const float cacWidth  = cacMaxX - cacMinX;
    const float cacHeight = cacMaxY - cacMinY;
    const float curWidth  = curMaxX - curMinX;
    const float curHeight = curMaxY - curMinY;

    // UV on cached shadow map (source of GPU copy)
    result.overlappedUVOnCached[0] = (ovMinX - cacMinX) / cacWidth;      // minU
    result.overlappedUVOnCached[1] = (cacMaxY - ovMaxY) / cacHeight;     // minV (Y-flipped)
    result.overlappedUVOnCached[2] = (ovMaxX - cacMinX) / cacWidth;      // maxU
    result.overlappedUVOnCached[3] = (cacMaxY - ovMinY) / cacHeight;     // maxV (Y-flipped)

    // UV on current shadow map (destination of GPU copy)
    result.overlappedUVOnCurrent[0] = (ovMinX - curMinX) / curWidth;     // minU
    result.overlappedUVOnCurrent[1] = (curMaxY - ovMaxY) / curHeight;    // minV (Y-flipped)
    result.overlappedUVOnCurrent[2] = (ovMaxX - curMinX) / curWidth;     // maxU
    result.overlappedUVOnCurrent[3] = (curMaxY - ovMinY) / curHeight;    // maxV (Y-flipped)

    // Z offset for depth reconstruction during scrolling copy.
    // @source UE5 ShadowSetup.cpp:5616
    result.zOffset = projCenterDelta.z;

    // Compute culling planes for the non-overlapped edge strips.
    // Each strip is a region of the new cascade that doesn't overlap with the cached one.
    // Only static primitives intersecting these strips need re-rendering.
    // @source UE5 ShadowSetup.cpp:5641-5667

    // Transform back to world space using the light basis vectors.
    result.cullingPlaneCount = 0;

    // Top strip: current extends above overlap
    // @source UE5 ShadowSetup.cpp:5646-5649
    if (curMaxY > ovMaxY) {
        auto& plane = result.cullingPlanes[result.cullingPlaneCount++];
        // Plane at the overlap top edge, normal pointing inward (toward the strip)
        plane.point = currentCenter + lightUp * ovMaxY;
        plane.normal = lightUp * (-1.f); // Points toward positive Y (into the strip)
    }

    // Bottom strip: current extends below overlap
    // @source UE5 ShadowSetup.cpp:5652-5655
    if (curMinY < ovMinY) {
        auto& plane = result.cullingPlanes[result.cullingPlaneCount++];
        plane.point = currentCenter + lightUp * ovMinY;
        plane.normal = lightUp;
    }

    // Left strip: current extends to the left of overlap
    // @source UE5 ShadowSetup.cpp:5658-5661
    if (curMinX < ovMinX) {
        auto& plane = result.cullingPlanes[result.cullingPlaneCount++];
        plane.point = currentCenter + lightRight * ovMinX;
        plane.normal = lightRight;
    }

    // Right strip: current extends to the right of overlap
    // @source UE5 ShadowSetup.cpp:5663-5667
    if (curMaxX > ovMaxX) {
        auto& plane = result.cullingPlanes[result.cullingPlaneCount++];
        plane.point = currentCenter + lightRight * ovMaxX;
        plane.normal = lightRight * (-1.f);
    }

    return result;
}

} // namespace uz
