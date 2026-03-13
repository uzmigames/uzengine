/// TickSubsystem implementation.

#include "tick_subsystem.h"
#include "world.h"
#include "log.h"
#include "profiler.h"

namespace uz {

bool TickSubsystem::Init() {
    UZ_LOG(ECS, Info, "TickSubsystem initialized");
    return true;
}

void TickSubsystem::Shutdown() {
    m_tickSystem.Clear();
    m_world = nullptr;
    UZ_LOG(ECS, Info, "TickSubsystem shut down");
}

void TickSubsystem::Tick(float deltaTime) {
    if (!m_world) return;

    UZ_PROFILE_FUNCTION();

    m_tickSystem.Tick(TickGroup::PrePhysics, deltaTime, *m_world);
    m_tickSystem.Tick(TickGroup::PostPhysics, deltaTime, *m_world);
    m_tickSystem.Tick(TickGroup::Update, deltaTime, *m_world);
    m_tickSystem.Tick(TickGroup::PostUpdate, deltaTime, *m_world);
}

} // namespace uz
