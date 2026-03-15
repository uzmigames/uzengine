/// Camera Test — Application-based demo.
///
/// Renders a single white cube with a directional sun light.
/// All engine boilerplate is handled by Application + SceneRenderer.

#pragma once

#include "application.h"
#include "entity.h"

namespace uz {

/// Camera Test demo — renders a cube with directional lighting and FPS camera.
///
/// Demonstrates the minimal uz::Application usage: one mesh entity,
/// one directional light, F1/F2 render path toggle.
class CameraTest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    Entity cube_{};
    Entity sun_{};
};

} // namespace uz
