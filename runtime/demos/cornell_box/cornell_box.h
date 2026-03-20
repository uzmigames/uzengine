/// Cornell Box Demo — GI Color Bleeding Test
///
/// Renders a classic Cornell box scene to test global illumination:
/// - White floor, ceiling, and back wall
/// - Red left wall, green right wall
/// - White sphere in the center
/// - Directional sun from above-right
/// - Sky atmosphere + sky light for ambient
///
/// With working GI, red light should bleed onto the sphere from the left wall
/// and green light from the right wall. Without GI, the sphere should appear
/// uniformly lit with no color bleeding.

#pragma once

#include "application.h"
#include "entity.h"
#include "material_asset.h"

namespace uz {

class CornellBoxDemo final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnStop() override;

private:
    // ── Wall/floor/ceiling entities ────────────────────────────────────────
    Entity floor_{};
    Entity ceiling_{};
    Entity backWall_{};
    Entity leftWall_{};
    Entity rightWall_{};

    // ── Center sphere ──────────────────────────────────────────────────────
    Entity sphere_{};

    // ── Lights ─────────────────────────────────────────────────────────────
    Entity sun_{};

    float totalTime_ = 0.f;

    // ── Materials (solid colors, no textures) ──────────────────────────────
    Material whiteMat_;
    Material redMat_;
    Material greenMat_;
};

} // namespace uz
