#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace uz {

enum class OwnerStatus : uint8_t { Ok, Timeout, Stopped, Busy };

/// Single-slot rendezvous between a submitter and the frame-driving owner.
/// Attach before thread creation; fail pending work and detach on owner exit;
/// join the owner before destroying this queue or any state commands reference.
/// Commands must not throw or synchronously submit another command.
class OwnerCommandQueue {
public:
    OwnerStatus Submit(std::function<void()> fn,
                       std::chrono::milliseconds pickupBudget,
                       std::chrono::milliseconds completionBudget) {
        std::shared_ptr<Command> cmd;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_ownerAttached.load(std::memory_order_acquire)) {
                lock.unlock();
                fn();
                return OwnerStatus::Ok;
            }
            if (m_pending) {
                return OwnerStatus::Busy;
            }
            cmd = std::make_shared<Command>();
            cmd->fn = std::move(fn);
            m_pending = cmd;
        }
        m_pendingChanged.notify_all();

        std::unique_lock<std::mutex> lock(cmd->m);
        if (!cmd->cv.wait_for(lock, pickupBudget, [&] { return cmd->pickedUp || cmd->done; })) {
            // All slot transitions lock the queue before the command. Recheck
            // pickup under both locks so cancellation cannot race execution.
            lock.unlock();
            std::lock_guard<std::mutex> queueLock(m_mutex);
            lock.lock();
            if (!cmd->pickedUp && !cmd->done) {
                if (m_pending == cmd) {
                    m_pending.reset();
                }
                return OwnerStatus::Timeout;
            }
        }
        if (!cmd->done &&
            !cmd->cv.wait_until(lock, cmd->pickedUpAt + completionBudget, [&] { return cmd->done; })) {
            cmd->abandoned = true;
            return OwnerStatus::Timeout;
        }
        return cmd->status;
    }

    /// Owner only. An executing command retains the slot until completion.
    void DrainOne() {
        std::shared_ptr<Command> cmd;
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            AssertOwner();
            if (!m_pending) {
                return;
            }
            cmd = m_pending;
            std::lock_guard<std::mutex> lock(cmd->m);
            if (cmd->pickedUp) {
                return;
            }
            cmd->pickedUp = true;
            cmd->pickedUpAt = std::chrono::steady_clock::now();
        }
        cmd->cv.notify_all();
        cmd->fn();
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            std::lock_guard<std::mutex> lock(cmd->m);
            cmd->done = true;
            m_pending.reset();
            if (!cmd->abandoned) {
                cmd->cv.notify_all();
            }
        }
    }

    /// Owner exit: wake a submitter whose command was never picked up.
    void FailPending() {
        std::lock_guard<std::mutex> queueLock(m_mutex);
        AssertOwner();
        if (m_pending) {
            std::lock_guard<std::mutex> lock(m_pending->m);
            if (!m_pending->pickedUp) {
                m_pending->status = OwnerStatus::Stopped;
                m_pending->done = true;
                m_pending->cv.notify_all();
                m_pending.reset();
            }
        }
    }

    /// Called by the launching thread, before constructing the owner thread.
    void AttachOwner() {
        std::lock_guard<std::mutex> lock(m_mutex);
        assert(!m_ownerAttached.load(std::memory_order_relaxed) && !m_pending);
        m_ownerThread = {};
        m_ownerAttached.store(true, std::memory_order_release);
    }

    /// Called at owner entry. DrainOne also binds lazily if this is omitted.
    void BindOwnerThread() {
        std::lock_guard<std::mutex> lock(m_mutex);
        AssertOwner();
    }

    void DetachOwner() {
        std::lock_guard<std::mutex> lock(m_mutex);
        assert(!m_pending);
        m_ownerThread = {};
        m_ownerAttached.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool IsOwnerAttached() const noexcept {
        return m_ownerAttached.load(std::memory_order_acquire);
    }

private:
    // The peer waits on publication, not on scheduling or repeated probes.
    friend struct OwnerCommandQueueTestAccess;

    struct Command {
        std::function<void()> fn;
        std::mutex m;
        std::condition_variable cv;
        bool pickedUp = false;
        bool done = false;
        bool abandoned = false;
        OwnerStatus status = OwnerStatus::Ok;
        std::chrono::steady_clock::time_point pickedUpAt;
    };

    // Requires m_mutex. AttachOwner cannot bind to the launching thread.
    void AssertOwner() {
        assert(m_ownerAttached.load(std::memory_order_relaxed));
        if (m_ownerThread == std::thread::id{}) {
            m_ownerThread = std::this_thread::get_id();
        }
        assert(m_ownerThread == std::this_thread::get_id());
    }

    std::mutex m_mutex;
    std::condition_variable m_pendingChanged;
    std::shared_ptr<Command> m_pending;
    std::atomic<bool> m_ownerAttached{false};
    std::thread::id m_ownerThread;
};

} // namespace uz
