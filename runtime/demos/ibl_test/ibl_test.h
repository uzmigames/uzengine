/// IBL Test — Application-based demo.
///
/// Renders 5 cubes with Stonebricks PBR textures under IBL ambient lighting.
/// SceneRenderer handles IBL baking internally; this demo only sets up the scene.

#pragma once

#include "application.h"
#include "entity.h"

namespace uz {

/// IBL Test demo — 5 Stonebricks cubes + directional sun.
///
/// Validates that SceneRenderer's IBL pipeline (irradiance + prefiltered
/// specular + BRDF LUT) produces correct ambient lighting on PBR materials.
class IBLTest final : public Application {
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
