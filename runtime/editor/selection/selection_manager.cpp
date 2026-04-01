/// SelectionManager — Entity picking via ray-AABB intersection.

#include "selection_manager.h"

#include "camera.h"
#include "world.h"
#include "scene_component.h"
#include "static_mesh_component.h"
#include "environment/decal_component.h"
#include "scene_renderer.h"
#include "mat4.h"
#include "log.h"

#include <cfloat>
#include <cmath>
#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(Editor);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────

void SelectionManager::Select(Entity entity) {
    m_selected = entity;
    if (entity.IsValid()) {
        UZ_LOG(Editor, Info, "Selected entity {}", entity.id);
    }
}

void SelectionManager::Deselect() {
    if (m_selected.IsValid()) {
        UZ_LOG(Editor, Info, "Deselected entity {}", m_selected.id);
    }
    m_selected = Entity::Invalid();
}

// ─────────────────────────────────────────────────────────────────────────────
// Unprojection
// ─────────────────────────────────────────────────────────────────────────────

Vec3 UnprojectPoint(const Mat4& invViewProj, float ndcX, float ndcY, float ndcZ) {
    // Build a clip-space point: (ndcX, ndcY, ndcZ, 1)
    const Vec4 clipPt{ndcX, ndcY, ndcZ, 1.0f};

    // Transform by inverse VP
    const Vec4 worldPt = invViewProj * clipPt;

    // Perspective divide (w != 0 for valid projections)
    if (std::abs(worldPt.w) < 1e-8f) {
        return Vec3{0.0f, 0.0f, 0.0f};
    }

    const float invW = 1.0f / worldPt.w;
    return Vec3{worldPt.x * invW, worldPt.y * invW, worldPt.z * invW};
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray-AABB Intersection (Slab Method)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Kay & Kajiya 1986 "Ray Tracing Complex Scenes" — slab intersection method
bool RayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                        const Vec3& aabbMin, const Vec3& aabbMax,
                        float& tMin, float& tMax) {
    // Initialize to full ray extent
    tMin = -FLT_MAX;
    tMax = FLT_MAX;

    const float* origin = rayOrigin.data();
    const float* dir = rayDir.data();
    const float* bmin = aabbMin.data();
    const float* bmax = aabbMax.data();

    for (int i = 0; i < 3; ++i) {
        if (std::abs(dir[i]) < 1e-8f) {
            // Ray is parallel to this slab — check if origin is inside
            if (origin[i] < bmin[i] || origin[i] > bmax[i]) {
                return false;
            }
        } else {
            const float invD = 1.0f / dir[i];
            float t0 = (bmin[i] - origin[i]) * invD;
            float t1 = (bmax[i] - origin[i]) * invD;

            // Ensure t0 <= t1
            if (t0 > t1) {
                const float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }

            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);

            if (tMin > tMax) {
                return false;
            }
        }
    }

    // Hit is valid if the exit point is in front of the ray origin
    return tMax >= 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen-space entity picking
// ─────────────────────────────────────────────────────────────────────────────

