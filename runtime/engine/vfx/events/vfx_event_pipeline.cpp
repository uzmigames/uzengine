#include "vfx_event_pipeline.h"

#include <algorithm>

namespace uz {

uint32_t VfxEventPipeline::AddReceiver(uint32_t receiverEmitterHandle,
                                        const VfxEventReceiverConfig& config) {
    ReceiverEntry e;
    e.receiverEmitterHandle = receiverEmitterHandle;
    e.config                = config;
    m_receivers.push_back(std::move(e));
    return static_cast<uint32_t>(m_receivers.size() - 1u);
}

void VfxEventPipeline::RemoveReceiver(uint32_t receiverEmitterHandle) {
    m_receivers.erase(
        std::remove_if(m_receivers.begin(), m_receivers.end(),
                        [receiverEmitterHandle](const ReceiverEntry& e) {
                            return e.receiverEmitterHandle == receiverEmitterHandle;
                        }),
        m_receivers.end());
}

void VfxEventPipeline::SubmitPayload(const VfxEventPayload& payload) {
    m_pendingPayloads.push_back(payload);
}

void VfxEventPipeline::SubmitPayloads(const VfxEventPayload* payloads, size_t count) {
    if (!payloads || count == 0) return;
    m_pendingPayloads.insert(m_pendingPayloads.end(), payloads, payloads + count);
}

std::vector<VfxEventSpawnRequest> VfxEventPipeline::Dispatch() {
    // Reuse the scratch vector. Start from a fresh empty view but keep
    // the capacity so repeated frames don't re-allocate.
    m_pendingRequests.clear();

    for (const auto& payload : m_pendingPayloads) {
        for (const auto& entry : m_receivers) {
            if (entry.config.sourceEmitterHandle != payload.sourceEmitterHandle) {
                continue;
            }
            if (!entry.config.Accepts(payload.kind)) {
                continue;
            }
            VfxEventSpawnRequest req;
            req.receiverEmitterHandle = entry.receiverEmitterHandle;
            req.burstCount            = entry.config.burstCount;
            req.spawnPosition         = payload.position;
            req.tintColor             = payload.color;
            req.triggeringKind        = payload.kind;
            req.sourceParticleID      = payload.sourceParticleID;

            switch (entry.config.action) {
                case VfxEventReceiverAction::InheritVelocity: {
                    const float f = entry.config.velocityInheritance;
                    req.inheritedVelocity = Vec3{
                        payload.velocity.x * f,
                        payload.velocity.y * f,
                        payload.velocity.z * f,
                    };
                    break;
                }
                case VfxEventReceiverAction::SpawnAtPosition:
                case VfxEventReceiverAction::SpawnBurst:
                default:
                    // Zero velocity inheritance.
                    req.inheritedVelocity = Vec3{};
                    break;
            }
            m_pendingRequests.push_back(req);
        }
    }

    m_pendingPayloads.clear();

    // Return a copy so callers can consume freely; the scratch buffer
    // keeps its capacity for the next frame.
    return m_pendingRequests;
}

} // namespace uz
