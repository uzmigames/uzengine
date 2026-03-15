/// TickableComponent — base class for components that receive per-frame updates.
///
/// Extends `Component` with a virtual `OnTick(float)` callback dispatched by
/// the `TickSystem`. Each tickable declares a `TickGroup` to control execution
/// ordering relative to physics and other systems.
///
/// # Interval Ticking
/// Set `m_tickInterval` > 0 to throttle ticks. The component will only receive
/// `OnTick` when at least `m_tickInterval` seconds have elapsed since the last
/// tick. Overshoot time is preserved for the next interval.
///
/// # Thread Safety
/// All tick callbacks are called from the main thread.

#pragma once

#include "component.h"
#include "subsystem.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TickableComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Component that participates in per-frame tick dispatch.
///
/// Subclasses implement `OnTick(float)` and optionally override `GetTickGroup()`
/// to control when they execute relative to physics and other systems.
///
/// # Examples
/// ```cpp
/// class RotatorComponent : public TickableComponent {
/// public:
///     void OnTick(float dt) override {
///         angle += speed * dt;
///     }
///     float angle = 0.F;
///     float speed = 1.F;
/// };
/// ```
class TickableComponent : public Component {
public:
    ~TickableComponent() override = default;

    // Inherit non-copyable/non-movable from Component.

    // ── Tick Callback ────────────────────────────────────────────────────

    /// Called each frame (or at the configured interval) during the
    /// component's tick group.
    ///
    /// # Arguments
    /// * `deltaTime` - Elapsed time in seconds since last tick for this component.
    ///                 When using interval ticking, this is the actual elapsed time
    ///                 (which may exceed `m_tickInterval` by up to one frame).
    virtual void OnTick(float deltaTime) = 0;

    /// Returns the tick group this component belongs to.
    ///
    /// Override to change execution ordering. Default: `TickGroup::Update`.
    ///
    /// # Returns
    /// The `TickGroup` for this component's tick dispatch.
    [[nodiscard]] virtual TickGroup GetTickGroup() const;

    // ── Tick Control ─────────────────────────────────────────────────────

    /// Whether this component receives OnTick calls. Default: true.
    ///
    /// When false, the TickSystem skips this component entirely.
    /// Toggling this is cheaper than unregistering/re-registering.
    void SetTickEnabled(bool enabled) { m_tickEnabled = enabled; }

    /// Returns whether tick dispatch is enabled for this component.
    [[nodiscard]] bool IsTickEnabled() const { return m_tickEnabled; }

    /// Sets the minimum interval between ticks in seconds.
    ///
    /// 0 means every frame. Values > 0 throttle `OnTick` invocations.
    /// The actual delta passed to `OnTick` reflects real elapsed time.
    ///
    /// # Arguments
    /// * `interval` - Minimum seconds between ticks. Must be >= 0.
    void SetTickInterval(float interval) { m_tickInterval = interval; }

    /// Returns the configured tick interval in seconds.
    [[nodiscard]] float GetTickInterval() const { return m_tickInterval; }

    /// Returns accumulated time since the last tick. Used by TickSystem.
    [[nodiscard]] float GetTimeSinceLastTick() const { return m_timeSinceLastTick; }

private:
    friend class TickSystem; // TickSystem manages m_timeSinceLastTick.

    bool m_tickEnabled = true;
    float m_tickInterval = 0.F;
    float m_timeSinceLastTick = 0.F;
};

} // namespace uz
