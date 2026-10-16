#include "owner_command_queue.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace uz {

struct OwnerCommandQueueTestAccess {
    static bool WaitForPending(OwnerCommandQueue& queue) {
        std::unique_lock<std::mutex> lock(queue.m_mutex);
        return queue.m_pendingChanged.wait_for(lock, 5s, [&] {
            return static_cast<bool>(queue.m_pending);
        });
    }
};

} // namespace uz

namespace {

using uz::OwnerCommandQueue;
using uz::OwnerCommandQueueTestAccess;
using uz::OwnerStatus;

// One-shot events with bounded failure paths, never scheduler delays.
class Signal {
public:
    void Open() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_open = true;
        m_cv.notify_all();
    }

    bool Wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, 5s, [&] { return m_open; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_open = false;
};

TEST(OwnerCommandQueue, NoOwnerAttached_RunsInlineOnCaller) {
    OwnerCommandQueue queue;
    const auto caller = std::this_thread::get_id();
    std::thread::id executedOn;

    EXPECT_FALSE(queue.IsOwnerAttached());
    EXPECT_EQ(queue.Submit([&] { executedOn = std::this_thread::get_id(); }, 0ms, 0ms),
              OwnerStatus::Ok);
    EXPECT_EQ(executedOn, caller);
}

TEST(OwnerCommandQueue, OwnerAttached_RunsOnOwnerThread_ReturnsOk) {
    OwnerCommandQueue queue;
    queue.AttachOwner(); // The launching thread must not become the bound owner.
    EXPECT_TRUE(queue.IsOwnerAttached());
    std::thread::id executedOn;
    std::thread owner([&] {
        queue.BindOwnerThread();
        EXPECT_TRUE(OwnerCommandQueueTestAccess::WaitForPending(queue));
        queue.DrainOne();
        queue.FailPending();
        queue.DetachOwner();
    });
    const auto ownerId = owner.get_id();

    EXPECT_EQ(queue.Submit([&] { executedOn = std::this_thread::get_id(); }, 5s, 5s),
              OwnerStatus::Ok);
    owner.join();
    EXPECT_EQ(executedOn, ownerId);
    EXPECT_NE(executedOn, std::this_thread::get_id());
}

TEST(OwnerCommandQueue, PickupTimeout_CommandNeverExecutes) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    bool executed = false;
    const auto before = std::chrono::steady_clock::now();
    EXPECT_EQ(queue.Submit([&] { executed = true; }, 1ms, 5s), OwnerStatus::Timeout);
    EXPECT_GE(std::chrono::steady_clock::now() - before, 1ms);

    // Draining is deliberately enabled only after cancellation has returned.
    std::thread owner([&] {
        queue.BindOwnerThread();
        queue.DrainOne();
        queue.FailPending();
        queue.DetachOwner();
    });
    owner.join();
    EXPECT_FALSE(executed);
}

TEST(OwnerCommandQueue, CompletionTimeout_OwnerFinishesSafely) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    Signal entered;
    Signal finish;
    auto result = std::make_shared<int>(0);
    std::weak_ptr<int> lifetime = result;
    OwnerStatus status = OwnerStatus::Busy;
    std::thread owner([&] {
        queue.BindOwnerThread();
        EXPECT_TRUE(OwnerCommandQueueTestAccess::WaitForPending(queue));
        queue.DrainOne();
        queue.FailPending();
        queue.DetachOwner();
    });
    std::thread submitter([&, result] {
        status = queue.Submit([&, result] {
            entered.Open();
            EXPECT_TRUE(finish.Wait());
            *result = 42;
        }, 5s, 0ms);
    });

    EXPECT_TRUE(entered.Wait());
    submitter.join();
    EXPECT_EQ(status, OwnerStatus::Timeout);
    result.reset();
    EXPECT_FALSE(lifetime.expired()); // The executing command owns the result.
    finish.Open();
    owner.join();
    EXPECT_EQ(status, OwnerStatus::Timeout);
    EXPECT_TRUE(lifetime.expired());
}

TEST(OwnerCommandQueue, FailPending_ReturnsStopped) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    bool executed = false;
    std::thread owner([&] {
        queue.BindOwnerThread();
        EXPECT_TRUE(OwnerCommandQueueTestAccess::WaitForPending(queue));
        queue.FailPending();
        queue.DrainOne();
        queue.DetachOwner();
    });

    EXPECT_EQ(queue.Submit([&] { executed = true; }, 5s, 5s), OwnerStatus::Stopped);
    owner.join();
    EXPECT_FALSE(executed);
}

TEST(OwnerCommandQueue, SecondSubmitWhilePending_IsBusy) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    OwnerStatus first = OwnerStatus::Busy;
    int executions = 0;
    std::thread submitter([&] {
        first = queue.Submit([&] { ++executions; }, 5s, 5s);
    });

    EXPECT_TRUE(OwnerCommandQueueTestAccess::WaitForPending(queue));
    EXPECT_EQ(queue.Submit([&] { executions += 100; }, 0ms, 0ms), OwnerStatus::Busy);
    std::thread owner([&] {
        queue.BindOwnerThread();
        queue.DrainOne();
        queue.FailPending();
        queue.DetachOwner();
    });
    submitter.join();
    owner.join();
    EXPECT_EQ(first, OwnerStatus::Ok);
    EXPECT_EQ(executions, 1);
}

