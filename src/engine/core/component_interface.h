#pragma once

// IComponent — base interface for all engine components (XE-3, COMP-1).
//
// Defines the minimal virtual interface that every component must implement.
// Uses explicit vtable dispatch (virtual methods) — no RTTI, no dynamic_cast.
// Downcasting is done via explicit as_*() methods that each component overrides
// to return `this` for its own type, or nullptr for all others.
//
// Thread-safety: Components live on the game thread. Access from the render
// thread must go through immutable SceneProxy snapshots (see scene_proxy.h).

#include "engine/core/string_id.h"

#include <cstdint>

namespace uz {

// Forward declarations for downcast targets
struct IRenderable;
struct IPhysicsBody;
struct IAudioSource;
struct ISceneProxy;

// ============================================================================
// IComponent — base class for all components
// ============================================================================

struct IComponent {
    virtual ~IComponent() = default;

    /// Unique type identifier for this component class.
    [[nodiscard]] virtual StringId type_id() const = 0;

    /// Human-readable type name for debug/editor display.
    [[nodiscard]] virtual const char* type_name() const = 0;

    // ---- Lifecycle hooks (optional overrides) --------------------------------

    /// Called when this component is attached to a scene node.
    virtual void on_attach() {}

    /// Called when this component is detached from a scene node.
    virtual void on_detach() {}

    /// Called once per frame tick (game thread).
    virtual void on_tick(float dt) { (void)dt; }

    // ---- Explicit downcasting — replaces dynamic_cast (XE-3) ----------------
    // Each family of components overrides the relevant as_*() to return `this`.
    // By default, all return nullptr (not that type).

    [[nodiscard]] virtual IRenderable*  as_renderable()  { return nullptr; }
    [[nodiscard]] virtual IPhysicsBody* as_physics_body() { return nullptr; }
    [[nodiscard]] virtual IAudioSource* as_audio_source() { return nullptr; }
};

// ============================================================================
// IRenderable — marker interface for components that produce render proxies
// ============================================================================

struct IRenderable {
    virtual ~IRenderable() = default;

    /// Create a new render-thread proxy for this component.
    /// The proxy must be a snapshot of the component's state.
    [[nodiscard]] virtual ISceneProxy* create_proxy() = 0;

    /// Returns true if this component has visible geometry this frame.
    [[nodiscard]] virtual bool is_visible() const { return true; }
};

// ============================================================================
// IPhysicsBody — marker interface for physics-enabled components
// ============================================================================

struct IPhysicsBody {
    virtual ~IPhysicsBody() = default;
};

// ============================================================================
// IAudioSource — marker interface for audio-emitting components
// ============================================================================

struct IAudioSource {
    virtual ~IAudioSource() = default;
};

} // namespace uz
