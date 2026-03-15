/// Deferred Test — Application-based demo.
///
/// Renders 5 PBR cubes in deferred mode with debug view mode keys (1-9).
/// All engine boilerplate is handled by Application + SceneRenderer.

#pragma once

#include "application.h"
#include "entity.h"

namespace uz {

/// Deferred Test demo — 5 Stonebricks cubes, directional sun, debug view modes.
///
/// Keys 1-9 select DebugViewMode channels, 0 returns to Lit.
/// F1/F2 toggle Deferred/Forward render path.
class DeferredTest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    static constexpr int kCubeCount = 5;

    Entity cubes_[kCubeCount]{};
    Entity sun_{};
};

} // namespace uz
