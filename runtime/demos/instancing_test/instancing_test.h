/// Instancing Test -- GPU instancing stress test demo.
///
/// Spawns 1000 identical cubes (10x10x10 grid) with the same mesh and material
/// to demonstrate draw call collapse via GPU instancing. Without instancing
/// this would be 1000 draw calls; with instancing the InstanceBatcher collapses
/// them into 1-2 DrawIndexedInstanced calls.

#pragma once

#include "application.h"
#include "entity.h"

#include <vector>

namespace uz {

/// Instancing demo -- 1000 identical Stonebricks cubes in a 10x10x10 grid.
///
/// Scene is static (no per-frame animation). The purpose is to validate
/// that the InstanceBatcher correctly groups identical mesh+material pairs
/// and issues a single instanced draw call.
///
/// # Controls
/// - F1: Switch to Deferred render path
/// - F2: Switch to Forward render path
class InstancingTest final : public Application {
public:
    /// Returns window title, resolution, camera placement, and render path.
    ApplicationConfig Configure() override;

    /// Creates 1000 cubes, ground plane, directional sun, and sky atmosphere.
    void OnStart() override;

    /// Handles render path toggle input. Scene is otherwise static.
    void OnUpdate(float dt) override;

private:
    static constexpr int kGridSize  = 10;
    static constexpr int kCubeCount = kGridSize * kGridSize * kGridSize;

    std::vector<Entity> cubes_;
    Entity ground_{};
    Entity sun_{};

    float totalTime_ = 0.f;
};

} // namespace uz
