/// Counter Implementation — decrement-to-zero with condition variable notification.
///
/// Used by Fence to track completion of batched jobs. When the count reaches zero,
/// all waiting threads are woken via condition variable broadcast.

#include "counter.h"

namespace uz {

void Counter::Reset(uint32_t count) {
    count_.store(count, std::memory_order_release);
}

void Counter::Decrement() {
    uint32_t prev = count_.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        // Counter just hit zero — wake all waiters
        std::lock_guard<std::mutex> lock(mutex_);
        cv_.notify_all();
    }
}

void Counter::Wait() {
    // Fast path: already complete
    if (count_.load(std::memory_order_acquire) == 0) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return count_.load(std::memory_order_acquire) == 0; });
}

bool Counter::IsComplete() const {
    return count_.load(std::memory_order_acquire) == 0;
}

} // namespace uz
