/// VFX event queue implementation.
///
/// All methods are O(1) amortized except Drain() which is O(1) via move.

#include "engine/vfx/events/vfx_event.h"

#include <utility>

namespace uz {

// ---------------------------------------------------------------------------
// VFXEventQueue
// ---------------------------------------------------------------------------

void VFXEventQueue::Emit(const std::string& name, uint32_t emissionCount,
                         float velocityScale)
{
    pending_.push_back(VFXPendingEvent{name, emissionCount, velocityScale});
}

void VFXEventQueue::EmitWithPayloads(const std::string& name,
                                      std::vector<EventPayload>&& payloads,
                                      float velocityScale)
{
    VFXPendingEvent event;
    event.name          = name;
    event.emissionCount = static_cast<uint32_t>(payloads.size());
    event.velocityScale = velocityScale;
    event.payloads      = std::move(payloads);
    pending_.push_back(std::move(event));
}

void VFXEventQueue::EmitFromDef(const VFXEventDef& def)
{
    pending_.push_back(
        VFXPendingEvent{def.name, def.emissionCount, def.velocityScale});
}

std::vector<VFXPendingEvent> VFXEventQueue::Drain()
{
    std::vector<VFXPendingEvent> result = std::move(pending_);
    pending_.clear(); // moved-from vector is valid but unspecified; reset it
    return result;
}

bool VFXEventQueue::HasPending() const noexcept
{
    return !pending_.empty();
}

size_t VFXEventQueue::GetPendingCount() const noexcept
{
    return pending_.size();
}

void VFXEventQueue::Clear()
{
    pending_.clear();
}

} // namespace uz
