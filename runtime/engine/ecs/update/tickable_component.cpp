/// TickableComponent implementation.

#include "tickable_component.h"

namespace uz {

TickGroup TickableComponent::GetTickGroup() const {
    return TickGroup::Update;
}

} // namespace uz
