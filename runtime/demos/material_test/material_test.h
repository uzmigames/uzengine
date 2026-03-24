/// Material Test — Application-based demo.
///
/// Renders 5 objects showcasing all material blend modes in a single scene:
/// 1. Emissive neon sphere  — bright cyan glow (intensity=10), drives bloom.
/// 2. Masked plane          — alpha-tested checkerboard (alphaClip=0.5).
/// 3. Translucent sphere    — blue glass, opacity=0.3.
/// 4. Additive sphere       — hologram glow overlay.
/// 5. Opaque reference      — standard PBR dielectric for comparison.
///
/// All objects sit on a ground plane with directional sun, sky atmosphere,
/// sky light, and height fog for realistic lighting context.

#pragma once

#include "application.h"
#include "entity.h"
#include "material_asset.h"

namespace uz {

class MaterialTest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnStop() override;

private:
    // ── Test entities ─────────────────────────────────────────────────────
    Entity emissiveSphere_{};      ///< Neon glow (emissive, intensity=10).
    Entity maskedPlane_{};         ///< Chain-link fence (alpha-tested checkerboard).
    Entity translucentSphere_{};   ///< Colored glass (translucent, blue tint).
    Entity additiveSphere_{};      ///< Hologram overlay (additive blending).
    Entity opaqueSphere_{};        ///< Reference PBR dielectric.
    Entity ground_{};              ///< Ground plane.
    Entity sun_{};                 ///< Directional sun light.

    float totalTime_ = 0.f;

    // ── Materials ─────────────────────────────────────────────────────────
    Material emissiveMat_;
    Material maskedMat_;
    Material translucentMat_;
    Material additiveMat_;
    Material opaqueMat_;
    Material groundMat_;
};

} // namespace uz
