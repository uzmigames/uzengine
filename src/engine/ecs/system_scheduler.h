#pragma once

// SystemScheduler — tick-group-aware ECS system scheduler with dependency ordering.
//
// Systems are registered with a TickGroup and optional before/after constraints.
// build() resolves dependencies per group via Kahn's algorithm (topological sort).
// tick(dt) executes all systems in group order, then topo order within each group.
// shutdown() tears down in reverse execution order.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// TickGroup — ordered execution phases within a single frame.
// ---------------------------------------------------------------------------
enum class TickGroup : uint8_t {
    PreUpdate = 0,
    PhysicsUpdate,
    Update,
    LateUpdate,
    PreRender,
    Count_  // sentinel — not a valid group
};

// ---------------------------------------------------------------------------
// IEcsSystem — pure interface for systems managed by the scheduler.
// ---------------------------------------------------------------------------
class IEcsSystem {
public:
    virtual ~IEcsSystem() = default;

    // Called once when the system is registered.
    virtual void on_register() = 0;

    // Called every frame during the system's tick group.
    virtual void tick(float dt) = 0;

    // Called during shutdown (reverse execution order).
    virtual void on_shutdown() = 0;

    // Human-readable name for debug display and dependency resolution.
    [[nodiscard]] virtual const char* name() const = 0;
};

// ---------------------------------------------------------------------------
// SystemDesc — registration descriptor for a single system.
// ---------------------------------------------------------------------------
struct SystemDesc {
    IEcsSystem*              system = nullptr;
    TickGroup                group  = TickGroup::Update;
    std::vector<std::string> before;  // names of systems this one must precede
    std::vector<std::string> after;   // names of systems this one must follow
};

// ---------------------------------------------------------------------------
// SystemScheduler — builds and executes a dependency-sorted system pipeline.
// ---------------------------------------------------------------------------
class SystemScheduler {
public:
    SystemScheduler() = default;
    ~SystemScheduler() = default;

    // Non-copyable, non-movable (owns raw pointers provided externally).
    SystemScheduler(const SystemScheduler&)            = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;
    SystemScheduler(SystemScheduler&&)                 = delete;
    SystemScheduler& operator=(SystemScheduler&&)      = delete;

    // Register a system. Calls system->on_register() immediately.
    // Must be called before build().
    void add_system(SystemDesc desc);

    // Build execution order via topological sort within each tick group.
    // Returns false if a cycle is detected (execution order left empty).
    [[nodiscard]] bool build();

    // Execute all systems in order. Requires a prior successful build().
    void tick(float dt);

    // Shutdown all systems in reverse execution order.
    void shutdown();

    // Debug: names in execution order (valid after build()).
    [[nodiscard]] std::vector<const char*> get_execution_order() const;

    // Number of registered systems.
    [[nodiscard]] size_t system_count() const;

private:
    struct Entry {
        IEcsSystem* system;
        TickGroup   group;
        std::vector<std::string> before;
        std::vector<std::string> after;
    };

    std::vector<Entry>       entries_;
    std::vector<IEcsSystem*> execution_order_;
    bool                     built_ = false;
};

} // namespace uz
