#pragma once

/// Generic physics collision-query data interface for VFX simulation
/// shaders.
///
/// Beyond the screen-space depth-buffer collision (3.85.0) and the async
/// GPU depth trace (4.23.0), some effects need real physics-scene raycast
/// results (obstacle awareness, material-aware impact responses, terrain
/// sticking). Niagara exposes this via
/// `UNiagaraDataInterfaceCollisionQuery` with
/// `PhysicsRaycast` / `PhysicsSphereCast` returning hit point / normal /
/// material.
///
/// # Pipeline shape
///
/// ```
///   [Writer emitter update CS]
///          │  fills VfxRaycastRequest in an append buffer
///          ▼
///   [GPU readback framework (phase 4.26.0)]
///          │  ships the request list to CPU (1-frame latency)
///          ▼
///   [VfxCollisionQueryResolver]
///          │  walks requests, dispatches to Jolt physics scene query,
///          │  produces VfxRaycastResult per request
///          ▼
///   [Result upload next frame]
///          │  reader emitter's update CS reads result[particleID]
///          ▼
///   [Particle reacts — bounce / stick / death]
/// ```
///
/// The CPU pipeline shipped here owns the resolver + request/result
/// layout; GPU append-buffer + upload wiring land in
/// `phase7_7.14.1-vfx-collision-query-di-gpu-binding`.
///
/// # Latency
///
/// One-frame latency is accepted — the physics world evolves slowly
/// compared to VFX simulation, and a frame of staleness is invisible for
/// the typical impact / stick / scorch-decal scenarios this DI
/// addresses. Callers that need same-frame accuracy use the depth trace
/// (4.23.0) instead.
///
/// @ref UE5 UNiagaraDataInterfaceCollisionQuery
/// @ref phase4d_4.26.0 GPU readback framework
/// @ref phase4-4.2.0 Jolt physics integration

#include "vector3.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Request / result structs — mirror the eventual HLSL bindings byte-for-byte.
// ─────────────────────────────────────────────────────────────────────────────

/// Raycast request emitted by a simulation shader. Each request carries
/// the source particle ID so the reply can be routed back per-particle
/// after the async resolve.
struct VfxRaycastRequest {
    Vec3     origin    {};     ///< World-space start of the ray.
    float    maxDist   = 100.0f; ///< Max trace distance (cm).
    Vec3     direction {0.0f, 0.0f, -1.0f};  ///< Unit direction (caller normalises).
    uint64_t particleID = 0u;  ///< Source particle persistent ID (7.4.0).
    uint32_t layerMask = 0xFFFFFFFFu; ///< Bitmask of physics layers the ray hits.
    uint32_t _pad      = 0u;   ///< Padding for 16-byte alignment in GPU upload.
};

/// Result of one resolved raycast. Paired with the request by index in
/// the two parallel arrays owned by the resolver.
struct VfxRaycastResult {
    uint32_t hit            = 0u;           ///< 0 = miss, 1 = hit.
    uint32_t hitMaterialID  = 0u;           ///< Caller-supplied material ID on hit.
    float    hitDistance    = 0.0f;         ///< Distance from origin (cm).
    float    _pad0          = 0.0f;
    Vec3     hitPosition    {};
    float    _pad1          = 0.0f;
    Vec3     hitNormal      {};
    float    _pad2          = 0.0f;
    uint64_t particleID     = 0u;           ///< Echoed from the request.
    uint64_t _pad3          = 0u;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resolver — CPU pipeline managing request batching + physics dispatch.
// ─────────────────────────────────────────────────────────────────────────────

/// Callback the resolver invokes per request to consult the physics
/// scene. Returns a filled-in result. Typically backed by the engine's
/// Jolt integration (phase4-4.2.0); tests supply a synthetic callback
/// so the resolver can be exercised without a physics dependency.
using VfxRaycastSink = std::function<VfxRaycastResult(const VfxRaycastRequest&)>;

/// Queue-based resolver for raycast requests. Not thread-safe; one
/// resolver lives per scene.
class VfxCollisionQueryResolver {
public:
    /// Replaces the physics sink. Passing an empty function disables
    /// resolution entirely (every request resolves to a miss).
    void SetPhysicsSink(VfxRaycastSink sink) { m_sink = std::move(sink); }

    /// Appends a single request to the pending queue. Typically called
    /// when the readback framework hands back the GPU-side append buffer.
    void SubmitRequest(const VfxRaycastRequest& req) {
        m_pendingRequests.push_back(req);
    }

    /// Batch append helper.
    void SubmitRequests(const VfxRaycastRequest* reqs, size_t count) {
        if (!reqs || count == 0u) return;
        m_pendingRequests.insert(m_pendingRequests.end(), reqs, reqs + count);
    }

    /// Walks the pending queue, calls the sink (or fabricates a miss if
    /// none is installed), and returns the result array in request
    /// order. Clears the pending queue as it goes.
    [[nodiscard]] std::vector<VfxRaycastResult> Resolve();

    /// Number of requests waiting for resolution.
    [[nodiscard]] uint32_t PendingCount() const noexcept {
        return static_cast<uint32_t>(m_pendingRequests.size());
    }

    /// True when a physics sink is installed.
    [[nodiscard]] bool HasSink() const noexcept {
        return static_cast<bool>(m_sink);
    }

private:
    VfxRaycastSink                  m_sink;
    std::vector<VfxRaycastRequest>  m_pendingRequests;
};

/// Convenience helper: produces a VfxRaycastResult representing a miss
/// (hit = 0). Caller-supplied particleID is echoed so per-particle
/// routing still works on misses.
[[nodiscard]] VfxRaycastResult VfxRaycastMiss(uint64_t particleID) noexcept;

} // namespace uz
