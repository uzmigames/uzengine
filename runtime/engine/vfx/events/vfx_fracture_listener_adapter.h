#pragma once

/// VFX fracture listener adapter — bridges PhysicsListenerRegistry to
/// VfxPhysicsFractureBridge (phase 9.18.0).
///
/// Implements `IPhysicsFractureListener` (from phase 10.10.0) and forwards
/// every `OnFracture` call to a `VfxPhysicsFractureBridge` (from phase
/// 8.24.0). This is the missing link that connects the physics world's
/// fracture callbacks to the VFX event pipeline.
///
/// # Registration pattern
///
/// ```cpp
/// // At VFX subsystem init:
/// VfxPhysicsFractureBridge bridge(&eventQueue, "fracture");
/// VfxFractureListenerAdapter adapter(&bridge);
/// auto handle = PhysicsListenerRegistry::Instance().RegisterFractureListener(&adapter);
///
/// // At VFX subsystem shutdown:
/// PhysicsListenerRegistry::Instance().UnregisterFractureListener(handle);
/// ```
///
/// # Lifecycle
///
/// The adapter does not own the bridge — the VFX subsystem owns both.
/// Both must outlive the physics listener registration. On scene unload
/// the caller unregisters the listener handle and optionally resets
/// the bridge's target queue.
///
/// @ref engine/physics/physics_listener_registry.h (IPhysicsFractureListener)
/// @ref engine/vfx/events/vfx_physics_fracture.h (VfxPhysicsFractureBridge)
/// @ref docs/engine/25-vfx/41-physics-fracture-events.md

#include "engine/physics/physics_listener_registry.h"
#include "engine/vfx/events/vfx_physics_fracture.h"

namespace uz {

/// Adapter bridging `IPhysicsFractureListener` to `VfxPhysicsFractureBridge`.
class VfxFractureListenerAdapter final : public IPhysicsFractureListener {
public:
    /// Constructs the adapter.
    ///
    /// # Arguments
    /// - `bridge` — the VFX fracture bridge to forward events to.
    ///   Must outlive the adapter. Null disables forwarding.
    explicit VfxFractureListenerAdapter(VfxPhysicsFractureBridge* bridge)
        : bridge_(bridge) {}

    /// Forwards the fracture event to the bridge.
    bool OnFracture(const VfxFractureEventInput& event) override {
        if (bridge_ == nullptr) return false;
        return bridge_->OnFracture(event);
    }

    /// Rebinds to a different bridge (e.g. on scene reload).
    void SetBridge(VfxPhysicsFractureBridge* bridge) noexcept {
        bridge_ = bridge;
    }

    /// Returns the currently bound bridge (for inspection / tests).
    [[nodiscard]] VfxPhysicsFractureBridge* GetBridge() const noexcept {
        return bridge_;
    }

private:
    VfxPhysicsFractureBridge* bridge_ = nullptr;
};

} // namespace uz
