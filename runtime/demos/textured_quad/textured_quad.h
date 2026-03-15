/// Textured Quad — Application-based demo.
///
/// Renders a flat ground plane with a checker texture, viewed from above.
/// All engine boilerplate is handled by Application + SceneRenderer.

#pragma once

#include "application.h"
#include "entity.h"

namespace uz {

/// Textured Quad demo — checker-textured ground plane + directional sun.
///
/// Camera looks straight down at a scaled cube (ground plane).
/// Validates texture loading and UV mapping via SceneRenderer.
class TexturedQuad final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    Entity ground_{};
    Entity sun_{};
};

} // namespace uz
