/// Fence Implementation — batches jobs and waits for all to complete.
///
/// Collects zero or more jobs via AddWork(), then Wait() enqueues them all with
/// a shared Counter, waits for the counter to reach zero, and returns.
/// Fences allow fine-grained sync granularity between job submission and awaiting.

#include "fence.h"
#include "job_system.h"

namespace uz {

Fence::Fence(JobSystem* system)
    : system_(system) {}

void Fence::AddWork(Job job) {
    pending_.push_back(job);
}

void Fence::Wait() {
    if (pending_.empty()) {
        return;
    }

    const auto count = static_cast<uint32_t>(pending_.size());
    counter_.Reset(count);

    // Assign counter and enqueue all jobs
    for (auto& job : pending_) {
        job.counter = &counter_;
        system_->Enqueue(job);
    }

    pending_.clear();
    counter_.Wait();
}

bool Fence::IsComplete() const {
    return pending_.empty() && counter_.IsComplete();
}

} // namespace uz
