#pragma once

/// Metal backend implementation of IRHIFence using MTLSharedEvent.
///
/// MTLSharedEvent provides a monotonically increasing 64-bit counter that the
/// GPU signals and the CPU can wait on or poll — a direct analogue to D3D12
/// fences and Vulkan timeline semaphores.
///
/// # Signal Encoding
/// Signal() creates a lightweight command buffer on the graphics queue and
/// encodes `encodeSignalEvent` into it. The event is signaled only after all
/// previously enqueued GPU work on the queue finishes — matching DX12's
/// `ID3D12CommandQueue::Signal()` semantics.
///
/// # Thread Safety
/// All methods are thread-safe. MTLSharedEvent operations are atomic on the GPU
/// timeline. WaitCPU uses a listener + dispatch_semaphore for blocking waits.

#include "../rhi.h"

#include <cstdint>

// Forward declarations for Metal types
namespace MTL {
    class SharedEvent;
    class Device;
} // namespace MTL

namespace uz {

class MetalDevice;

// ─────────────────────────────────────────────────────────────────────────────
// MetalFence
// ─────────────────────────────────────────────────────────────────────────────

/// Metal implementation of IRHIFence using MTLSharedEvent.
///
/// MTLSharedEvent is Metal's equivalent of D3D12 fences and Vulkan timeline
/// semaphores. It provides a monotonically increasing 64-bit counter that
/// can be signaled from the GPU and waited on from the CPU.
///
/// # Lifetime
/// Created via `MetalDevice::CreateFence()`, destroyed via `MetalDevice::DestroyFence()`.
/// Must be destroyed before the MetalDevice.
class MetalFence final : public IRHIFence {
public:
    MetalFence() = default;
    ~MetalFence() override;

    /// Creates the MTLSharedEvent with initial value 0.
    ///
    /// # Arguments
    /// * `device` — Parent Metal device (must outlive this fence).
    ///
    /// # Returns
    /// `true` on success, `false` if event creation fails (unsupported hardware).
    bool Init(MetalDevice* device);

    // -- IRHIFence overrides --

    /// Signals the fence to the given value after all submitted GPU work completes.
    ///
    /// Creates a lightweight command buffer on the graphics queue and encodes
    /// `encodeSignalEvent` into it. The event is signaled only after all
    /// previously enqueued GPU work on the queue finishes — matching DX12's
    /// `ID3D12CommandQueue::Signal()` semantics.
    ///
    /// # Arguments
    /// * `value` — Monotonically increasing fence value.
    void Signal(uint64_t value) override;

    /// Blocks the calling CPU thread until the shared event reaches the given value.
    ///
    /// Uses `MTLSharedEventListener` with a `dispatch_semaphore_t` for blocking.
    /// Returns immediately if the event has already reached or passed `value`.
    /// Times out after 5 seconds to prevent infinite hangs.
    ///
    /// # Arguments
    /// * `value` — The fence value to wait for.
    void WaitCPU(uint64_t value) override;

    /// Returns true if the shared event has reached or passed the given value.
    ///
    /// # Arguments
    /// * `value` — The fence value to check against.
    ///
    /// # Returns
    /// `true` if `signaledValue >= value`, `false` otherwise.
    bool IsComplete(uint64_t value) const override;

    /// Returns the highest value the shared event has been signaled to.
    ///
    /// # Returns
    /// The current signaled value, or 0 if no signal has occurred.
    uint64_t GetCompletedValue() const override;

private:
    MetalDevice*     m_device = nullptr;      ///< Parent device (not owned)
    MTL::SharedEvent* m_event = nullptr;       ///< Metal shared event for timeline synchronization
};

} // namespace uz
