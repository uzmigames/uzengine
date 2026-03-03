// viewport_registry.cpp -- ViewportRegistry implementation.
//
// Slot-array design: ViewportInfo slots_[kMaxViewports] with monotonically
// increasing IDs. Destroyed slots are recycled on the next create().
//
// Thread safety: all public methods lock impl_->mutex before touching state.
// The mutex is a uz::platform::Mutex (PIMPL hides the platform header).

#include "engine/renderer/viewport_registry.h"
#include "engine/core/platform.h"

#include <algorithm>
#include <cstring>

namespace uz {

// ---------------------------------------------------------------------------
// Internal slot structure
// ---------------------------------------------------------------------------

struct ViewportInfo {
    ViewportId    id          = kViewportIdInvalid;
    ViewportState state       = ViewportState::Destroyed;
    ViewportDesc  desc        = {};
    uint64_t      frame_count = 0;
    float         fps         = 0.0f;
};

// ---------------------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------------------

struct ViewportRegistry::Impl {
    platform::Mutex* mutex         = nullptr;
    ViewportInfo     slots[kMaxViewports] = {};
    uint32_t         next_id       = 1;   // Monotonically increasing

    // Heartbeat
    double           last_heartbeat_time = 0.0;

    // Suspend callback
    ViewportSuspendCallback suspend_cb   = nullptr;
    void*                   suspend_data = nullptr;
};

// ---------------------------------------------------------------------------
// RAII lock guard for uz::platform::Mutex
// ---------------------------------------------------------------------------

namespace {

class MutexGuard {
public:
    explicit MutexGuard(platform::Mutex* m) noexcept : m_(m) {
        platform::mutex_lock(m_);
    }
    ~MutexGuard() {
        platform::mutex_unlock(m_);
    }
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

private:
    platform::Mutex* m_;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

ViewportRegistry::ViewportRegistry()
    : impl_(new Impl)
{
    impl_->mutex = platform::mutex_create();
    impl_->last_heartbeat_time = platform::get_time_seconds();
}

ViewportRegistry::~ViewportRegistry() {
    if (impl_) {
        platform::mutex_destroy(impl_->mutex);
        delete impl_;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ViewportId ViewportRegistry::create(const ViewportDesc& desc) {
    MutexGuard lock(impl_->mutex);

    // Find a free slot (state == Destroyed).
    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].state == ViewportState::Destroyed) {
            const ViewportId id = impl_->next_id++;
            impl_->slots[i].id          = id;
            impl_->slots[i].state       = ViewportState::Inactive;
            impl_->slots[i].desc        = desc;
            impl_->slots[i].frame_count = 0;
            impl_->slots[i].fps         = 0.0f;
            return id;
        }
    }

    return kViewportIdInvalid;
}

bool ViewportRegistry::destroy(ViewportId id) {
    MutexGuard lock(impl_->mutex);

    if (id == kViewportIdInvalid) {
        return false;
    }

    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].id == id && impl_->slots[i].state != ViewportState::Destroyed) {
            impl_->slots[i].state = ViewportState::Destroyed;
            impl_->slots[i].id    = kViewportIdInvalid;
            return true;
        }
    }

    return false;
}

ViewportError ViewportRegistry::set_active(ViewportId id, bool active) {
    MutexGuard lock(impl_->mutex);

    if (id == kViewportIdInvalid) {
        return ViewportError::InvalidId;
    }

    // Find the slot.
    ViewportInfo* slot = nullptr;
    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].id == id && impl_->slots[i].state != ViewportState::Destroyed) {
            slot = &impl_->slots[i];
            break;
        }
    }

    if (!slot) {
        return ViewportError::InvalidId;
    }

    if (active) {
        // Already active?
        if (slot->state == ViewportState::Active) {
            return ViewportError::AlreadyInState;
        }

        // Count current ACTIVE viewports.
        uint32_t current_active = 0;
        for (uint32_t i = 0; i < kMaxViewports; ++i) {
            if (impl_->slots[i].state == ViewportState::Active) {
                ++current_active;
            }
        }

        if (current_active >= kMaxActiveViewports) {
            return ViewportError::CapReached;
        }

        // VRAM budget check. Sum current ACTIVE VRAM.
        uint64_t active_vram = 0;
        for (uint32_t i = 0; i < kMaxViewports; ++i) {
            if (impl_->slots[i].state == ViewportState::Active) {
                active_vram += impl_->slots[i].desc.vram_bytes;
            }
        }

        // If adding this viewport would exceed the budget, auto-suspend oldest.
        while (active_vram + slot->desc.vram_bytes > kVramBudgetBytes) {
            // Find the ACTIVE viewport with the smallest frame_count (oldest).
            ViewportInfo* oldest = nullptr;
            for (uint32_t i = 0; i < kMaxViewports; ++i) {
                if (impl_->slots[i].state == ViewportState::Active) {
                    if (!oldest || impl_->slots[i].frame_count < oldest->frame_count) {
                        oldest = &impl_->slots[i];
                    }
                }
            }

            if (!oldest) {
                // No ACTIVE viewports to suspend -- budget truly exceeded.
                return ViewportError::VramBudget;
            }

            oldest->state = ViewportState::Suspended;
            active_vram -= oldest->desc.vram_bytes;

            if (impl_->suspend_cb) {
                impl_->suspend_cb(oldest->id, impl_->suspend_data);
            }
        }

        slot->state = ViewportState::Active;
        return ViewportError::None;
    }

    // Suspending.
    if (slot->state == ViewportState::Suspended || slot->state == ViewportState::Inactive) {
        return ViewportError::AlreadyInState;
    }

    // ACTIVE -> SUSPENDED
    slot->state = ViewportState::Suspended;
    return ViewportError::None;
}

