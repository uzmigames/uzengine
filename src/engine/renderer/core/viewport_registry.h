#pragma once

// viewport_registry.h -- Manages N independent render viewports with lifecycle
// state machine.
//
// State machine:
//   INACTIVE  -> ACTIVE    (set_active(id, true))
//   ACTIVE   <-> SUSPENDED (set_active toggle)
//   any       -> DESTROYED (destroy)
//
// Hard caps:
//   Max viewports:     16 (kMaxViewports)
//   Max ACTIVE:        4  (kMaxActiveViewports)
//   VRAM budget:       512 MB across all ACTIVE viewports
//
// Thread safety: all public functions are safe to call from the IPC thread.
//   Internally guarded by a uz::platform::Mutex.

#include <cstddef>
#include <cstdint>

namespace uz {

// --- IDs & constants -------------------------------------------------------

using ViewportId = uint32_t;
inline constexpr ViewportId kViewportIdInvalid  = 0;
inline constexpr uint32_t   kMaxViewports       = 16;
inline constexpr uint32_t   kMaxActiveViewports = 4;
inline constexpr uint64_t   kVramBudgetBytes    = 512ULL * 1024 * 1024;  // 512 MB

// --- Enums -----------------------------------------------------------------

enum class ViewportState : uint8_t {
    Inactive  = 0,
    Active    = 1,
    Suspended = 2,
    Destroyed = 3,
};

enum class ViewportType : uint8_t {
    Scene      = 0,
    VfxPreview = 1,
    Material   = 2,
    Mesh       = 3,
    Texture    = 4,
    Prefab     = 5,
    Animation  = 6,
};

enum class ViewportError : uint8_t {
    None           = 0,
    InvalidId      = 1,
    CapReached     = 2,   // 4 viewports already ACTIVE
    VramBudget     = 3,   // Activation would exceed VRAM budget
    AlreadyInState = 4,
};

// --- Descriptors -----------------------------------------------------------

struct ViewportDesc {
    ViewportType type         = ViewportType::Scene;
    uint32_t     width        = 1280;
    uint32_t     height       = 720;
    uint32_t     target_fps   = 60;
    uint32_t     jpeg_quality = 85;
    uint64_t     vram_bytes   = 32ULL * 1024 * 1024;  // Estimated 32 MB default
    char         debug_name[64] = {};
};

// --- Status ----------------------------------------------------------------

struct ViewportStatus {
    ViewportId    id          = kViewportIdInvalid;
    ViewportState state       = ViewportState::Inactive;
    ViewportType  type        = ViewportType::Scene;
    uint32_t      width       = 0;
    uint32_t      height      = 0;
    uint64_t      frame_count = 0;
    uint64_t      vram_bytes  = 0;
    float         fps         = 0.0f;
};

// --- Callbacks -------------------------------------------------------------

// Called when a viewport is auto-suspended due to VRAM budget enforcement.
// Can be called from any thread (under the registry lock).
using ViewportSuspendCallback = void(*)(ViewportId id, void* user_data);

// --- Registry --------------------------------------------------------------

class ViewportRegistry {
public:
    ViewportRegistry();
    ~ViewportRegistry();

    ViewportRegistry(const ViewportRegistry&) = delete;
    ViewportRegistry& operator=(const ViewportRegistry&) = delete;

    // Lifecycle ---------------------------------------------------------------

    // Create a new viewport. Returns kViewportIdInvalid if registry is full.
    [[nodiscard]] ViewportId create(const ViewportDesc& desc);

    // Destroy a viewport. Releases the slot. Returns false if id is invalid.
    bool destroy(ViewportId id);

    // Activate or suspend a viewport.
    // On activate: enforces ACTIVE cap and VRAM budget (auto-suspends oldest
    //              ACTIVE viewport if budget would be exceeded).
    // Returns error code.
    [[nodiscard]] ViewportError set_active(ViewportId id, bool active);

    // Resize a viewport. Returns false if id is invalid or destroyed.
    bool resize(ViewportId id, uint32_t width, uint32_t height);

    // Tick called once per engine frame. Increments frame_count for ACTIVE
    // viewports. Also checks heartbeat watchdog.
    void tick();

    // Query -------------------------------------------------------------------

    [[nodiscard]] bool          get_status(ViewportId id, ViewportStatus& out) const;
    [[nodiscard]] ViewportState state(ViewportId id) const;
    [[nodiscard]] uint32_t      active_count() const;
    [[nodiscard]] uint32_t      total_count() const;          // Excludes Destroyed
    [[nodiscard]] uint64_t      total_vram_active() const;    // Sum for ACTIVE only

    // Suspend all ACTIVE viewports (heartbeat watchdog / manual call).
    void suspend_all();

    // Heartbeat watchdog ------------------------------------------------------

    // Record an IPC heartbeat. Called whenever an IPC message arrives.
    void record_ipc_heartbeat();

    // Returns seconds since last heartbeat.
    [[nodiscard]] double seconds_since_heartbeat() const;

    // Callbacks ---------------------------------------------------------------

    void set_suspend_callback(ViewportSuspendCallback cb, void* user_data);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace uz