void SelectionManager::SelectAtScreenPos(float screenX, float screenY,
                                          float viewportWidth, float viewportHeight,
                                          const Camera* camera, World* world,
                                          const SceneRenderer* renderer) {
    if (!camera || !world) return;
    if (viewportWidth < 1.0f || viewportHeight < 1.0f) return;

    // ── Convert screen coordinates to NDC ──────────────────────────────
    const float ndcX =  (screenX / viewportWidth)  * 2.0f - 1.0f;
    const float ndcY = -(screenY / viewportHeight) * 2.0f + 1.0f; // flip Y (screen Y down, NDC Y up)

    // ── Build inverse view-projection matrix ───────────────────────────
    const Mat4 vp = camera->GetViewProjection();
    const Mat4 invVP = vp.Inverse();

    // ── Construct world-space ray ──────────────────────────────────────
    // UzEngine uses reversed-Z: near plane = NDC Z=1, far plane = NDC Z=0
    const Vec3 nearPt = UnprojectPoint(invVP, ndcX, ndcY, 1.0f);
    const Vec3 farPt  = UnprojectPoint(invVP, ndcX, ndcY, 0.0f);

    const Vec3 rayOrigin = camera->GetPosition();
    const Vec3 rayDirRaw = Vec3{farPt.x - nearPt.x, farPt.y - nearPt.y, farPt.z - nearPt.z};
    const Vec3 rayDir = rayDirRaw.Normalized();

    // ── Test ray against all entities with SceneComponent + StaticMeshComponent
    float closestT = FLT_MAX;
    Entity closestEntity = Entity::Invalid();

    // Default AABB half-extent for entities without mesh bounds (50cm unit cube)
    constexpr float kDefaultHalfExtent = 50.0f;

    auto view = world->View<SceneComponent, StaticMeshComponent>();
    for (auto [enttEntity, scene, mesh] : view.each()) {
        // ── Compute world-space AABB ────────────────────────────────
        const Vec3 worldPos = scene.GetWorldPosition();
        const Vec3 localScale = scene.GetScale();

        Vec3 objectMin{-kDefaultHalfExtent, -kDefaultHalfExtent, -kDefaultHalfExtent};
        Vec3 objectMax{ kDefaultHalfExtent,  kDefaultHalfExtent,  kDefaultHalfExtent};

        // Try to get actual mesh bounds from the renderer
        if (renderer && mesh.meshLoaded) {
            Vec3 meshMin, meshMax;
            if (renderer->GetMeshBounds(mesh.meshId, meshMin, meshMax)) {
                objectMin = meshMin;
                objectMax = meshMax;
            }
        }

        // Scale object-space bounds by local scale
        const Vec3 scaledMin{
            objectMin.x * localScale.x,
            objectMin.y * localScale.y,
            objectMin.z * localScale.z
        };
        const Vec3 scaledMax{
            objectMax.x * localScale.x,
            objectMax.y * localScale.y,
            objectMax.z * localScale.z
        };

        // Ensure min < max after scaling (negative scale flips)
        const Vec3 aabbMin{
            std::min(scaledMin.x, scaledMax.x) + worldPos.x,
            std::min(scaledMin.y, scaledMax.y) + worldPos.y,
            std::min(scaledMin.z, scaledMax.z) + worldPos.z
        };
        const Vec3 aabbMax{
            std::max(scaledMin.x, scaledMax.x) + worldPos.x,
            std::max(scaledMin.y, scaledMax.y) + worldPos.y,
            std::max(scaledMin.z, scaledMax.z) + worldPos.z
        };

        // ── Ray-AABB intersection ──────────────────────────────────
        float tMin = 0.0f;
        float tMax = 0.0f;
        if (RayIntersectsAABB(rayOrigin, rayDir, aabbMin, aabbMax, tMin, tMax)) {
            // Skip objects where the ray starts inside the AABB (tMin < 0)
            // unless nothing else hits. This prevents the ground plane
            // (which the camera is always inside) from stealing selection.
            if (tMin < 0.0f) continue;  // ray inside box — skip

            if (tMin < closestT) {
                closestT = tMin;
                closestEntity = world->EntityFromEntt(enttEntity);
            }
        }
    }

    // ── Also test DecalComponent entities (no mesh — use transform scale as AABB)
    {
        auto decalView = world->View<SceneComponent, DecalComponent>();
        for (auto [enttEntity, scene, decal] : decalView.each()) {
            if (!decal.enabled) continue;

            const Vec3 worldPos = scene.GetWorldPosition();
            const Vec3 localScale = scene.GetScale();

            // Decal OBB = unit cube [-0.5, 0.5]³ × scale
            const Vec3 halfExtent{
                localScale.x * 0.5f,
                localScale.y * 0.5f,
                localScale.z * 0.5f
            };

            const Vec3 aabbMin{worldPos.x - std::abs(halfExtent.x),
                               worldPos.y - std::abs(halfExtent.y),
                               worldPos.z - std::abs(halfExtent.z)};
            const Vec3 aabbMax{worldPos.x + std::abs(halfExtent.x),
                               worldPos.y + std::abs(halfExtent.y),
                               worldPos.z + std::abs(halfExtent.z)};

            float tMin = 0.0f;
            float tMax = 0.0f;
            if (RayIntersectsAABB(rayOrigin, rayDir, aabbMin, aabbMax, tMin, tMax)) {
                if (tMin < 0.0f) continue;
                if (tMin < closestT) {
                    closestT = tMin;
                    closestEntity = world->EntityFromEntt(enttEntity);
                }
            }
        }
    }

    if (closestEntity.IsValid()) {
        Select(closestEntity);
    } else {
        Deselect();
    }
}

} // namespace uz
