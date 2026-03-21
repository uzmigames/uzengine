/// FBX Test — Loads an FBX character and renders with PBR.
///
/// Demonstrates `uz::LoadFbx()` runtime API: loads MushroomMonster_Low.fbx,
/// auto-caches as .uzmesh, applies PBR textures, renders in deferred pipeline.

#pragma once

#include "application.h"
#include "entity.h"
#include "material_asset.h"

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
};

} // namespace uz