bool ViewportRegistry::resize(ViewportId id, uint32_t width, uint32_t height) {
    MutexGuard lock(impl_->mutex);

    if (id == kViewportIdInvalid) {
        return false;
    }

    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].id == id && impl_->slots[i].state != ViewportState::Destroyed) {
            impl_->slots[i].desc.width  = width;
            impl_->slots[i].desc.height = height;
            return true;
        }
    }

    return false;
}

void ViewportRegistry::tick() {
    MutexGuard lock(impl_->mutex);

    // Heartbeat watchdog: if > 30s since last heartbeat, suspend all.
    const double now = platform::get_time_seconds();
    const double elapsed = now - impl_->last_heartbeat_time;

    if (elapsed > 30.0) {
        bool has_active = false;
        for (uint32_t i = 0; i < kMaxViewports; ++i) {
            if (impl_->slots[i].state == ViewportState::Active) {
                has_active = true;
                break;
            }
        }

        if (has_active) {
            for (uint32_t i = 0; i < kMaxViewports; ++i) {
                if (impl_->slots[i].state == ViewportState::Active) {
                    impl_->slots[i].state = ViewportState::Suspended;
                    if (impl_->suspend_cb) {
                        impl_->suspend_cb(impl_->slots[i].id, impl_->suspend_data);
                    }
                }
            }
        }
    }

    // Increment frame_count for ACTIVE viewports.
    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].state == ViewportState::Active) {
            ++impl_->slots[i].frame_count;
        }
    }
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

bool ViewportRegistry::get_status(ViewportId id, ViewportStatus& out) const {
    MutexGuard lock(impl_->mutex);

    if (id == kViewportIdInvalid) {
        return false;
    }

    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].id == id && impl_->slots[i].state != ViewportState::Destroyed) {
            out.id          = impl_->slots[i].id;
            out.state       = impl_->slots[i].state;
            out.type        = impl_->slots[i].desc.type;
            out.width       = impl_->slots[i].desc.width;
            out.height      = impl_->slots[i].desc.height;
            out.frame_count = impl_->slots[i].frame_count;
            out.vram_bytes  = impl_->slots[i].desc.vram_bytes;
            out.fps         = impl_->slots[i].fps;
            return true;
        }
    }

    return false;
}

ViewportState ViewportRegistry::state(ViewportId id) const {
    MutexGuard lock(impl_->mutex);

    if (id == kViewportIdInvalid) {
        return ViewportState::Destroyed;
    }

    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].id == id && impl_->slots[i].state != ViewportState::Destroyed) {
            return impl_->slots[i].state;
        }
    }

    return ViewportState::Destroyed;
}

uint32_t ViewportRegistry::active_count() const {
    MutexGuard lock(impl_->mutex);

    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].state == ViewportState::Active) {
            ++count;
        }
    }
    return count;
}

uint32_t ViewportRegistry::total_count() const {
    MutexGuard lock(impl_->mutex);

    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].state != ViewportState::Destroyed) {
            ++count;
        }
    }
    return count;
}

uint64_t ViewportRegistry::total_vram_active() const {
    MutexGuard lock(impl_->mutex);

    uint64_t sum = 0;
    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].state == ViewportState::Active) {
            sum += impl_->slots[i].desc.vram_bytes;
        }
    }
    return sum;
}

void ViewportRegistry::suspend_all() {
    MutexGuard lock(impl_->mutex);

    for (uint32_t i = 0; i < kMaxViewports; ++i) {
        if (impl_->slots[i].state == ViewportState::Active) {
            impl_->slots[i].state = ViewportState::Suspended;
            if (impl_->suspend_cb) {
                impl_->suspend_cb(impl_->slots[i].id, impl_->suspend_data);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Heartbeat watchdog
// ---------------------------------------------------------------------------

void ViewportRegistry::record_ipc_heartbeat() {
    MutexGuard lock(impl_->mutex);
    impl_->last_heartbeat_time = platform::get_time_seconds();
}

double ViewportRegistry::seconds_since_heartbeat() const {
    MutexGuard lock(impl_->mutex);
    return platform::get_time_seconds() - impl_->last_heartbeat_time;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void ViewportRegistry::set_suspend_callback(ViewportSuspendCallback cb, void* user_data) {
    MutexGuard lock(impl_->mutex);
    impl_->suspend_cb   = cb;
    impl_->suspend_data = user_data;
}

} // namespace uz
