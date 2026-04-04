/// DecalManager implementation -- central decal subsystem facade.

#include "engine/vfx/decals/decal_manager.h"

#include "engine/logging/log.h"
#include "engine/math/mat4.h"
#include "engine/math/math_defs.h"
#include "engine/vfx/decals/decal_asset.h"
#include "engine/vfx/decals/decal_atlas.h"
#include "engine/vfx/decals/decal_component.h"
#include "engine/vfx/decals/decal_pool.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Small offset along the surface normal to prevent z-fighting between the
/// decal projection volume and the underlying geometry.
static constexpr float kSurfaceOffset = 0.01f;

/// Dot product threshold for detecting that the surface normal is nearly
/// parallel to world up. Above this value we fall back to world right as
/// the reference axis for alignment.
static constexpr float kUpParallelThreshold = 0.999f;

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

DecalManager::DecalManager()
    : rng_(42u) {}  // Deterministic seed; use PCGRandom for fast + quality RNG.

DecalManager::~DecalManager() {
    if (initialized_) {
        UZ_LOG(VFX, Warning,
               "DecalManager destroyed without Shutdown() -- resources may leak");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Move semantics
// ─────────────────────────────────────────────────────────────────────────────

DecalManager::DecalManager(DecalManager&& other) noexcept
    : pool_(std::move(other.pool_)),
      albedoAtlas_(std::move(other.albedoAtlas_)),
      normalAtlas_(std::move(other.normalAtlas_)),
      roughnessAtlas_(std::move(other.roughnessAtlas_)),
      sortedDecals_(std::move(other.sortedDecals_)),
      culledFlags_(std::move(other.culledFlags_)),
      rng_(std::move(other.rng_)),
      initialized_(other.initialized_) {
    other.initialized_ = false;
}

DecalManager& DecalManager::operator=(DecalManager&& other) noexcept {
    if (this != &other) {
        pool_ = std::move(other.pool_);
        albedoAtlas_ = std::move(other.albedoAtlas_);
        normalAtlas_ = std::move(other.normalAtlas_);
        roughnessAtlas_ = std::move(other.roughnessAtlas_);
        sortedDecals_ = std::move(other.sortedDecals_);
        culledFlags_ = std::move(other.culledFlags_);
        rng_ = std::move(other.rng_);
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void DecalManager::Initialize(IRHIDevice* /*device*/,
                               uint32_t poolSize,
                               uint32_t atlasSize) {
    if (initialized_) {
        UZ_LOG(VFX, Warning,
               "DecalManager::Initialize called on an already-initialized manager");
        return;
    }

    pool_ = std::make_unique<DecalPool>(poolSize);
    albedoAtlas_ = std::make_unique<DecalAtlas>(atlasSize);
    normalAtlas_ = std::make_unique<DecalAtlas>(atlasSize);
    roughnessAtlas_ = std::make_unique<DecalAtlas>(atlasSize);

    sortedDecals_.reserve(poolSize);
    culledFlags_.resize(poolSize, false);

    initialized_ = true;

    UZ_LOG(VFX, Info,
           "DecalManager initialized: poolSize={}, atlasSize={}",
           poolSize, atlasSize);
}

void DecalManager::Shutdown(IRHIDevice* /*device*/) {
    if (!initialized_) {
        return;
    }

    const uint32_t activeCount = pool_ ? pool_->GetActiveCount() : 0;

    if (pool_) {
        pool_->Clear();
    }

    if (albedoAtlas_)    { albedoAtlas_->Reset(); }
    if (normalAtlas_)    { normalAtlas_->Reset(); }
    if (roughnessAtlas_) { roughnessAtlas_->Reset(); }

    pool_.reset();
    albedoAtlas_.reset();
    normalAtlas_.reset();
    roughnessAtlas_.reset();

    sortedDecals_.clear();
    culledFlags_.clear();

    initialized_ = false;

    UZ_LOG(VFX, Info,
           "DecalManager shut down: {} active decals released", activeCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// SpawnDecal
// ─────────────────────────────────────────────────────────────────────────────

VfxDecal* DecalManager::SpawnDecal(const DecalAsset& asset,
                                          const Vec3& position,
                                          const Vec3& surfaceNormal,
                                          float lifetimeOverride) {
    if (!initialized_) {
        UZ_LOG(VFX, Error,
               "DecalManager::SpawnDecal called before Initialize()");
        return nullptr;
    }

    VfxDecal* decal = pool_->AcquireDecal();
    if (!decal) {
        // Pool exhaustion warning is logged by DecalPool::AcquireDecal().
        return nullptr;
    }

    // Position: offset slightly along the surface normal to avoid z-fighting.
    decal->position = position + surfaceNormal * kSurfaceOffset;

    // Orientation: align decal's -Z to surface normal, then apply random twist.
    const Quat alignQuat = AlignToSurface(surfaceNormal);
    const Quat randomTwist = RandomRotationAroundAxis(surfaceNormal);
    decal->rotation = (randomTwist * alignQuat).Normalized();

    // Copy asset configuration.
    decal->halfExtents = asset.halfExtents;
    decal->materialPath = asset.materialPath;
    decal->blendMode = asset.blendMode;
    decal->normalIntensity = asset.normalIntensity;
    decal->roughnessOverride = asset.roughnessOverride;
    decal->sortOrder = asset.sortOrder;

    // Lifetime: use override if positive, otherwise asset default.
    decal->lifetime = (lifetimeOverride > 0.0f) ? lifetimeOverride : asset.lifetime;

    // Ensure the decal starts fully active.
    decal->age = 0.0f;
    decal->fadeAlpha = 1.0f;
    decal->isActive = true;

    UZ_LOG(VFX, Debug,
           "Decal spawned: material='{}', lifetime={:.1f}s",
           asset.materialPath, decal->lifetime);

    return decal;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update / Cleanup
// ─────────────────────────────────────────────────────────────────────────────

void DecalManager::Update(float dt) {
    if (!initialized_) {
        return;
    }

    const auto& activeDecals = pool_->GetActiveDecals();
    for (auto* decal : activeDecals) {
        decal->Tick(dt);
    }

    Cleanup();
}

void DecalManager::Cleanup() {
    const auto& activeDecals = pool_->GetActiveDecals();

    // Collect expired decals first to avoid mutating the list while iterating.
    // We use a small local vector to batch the returns.
    std::vector<VfxDecal*> toReturn;
    toReturn.reserve(16);

    for (auto* decal : activeDecals) {
        if (!decal->isActive || decal->IsExpired()) {
            toReturn.push_back(decal);
        }
    }

    for (auto* decal : toReturn) {
        pool_->ReturnDecal(decal);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering -- sorting and frustum culling
// ─────────────────────────────────────────────────────────────────────────────

const std::vector<VfxDecal*>& DecalManager::GetSortedDecals(
    const Vec3& cameraPos) {
    sortedDecals_.clear();

    if (!initialized_) {
        return sortedDecals_;
    }

    const auto& activeDecals = pool_->GetActiveDecals();
    const auto activeCount = activeDecals.size();

    // Collect non-culled, active decals. culledFlags_ is indexed by active-list
    // position, set by the most recent FrustumCull() call. If FrustumCull()
    // has not been called this frame, all flags default to false (not culled).
    for (size_t i = 0; i < activeCount; ++i) {
        auto* decal = activeDecals[i];
        if (!decal->isActive) {
            continue;
        }
        if (i < culledFlags_.size() && culledFlags_[i]) {
            continue;
        }
        sortedDecals_.push_back(decal);
    }

    // Sort: primary key = sortOrder (ascending), secondary = distance (back-to-front).
    std::sort(sortedDecals_.begin(), sortedDecals_.end(),
              [&cameraPos](const VfxDecal* a, const VfxDecal* b) {
                  if (a->sortOrder != b->sortOrder) {
                      return a->sortOrder < b->sortOrder;
                  }
                  // Back-to-front: farther decals first.
                  const float distA = (a->position - cameraPos).lengthSq();
                  const float distB = (b->position - cameraPos).lengthSq();
                  return distA > distB;
              });

    return sortedDecals_;
}

void DecalManager::FrustumCull(const Mat4& viewProjection) {
    if (!initialized_) {
        return;
    }

    const auto& activeDecals = pool_->GetActiveDecals();

    // Reset all culled flags for active decals.
    // We use the active-list index as the culling key.
    const auto activeCount = activeDecals.size();
    if (culledFlags_.size() < activeCount) {
        culledFlags_.resize(activeCount, false);
    }

    for (size_t i = 0; i < activeCount; ++i) {
        const auto* decal = activeDecals[i];

        // Conservative bounding sphere: use the largest half-extent as radius.
        const float radius = std::max({decal->halfExtents.x,
                                        decal->halfExtents.y,
                                        decal->halfExtents.z});

        culledFlags_[i] = !IsSphereInFrustum(viewProjection,
                                              decal->position,
                                              radius);
    }

    // Remove culled decals from sortedDecals_ (if it was populated before cull).
    // Typically FrustumCull is called before GetSortedDecals, so this is a no-op.
    // But we also filter in GetSortedDecals for safety.
}

// ─────────────────────────────────────────────────────────────────────────────
// Stats
// ─────────────────────────────────────────────────────────────────────────────

uint32_t DecalManager::GetActiveCount() const noexcept {
    if (!initialized_ || !pool_) {
        return 0;
    }
    return pool_->GetActiveCount();
}

uint32_t DecalManager::GetPoolAvailable() const noexcept {
    if (!initialized_ || !pool_) {
        return 0;
    }
    return pool_->GetAvailableCount();
}

float DecalManager::GetAtlasOccupancy() const noexcept {
    if (!initialized_) {
        return 0.0f;
    }

    float total = 0.0f;
    uint32_t count = 0;

    if (albedoAtlas_) {
        total += albedoAtlas_->GetOccupancy();
        ++count;
    }
    if (normalAtlas_) {
        total += normalAtlas_->GetOccupancy();
        ++count;
    }
    if (roughnessAtlas_) {
        total += roughnessAtlas_->GetOccupancy();
        ++count;
    }

    return (count > 0) ? (total / static_cast<float>(count)) : 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// AlignToSurface -- quaternion that maps -Z to the given surface normal
// ─────────────────────────────────────────────────────────────────────────────

Quat DecalManager::AlignToSurface(const Vec3& surfaceNormal) {
    // We want a rotation that maps the default projection direction (-Z)
    // to the surface normal. That is equivalent to mapping +Z to -surfaceNormal,
    // but it is simpler to think of it as: "rotate from -Z to surfaceNormal".
    const Vec3 from{0.0f, 0.0f, -1.0f};
    const Vec3 to = surfaceNormal.Normalized();

    const float d = from.Dot(to);

    // If the vectors are nearly identical, return identity.
    if (d > kUpParallelThreshold) {
        return Quat::Identity();
    }

    // If the vectors are nearly opposite, pick an arbitrary perpendicular axis.
    if (d < -kUpParallelThreshold) {
        // surfaceNormal ~ +Z, so -Z -> +Z is a 180-degree flip.
        // Use world right (Y) as fallback rotation axis.
        Vec3 axis = Vec3::Right().Cross(from);
        if (axis.lengthSq() < 1e-6f) {
            axis = Vec3::Up().Cross(from);
        }
        axis = axis.Normalized();
        return Quat::FromAxisAngle(axis, Math::PI);
    }

    // General case: rotation axis = cross(from, to), angle = acos(dot).
    const Vec3 axis = from.Cross(to).Normalized();
    const float angle = std::acos(std::clamp(d, -1.0f, 1.0f));
    return Quat::FromAxisAngle(axis, angle);
}

// ─────────────────────────────────────────────────────────────────────────────
// RandomRotationAroundAxis
// ─────────────────────────────────────────────────────────────────────────────

Quat DecalManager::RandomRotationAroundAxis(const Vec3& axis) {
    const float angle = rng_.Range(0.0f, Math::TAU);
    return Quat::FromAxisAngle(axis, angle);
}

// ─────────────────────────────────────────────────────────────────────────────
// IsSphereInFrustum -- extract 6 planes from VP matrix and test sphere
// ─────────────────────────────────────────────────────────────────────────────

bool DecalManager::IsSphereInFrustum(const Mat4& viewProjection,
                                      const Vec3& center,
                                      float radius) {
    // Extract frustum planes from the combined view-projection matrix.
    // Each plane is (A, B, C, D) where Ax + By + Cz + D = 0.
    // We use the Griggs/Hartmann method: rows of the VP matrix.
    const auto& m = viewProjection.m;

    // The VP matrix in row-major layout (DirectX XMFLOAT4X4):
    //   row 0: _11 _12 _13 _14
    //   row 1: _21 _22 _23 _24
    //   row 2: _31 _32 _33 _34
    //   row 3: _41 _42 _43 _44
    //
    // Left   = row3 + row0
    // Right  = row3 - row0
    // Bottom = row3 + row1
    // Top    = row3 - row1
    // Near   = row2          (DX convention: 0 <= z <= w)
    // Far    = row3 - row2

    struct Plane {
        float a, b, c, d;
    };

    auto makePlane = [](float a, float b, float c, float d) -> Plane {
        const float invLen = 1.0f / std::sqrt(a * a + b * b + c * c);
        return {a * invLen, b * invLen, c * invLen, d * invLen};
    };

    const Plane planes[6] = {
        // Left
        makePlane(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41),
        // Right
        makePlane(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41),
        // Bottom
        makePlane(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42),
        // Top
        makePlane(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42),
        // Near
        makePlane(m._13, m._23, m._33, m._43),
        // Far
        makePlane(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43),
    };

    // Test sphere against each plane. If the sphere is entirely behind any
    // plane (signed distance < -radius), it is outside the frustum.
    for (const auto& p : planes) {
        const float dist = p.a * center.x + p.b * center.y + p.c * center.z + p.d;
        if (dist < -radius) {
            return false;
        }
    }

    return true;
}

} // namespace uz
