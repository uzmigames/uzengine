#include "vfx_collision_query_di.h"

namespace uz {

VfxRaycastResult VfxRaycastMiss(uint64_t particleID) noexcept {
    VfxRaycastResult r;
    r.hit        = 0u;
    r.particleID = particleID;
    return r;
}

std::vector<VfxRaycastResult> VfxCollisionQueryResolver::Resolve() {
    std::vector<VfxRaycastResult> results;
    results.reserve(m_pendingRequests.size());

    for (const auto& req : m_pendingRequests) {
        if (m_sink) {
            VfxRaycastResult r = m_sink(req);
            // Defensive: always echo the source particle ID regardless
            // of what the sink filled in — keeps the routing contract
            // under the resolver's control.
            r.particleID = req.particleID;
            results.push_back(r);
        } else {
            // No physics available → synthesise a miss so callers have a
            // well-defined result per request rather than mismatched counts.
            results.push_back(VfxRaycastMiss(req.particleID));
        }
    }

    m_pendingRequests.clear();
    return results;
}

} // namespace uz
