/// Mesh Test — Application-based demo.
///
/// Renders a single lit cube with a directional sun light.
/// All engine boilerplate is handled by Application + SceneRenderer.

#pragma once

#include "application.h"
#include "entity.h"

namespace uz {

/// Mesh Test demo — renders a cube with directional lighting.
///
/// Demonstrates the minimal uz::Application usage: one mesh entity,
/// one directional light, F1/F2 render path toggle.
class MeshTest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    Entity cube_{};
    Entity sun_{};
};

} // namespace uz
