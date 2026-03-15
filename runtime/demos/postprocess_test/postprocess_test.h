/// Post-Process Test — Application-based demo.
///
/// Renders 3 cubes with emissive white material and a directional sun.
/// All engine boilerplate is handled by Application + SceneRenderer.

#pragma once

#include "application.h"
#include "entity.h"

namespace uz {

/// Post-Process Test demo — 3 cubes + directional sun, F1/F2 render path toggle.
///
/// Validates that SceneRenderer's post-processing chain (tonemap, FXAA)
/// works correctly with the Application abstraction.
class PostProcessTest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    static constexpr int kCubeCount = 3;

    Entity cubes_[kCubeCount]{};
    Entity sun_{};
};

} // namespace uz
