/// FBX Test — Loads an FBX character and renders with PBR.
///
/// Demonstrates `uz::LoadFbx()` runtime API: loads MushroomMonster_Low.fbx,
/// auto-caches as .uzmesh, applies PBR textures, renders in deferred pipeline.

#pragma once

#include "application.h"
#include "entity.h"
#include "material_asset.h"
#include "material_asset_graph.h"
#include "material_shader_cache.h"
#include "rhi_types.h"

namespace uz {

class FBXTest final : public Application {
public:
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnStop() override;

private:
    Entity character_{};
    Entity ground_{};
    Entity sun_{};
    float totalTime_ = 0.f;
    Material characterMat_;
    Material groundMat_;
    MaterialShaderCache materialCache_;
    MaterialAssetGraph graphMaterial_;
    RHITextureHandle gpuBakedBaseColor_{};
    RHITextureHandle gpuBakedRoughness_{};
};

} // namespace uz
