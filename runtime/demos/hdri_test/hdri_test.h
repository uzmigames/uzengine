/// HDRI Environment Test — Loads an equirectangular .exr panorama and uses it
/// as the sky environment for Image-Based Lighting.
///
/// Demonstrates:
/// - EXR texture loading via `TextureLoader::LoadHDR()`
/// - Equirectangular-to-cubemap conversion via IBLBaker compute shader
/// - IBL baking (irradiance + prefiltered specular) from the HDRI cubemap
/// - PBR spheres reflecting the HDRI environment

#pragma once

#include "application.h"
#include "entity.h"
#include "material_asset.h"
#include "rhi_types.h"

namespace uz {

class HDRITest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnStop() override;

private:
    static constexpr int kSphereCount = 7;

    Entity spheres_[kSphereCount]{};
    Entity ground_{};
    Entity sun_{};

    float totalTime_ = 0.f;

    // ── HDRI texture (owned, must be destroyed at shutdown) ──────────────
    RHITextureHandle hdriTexture_{};

    // ── Materials (varying metallic + roughness for IBL showcase) ────────
    Material metalMat_{};
    Material roughMat_{};
    Material chromeMatLow_{};
    Material chromeMatMid_{};
    Material chromeMatHigh_{};
    Material plasticMat_{};
    Material dielectricMat_{};
    Material groundMat_{};
};

} // namespace uz
