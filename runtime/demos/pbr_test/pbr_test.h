/// PBR Test — Application-based demo.
///
/// Renders 9 PBR spheres with diverse materials (Gold, Concrete, Grass, Metal,
/// MetalCorroded, RattanWeave, Sand, StoneQuartzite, Stonebricks) arranged in
/// two rows, plus a ground plane, directional sun, 4 colored point lights,
/// sky atmosphere, sky light, and height fog.
///
/// The ground plane demonstrates the MaterialGraph pipeline: loads
/// `mi_procgrid.uzmat`, bakes it to textures via MaterialBaker, and
/// renders using the standard texture-based material path.

#pragma once

#include "application.h"
#include "entity.h"
#include "material_asset.h"
#include "material_asset_graph.h"
#include "material_instance.h"
#include "material_shader_cache.h"
#include "rhi_types.h"

namespace uz {

class PBRTest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnStop() override;

private:
    static constexpr int kSphereCount = 11;

    Entity spheres_[kSphereCount]{};
    Entity ground_{};
    Entity sun_{};
    Entity pointLights_[4]{};

    float totalTime_ = 0.f;

    // ── Materials (9 diverse PBR material sets) ──────────────────────────

    Material goldMat_;
    Material concreteMat_;
    Material grassMat_;
    Material metalMat_;
    Material metalCorrodedMat_;
    Material rattanMat_;
    Material sandMat_;
    Material quartziteMat_;
    Material stoneBrickMat_;
    Material unlitMat_;           ///< Unlit test material — flat red, no lighting.
    Material sssMat_;             ///< Subsurface scattering test — warm skin-like.

    MaterialInstance wetBrickInst_;
    Material groundMat_;

    // ── Material graph integration ───────────────────────────────────────

    MaterialShaderCache materialCache_;     ///< Compilation cache for graph materials
    MaterialAssetGraph  graphMaterial_;     ///< Graph-based material loaded from .uzmat

    // ── GPU-baked material textures (owned, must be destroyed at shutdown) ──
    RHITextureHandle gpuBakedBaseColor_{};
    RHITextureHandle gpuBakedRoughness_{};
};

} // namespace uz
