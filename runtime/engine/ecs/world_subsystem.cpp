/// UzEngine World Subsystem Implementation

#include "world_subsystem.h"

namespace uz {

bool WorldSubsystem::Init() {
    world_ = std::make_unique<World>();
    UZ_LOG(ECS, Info, "WorldSubsystem initialized");
    return true;
}

void WorldSubsystem::Shutdown() {
    if (world_) {
        world_->Clear();
        world_.reset();
        UZ_LOG(ECS, Info, "WorldSubsystem shut down");
    }
}

} // namespace uz
