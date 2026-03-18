/// PBR Test — Application-based demo.
///
/// Renders 5 PBR cubes + ground plane + directional sun + 4 colored point lights.
/// All engine boilerplate is handled by Application + SceneRenderer.

#pragma once

#include "application.h"
#include "entity.h"
#include "material_asset.h"
#include "material_instance.h"

namespace uz {

/// PBR Test demo — Stonebricks sphere, ground plane, 4 orbiting point lights, directional sun.
///
/// Demonstrates the material system:
/// - `stoneBrickMat_`: Shared Material with PBR texture set.
/// - `wetBrickInst_`: MaterialInstance overriding roughness for a wet surface look.
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

    // ── Material system assets ───────────────────────────────────────────

    /// Shared PBR material for the Stonebricks surface.
    /// Lifetime: owned by this demo, outlives all entities that reference it.
    Material stoneBrickMat_;

    /// Material instance for wet variant — overrides roughness from parent.
    MaterialInstance wetBrickInst_;

    /// Simple ground material — checker texture only.
    Material groundMat_;
};

} // namespace uz
