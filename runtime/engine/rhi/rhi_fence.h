#pragma once

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IRHIFence — CPU/GPU synchronization primitive
// ─────────────────────────────────────────────────────────────────────────────

/// GPU fence for synchronizing CPU and GPU work.
///
/// Fences are monotonically increasing: each `Signal(value)` must use a value
/// greater than any previously signaled value. The fence starts at value 0.
///
/// # Thread Safety
/// All methods are thread-safe. Multiple CPU threads can poll `IsComplete()`
/// concurrently. Only one thread should call `WaitCPU()` at a time per fence.
///
/// # Lifetime
/// Created via `IRHIDevice::CreateFence()`, destroyed via `IRHIDevice::DestroyFence()`.
/// Must be destroyed before the device.
///
/// # Examples
/// ```cpp
/// auto* fence = device->CreateFence();
/// uint64_t fenceValue = 1;
///
/// cmdList->End();
/// cmdList->Execute();
/// fence->Signal(fenceValue);
/// fence->WaitCPU(fenceValue);
/// // GPU work is now complete
///
/// device->DestroyFence(fence);
/// ```
class IRHIFence {
public:
    virtual ~IRHIFence() = default;

    /// Signals the fence to the given value from the GPU timeline.
    ///
    /// # Arguments
    /// * `value` — Monotonically increasing fence value. Must be greater
    ///   than any previously signaled value.
    virtual void Signal(uint64_t value) = 0;

    /// Blocks the calling CPU thread until the fence reaches the given value.
    ///
    /// Returns immediately if the fence has already reached or passed `value`.
    ///
    /// # Arguments
    /// * `value` — The fence value to wait for.
    virtual void WaitCPU(uint64_t value) = 0;

    /// Returns true if the fence has reached or passed the given value.
    ///
    /// # Arguments
    /// * `value` — The fence value to check against.
    ///
    /// # Returns
    /// `true` if `GetCompletedValue() >= value`, `false` otherwise.
    virtual bool IsComplete(uint64_t value) const = 0;

    /// Returns the highest value the fence has completed on the GPU.
    ///
    /// # Returns
    /// The most recently completed fence value, or 0 if no signal has occurred.
    virtual uint64_t GetCompletedValue() const = 0;
};

} // namespace uz