TEST(OwnerCommandQueue, SecondSubmitWhileExecuting_IsBusyUntilCompletion) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    Signal entered;
    Signal finish;
    std::thread owner([&] {
        queue.BindOwnerThread();
        EXPECT_TRUE(OwnerCommandQueueTestAccess::WaitForPending(queue));
        queue.DrainOne();
        queue.FailPending();
        queue.DetachOwner();
    });
    OwnerStatus first = OwnerStatus::Busy;
    std::thread submitter([&] {
        first = queue.Submit([&] {
            entered.Open();
            EXPECT_TRUE(finish.Wait());
        }, 5s, 0ms);
    });

    EXPECT_TRUE(entered.Wait());
    submitter.join(); // Even an abandoned executing command occupies the slot.
    EXPECT_EQ(first, OwnerStatus::Timeout);
    bool secondExecuted = false;
    EXPECT_EQ(queue.Submit([&] { secondExecuted = true; }, 0ms, 0ms), OwnerStatus::Busy);
    finish.Open();
    owner.join();
    EXPECT_FALSE(secondExecuted);
}

TEST(OwnerCommandQueue, DetachAfterStop_RunsInlineAgain) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    std::thread owner([&] {
        queue.BindOwnerThread();
        queue.FailPending();
        queue.DetachOwner();
    });
    owner.join();

    EXPECT_FALSE(queue.IsOwnerAttached());
    std::thread::id executedOn;
    EXPECT_EQ(queue.Submit([&] { executedOn = std::this_thread::get_id(); }, 0ms, 0ms),
              OwnerStatus::Ok);
    EXPECT_EQ(executedOn, std::this_thread::get_id());
}

TEST(OwnerCommandQueue, SequentialCommands_PreserveOrderAndPublishWritesBeforeReturn) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    std::vector<int> order;
    int published = 0;
    std::thread owner([&] {
        queue.BindOwnerThread();
        for (int i = 0; i < 2; ++i) {
            EXPECT_TRUE(OwnerCommandQueueTestAccess::WaitForPending(queue));
            queue.DrainOne();
        }
        queue.FailPending();
        queue.DetachOwner();
    });

    EXPECT_EQ(queue.Submit([&] {
        order.push_back(1);
        published = 10;
    }, 5s, 5s), OwnerStatus::Ok);
    EXPECT_EQ(published, 10);
    EXPECT_EQ(queue.Submit([&] {
        order.push_back(2);
        published = 20;
    }, 5s, 5s), OwnerStatus::Ok);
    EXPECT_EQ(published, 20);
    owner.join();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(OwnerCommandQueue, DrainOne_WithoutExplicitBinding_BindsToOwnerNotLauncher) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    std::thread::id executedOn;
    std::thread owner([&] {
        EXPECT_TRUE(OwnerCommandQueueTestAccess::WaitForPending(queue));
        queue.DrainOne();
        queue.FailPending();
        queue.DetachOwner();
    });
    const auto ownerId = owner.get_id();
    EXPECT_EQ(queue.Submit([&] { executedOn = std::this_thread::get_id(); }, 5s, 5s),
              OwnerStatus::Ok);
    owner.join();
    EXPECT_EQ(executedOn, ownerId);
}

#if GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
TEST(OwnerCommandQueue, DrainOne_FromNonOwnerThread_RejectsAffinity) {
    OwnerCommandQueue queue;
    queue.AttachOwner();
    queue.BindOwnerThread();
    EXPECT_DEATH({
        std::thread wrongOwner([&] { queue.DrainOne(); });
        wrongOwner.join();
    }, "m_ownerThread");
    queue.FailPending();
    queue.DetachOwner();
}
#endif

TEST(OwnerCommandQueue, Stop_JoinsOwnerBeforeDestroyingReferencedState) {
    struct State {
        explicit State(std::vector<int>& events) : events(events) {}
        ~State() { events.push_back(3); }
        std::vector<int>& events;
    };
    struct Harness {
        std::vector<int> events;
        std::unique_ptr<State> state = std::make_unique<State>(events);
        OwnerCommandQueue queue;
        Signal inFrame;
        Signal leaveFrame;
        Signal stopRequested;
        std::thread owner;

        void Start() {
            queue.AttachOwner();
            owner = std::thread([this] {
                queue.BindOwnerThread();
                inFrame.Open();
                EXPECT_TRUE(leaveFrame.Wait());
                state->events.push_back(1); // Frame still uses the live state.
                queue.FailPending();
                queue.DetachOwner();
                events.push_back(2);
            });
        }

        void StopAndDestroy() {
            stopRequested.Open();
            owner.join();
            state.reset();
        }
    } runtime;

    runtime.Start();
    EXPECT_TRUE(runtime.inFrame.Wait());
    std::thread stopper([&] { runtime.StopAndDestroy(); });
    EXPECT_TRUE(runtime.stopRequested.Wait());
    runtime.leaveFrame.Open();
    stopper.join();
    EXPECT_EQ(runtime.events, (std::vector<int>{1, 2, 3}));
    EXPECT_FALSE(runtime.queue.IsOwnerAttached());
    EXPECT_EQ(runtime.state, nullptr);
}

} // namespace
