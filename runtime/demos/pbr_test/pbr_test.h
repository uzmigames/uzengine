/// PBR Test — Application-based demo.
///
/// Renders 5 PBR cubes + ground plane + directional sun + 4 colored point lights.
/// All engine boilerplate is handled by Application + SceneRenderer.

#pragma once

#include "application.h"
#include "entity.h"

namespace uz {

/// PBR Test demo — 5 Stonebricks cubes, 4 orbiting point lights, directional sun.
///
/// Scene setup and per-frame light animation only.
/// All rendering infrastructure is owned by SceneRenderer.
class PBRTest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    static constexpr int kCubeCount = 5;

    Entity cubes_[kCubeCount]{};
    Entity ground_{};
    Entity sun_{};
    Entity pointLights_[4]{};

    float totalTime_ = 0.f;
};

} // namespace uz
