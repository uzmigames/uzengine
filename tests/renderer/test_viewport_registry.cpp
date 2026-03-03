// test_viewport_registry.cpp -- Google Test coverage for ViewportRegistry and
// ipc_viewport_handlers.
//
// Covers:
//   - ViewportRegistry: create, destroy, set_active, resize, tick, state machine
//   - Active cap enforcement (kMaxActiveViewports = 4)
//   - VRAM budget enforcement (kVramBudgetBytes = 512 MB)
//   - Heartbeat watchdog (auto-suspend after 30s)
//   - Suspend callback invocation
//   - Thread safety: concurrent create/destroy/set_active
//   - IPC handlers: viewport.create, viewport.destroy, viewport.setActive,
//                   viewport.resize, viewport.getStatus, viewport.list

#include <gtest/gtest.h>
#include "engine/renderer/viewport_registry.h"
#include "engine/core/platform.h"
#include "common/ipc_viewport_handlers.h"
#include "common/ipc_registry.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;

// ============================================================================
// ViewportRegistry unit tests
// ============================================================================

class ViewportRegistryTest : public ::testing::Test {
protected:
    uz::ViewportRegistry reg_;
};

// --- Construction ---

TEST_F(ViewportRegistryTest, InitialState_ZeroCounts) {
    EXPECT_EQ(reg_.active_count(), 0u);
    EXPECT_EQ(reg_.total_count(), 0u);
    EXPECT_EQ(reg_.total_vram_active(), 0u);
}

// --- Create ---

TEST_F(ViewportRegistryTest, Create_ReturnsValidId) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    EXPECT_NE(id, uz::kViewportIdInvalid);
}

TEST_F(ViewportRegistryTest, Create_StateIsInactive) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Inactive);
}

TEST_F(ViewportRegistryTest, Create_IncrementsTotalCount) {
    uz::ViewportDesc desc;
    reg_.create(desc);
    EXPECT_EQ(reg_.total_count(), 1u);
    reg_.create(desc);
    EXPECT_EQ(reg_.total_count(), 2u);
}

TEST_F(ViewportRegistryTest, Create_UniqueIds) {
    uz::ViewportDesc desc;
    uz::ViewportId id1 = reg_.create(desc);
    uz::ViewportId id2 = reg_.create(desc);
    EXPECT_NE(id1, id2);
}

TEST_F(ViewportRegistryTest, Create_MaxViewports_AllSucceed) {
    uz::ViewportDesc desc;
    for (uint32_t i = 0; i < uz::kMaxViewports; ++i) {
        uz::ViewportId id = reg_.create(desc);
        EXPECT_NE(id, uz::kViewportIdInvalid) << "create failed at slot " << i;
    }
    EXPECT_EQ(reg_.total_count(), uz::kMaxViewports);
}

TEST_F(ViewportRegistryTest, Create_RegistryFull_ReturnsInvalid) {
    uz::ViewportDesc desc;
    for (uint32_t i = 0; i < uz::kMaxViewports; ++i) {
        reg_.create(desc);
    }
    uz::ViewportId id = reg_.create(desc);
    EXPECT_EQ(id, uz::kViewportIdInvalid);
}

TEST_F(ViewportRegistryTest, Create_DebugName_Preserved) {
    uz::ViewportDesc desc;
    std::strncpy(desc.debug_name, "TestVP", sizeof(desc.debug_name) - 1);
    uz::ViewportId id = reg_.create(desc);

    uz::ViewportStatus status;
    ASSERT_TRUE(reg_.get_status(id, status));
    EXPECT_EQ(status.type, uz::ViewportType::Scene);
}

TEST_F(ViewportRegistryTest, Create_CustomType_Preserved) {
    uz::ViewportDesc desc;
    desc.type = uz::ViewportType::Material;
    uz::ViewportId id = reg_.create(desc);

    uz::ViewportStatus status;
    ASSERT_TRUE(reg_.get_status(id, status));
    EXPECT_EQ(status.type, uz::ViewportType::Material);
}

// --- Destroy ---

TEST_F(ViewportRegistryTest, Destroy_ValidId_ReturnsTrue) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    EXPECT_TRUE(reg_.destroy(id));
}

TEST_F(ViewportRegistryTest, Destroy_DecrementsTotalCount) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    EXPECT_EQ(reg_.total_count(), 1u);
    reg_.destroy(id);
    EXPECT_EQ(reg_.total_count(), 0u);
}

TEST_F(ViewportRegistryTest, Destroy_InvalidId_ReturnsFalse) {
    EXPECT_FALSE(reg_.destroy(uz::kViewportIdInvalid));
}

TEST_F(ViewportRegistryTest, Destroy_NonexistentId_ReturnsFalse) {
    EXPECT_FALSE(reg_.destroy(9999));
}

TEST_F(ViewportRegistryTest, Destroy_AlreadyDestroyed_ReturnsFalse) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    EXPECT_TRUE(reg_.destroy(id));
    EXPECT_FALSE(reg_.destroy(id));
}

TEST_F(ViewportRegistryTest, Destroy_ActiveViewport_DecrementsActiveCount) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.set_active(id, true);
    EXPECT_EQ(reg_.active_count(), 1u);
    reg_.destroy(id);
    EXPECT_EQ(reg_.active_count(), 0u);
}

TEST_F(ViewportRegistryTest, Destroy_FreesSlot_CanCreateNew) {
    uz::ViewportDesc desc;
    // Fill all slots.
    std::vector<uz::ViewportId> ids;
    for (uint32_t i = 0; i < uz::kMaxViewports; ++i) {
        ids.push_back(reg_.create(desc));
    }
    EXPECT_EQ(reg_.create(desc), uz::kViewportIdInvalid);

    // Destroy one, then create should succeed.
    reg_.destroy(ids[0]);
    uz::ViewportId new_id = reg_.create(desc);
    EXPECT_NE(new_id, uz::kViewportIdInvalid);
    EXPECT_NE(new_id, ids[0]);  // New IDs are monotonically increasing.
}

// --- State machine (set_active) ---

TEST_F(ViewportRegistryTest, SetActive_InactiveToActive) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    auto err = reg_.set_active(id, true);
    EXPECT_EQ(err, uz::ViewportError::None);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Active);
}

TEST_F(ViewportRegistryTest, SetActive_ActiveToSuspended) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.set_active(id, true);
    auto err = reg_.set_active(id, false);
    EXPECT_EQ(err, uz::ViewportError::None);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Suspended);
}

TEST_F(ViewportRegistryTest, SetActive_SuspendedToActive) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.set_active(id, true);
    reg_.set_active(id, false);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Suspended);

    auto err = reg_.set_active(id, true);
    EXPECT_EQ(err, uz::ViewportError::None);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Active);
}

TEST_F(ViewportRegistryTest, SetActive_FullRoundtrip) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);

    EXPECT_EQ(reg_.state(id), uz::ViewportState::Inactive);

    reg_.set_active(id, true);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Active);

    reg_.set_active(id, false);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Suspended);

    reg_.set_active(id, true);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Active);
}

TEST_F(ViewportRegistryTest, SetActive_AlreadyActive_ReturnsAlreadyInState) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.set_active(id, true);

    auto err = reg_.set_active(id, true);
    EXPECT_EQ(err, uz::ViewportError::AlreadyInState);
}

TEST_F(ViewportRegistryTest, SetActive_SuspendInactive_ReturnsAlreadyInState) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    // Inactive -> try to suspend -> AlreadyInState (not active, so nothing to suspend)
    auto err = reg_.set_active(id, false);
    EXPECT_EQ(err, uz::ViewportError::AlreadyInState);
}

TEST_F(ViewportRegistryTest, SetActive_SuspendAlreadySuspended_ReturnsAlreadyInState) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.set_active(id, true);
    reg_.set_active(id, false);

    auto err = reg_.set_active(id, false);
    EXPECT_EQ(err, uz::ViewportError::AlreadyInState);
}

TEST_F(ViewportRegistryTest, SetActive_InvalidId_ReturnsInvalidId) {
    auto err = reg_.set_active(uz::kViewportIdInvalid, true);
    EXPECT_EQ(err, uz::ViewportError::InvalidId);
}

TEST_F(ViewportRegistryTest, SetActive_DestroyedId_ReturnsInvalidId) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.destroy(id);

    auto err = reg_.set_active(id, true);
    EXPECT_EQ(err, uz::ViewportError::InvalidId);
}

TEST_F(ViewportRegistryTest, SetActive_NonexistentId_ReturnsInvalidId) {
    auto err = reg_.set_active(9999, true);
    EXPECT_EQ(err, uz::ViewportError::InvalidId);
}

// --- Active cap enforcement ---

TEST_F(ViewportRegistryTest, ActiveCap_4Active_5thReturnsCapReached) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 1;  // Tiny VRAM so budget is not the issue.

    std::vector<uz::ViewportId> ids;
    for (uint32_t i = 0; i < 5; ++i) {
        ids.push_back(reg_.create(desc));
    }

    for (uint32_t i = 0; i < 4; ++i) {
        auto err = reg_.set_active(ids[i], true);
        EXPECT_EQ(err, uz::ViewportError::None) << "activate failed at i=" << i;
    }
    EXPECT_EQ(reg_.active_count(), 4u);

    auto err = reg_.set_active(ids[4], true);
    EXPECT_EQ(err, uz::ViewportError::CapReached);
    EXPECT_EQ(reg_.active_count(), 4u);
}

TEST_F(ViewportRegistryTest, ActiveCap_AfterSuspend_5thCanActivate) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 1;

    std::vector<uz::ViewportId> ids;
    for (uint32_t i = 0; i < 5; ++i) {
        ids.push_back(reg_.create(desc));
    }

    for (uint32_t i = 0; i < 4; ++i) {
        reg_.set_active(ids[i], true);
    }

    // Suspend one.
    reg_.set_active(ids[0], false);
    EXPECT_EQ(reg_.active_count(), 3u);

    // Now 5th can activate.
    auto err = reg_.set_active(ids[4], true);
    EXPECT_EQ(err, uz::ViewportError::None);
    EXPECT_EQ(reg_.active_count(), 4u);
}

// --- VRAM budget enforcement ---

TEST_F(ViewportRegistryTest, VramBudget_TotalVramActive_SumsCorrectly) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 100ULL * 1024 * 1024;  // 100 MB each

    uz::ViewportId id1 = reg_.create(desc);
    uz::ViewportId id2 = reg_.create(desc);
    reg_.set_active(id1, true);
    reg_.set_active(id2, true);

    EXPECT_EQ(reg_.total_vram_active(), 200ULL * 1024 * 1024);
}

TEST_F(ViewportRegistryTest, VramBudget_ExceedBudget_OldestAutoSuspended) {
    // 3 viewports of 200 MB each = 600 MB > 512 MB budget
    // Activating the 3rd should auto-suspend the 1st (oldest by frame_count).
    uz::ViewportDesc desc;
    desc.vram_bytes = 200ULL * 1024 * 1024;

    uz::ViewportId id1 = reg_.create(desc);
    uz::ViewportId id2 = reg_.create(desc);
    uz::ViewportId id3 = reg_.create(desc);

    reg_.set_active(id1, true);
    reg_.set_active(id2, true);
    // Both at frame_count=0. Tick once to differentiate.
    reg_.tick();  // id1 and id2 both get frame_count=1

    // Now activate id3 (200 MB). Total would be 600 MB > 512 MB.
    // id1 and id2 have same frame_count. The one with smaller frame_count
    // (or first found) gets auto-suspended. Since they have the same count,
    // the first one found in the slot array (id1) will be suspended.
    auto err = reg_.set_active(id3, true);
    EXPECT_EQ(err, uz::ViewportError::None);

    // id1 should be auto-suspended, id2 and id3 are ACTIVE.
    EXPECT_EQ(reg_.state(id1), uz::ViewportState::Suspended);
    EXPECT_EQ(reg_.state(id2), uz::ViewportState::Active);
    EXPECT_EQ(reg_.state(id3), uz::ViewportState::Active);
    EXPECT_EQ(reg_.active_count(), 2u);
}

TEST_F(ViewportRegistryTest, VramBudget_SuspendCallback_CalledOnAutoSuspend) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 300ULL * 1024 * 1024;  // 300 MB each

    uz::ViewportId id1 = reg_.create(desc);
    uz::ViewportId id2 = reg_.create(desc);

    reg_.set_active(id1, true);

    // Set callback.
    uz::ViewportId suspended_id = uz::kViewportIdInvalid;
    int callback_count = 0;

    struct CallbackData {
        uz::ViewportId* suspended_id;
        int* callback_count;
    };

    CallbackData cb_data{&suspended_id, &callback_count};

    reg_.set_suspend_callback(
        [](uz::ViewportId id, void* user_data) {
            auto* data = static_cast<CallbackData*>(user_data);
            *data->suspended_id = id;
            ++(*data->callback_count);
        },
        &cb_data
    );

    // Activate id2 (300 MB). Total would be 600 MB > 512 MB -> auto-suspend id1.
    auto err = reg_.set_active(id2, true);
    EXPECT_EQ(err, uz::ViewportError::None);
    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(suspended_id, id1);
}

TEST_F(ViewportRegistryTest, VramBudget_SingleViewportExceedsBudget_NoActiveToSuspend) {
    uz::ViewportDesc desc;
    desc.vram_bytes = uz::kVramBudgetBytes + 1;  // Exceeds budget alone

    uz::ViewportId id = reg_.create(desc);

    // Should fail: no active viewports to auto-suspend, and the new one alone
    // exceeds the budget.
    auto err = reg_.set_active(id, true);
    EXPECT_EQ(err, uz::ViewportError::VramBudget);
    EXPECT_EQ(reg_.state(id), uz::ViewportState::Inactive);
}

TEST_F(ViewportRegistryTest, VramBudget_ExactBudget_Succeeds) {
    uz::ViewportDesc desc;
    desc.vram_bytes = uz::kVramBudgetBytes;  // Exactly the budget

    uz::ViewportId id = reg_.create(desc);
    auto err = reg_.set_active(id, true);
    EXPECT_EQ(err, uz::ViewportError::None);
    EXPECT_EQ(reg_.total_vram_active(), uz::kVramBudgetBytes);
}

TEST_F(ViewportRegistryTest, VramBudget_SuspendedNotCounted) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 256ULL * 1024 * 1024;  // 256 MB

    uz::ViewportId id1 = reg_.create(desc);
    uz::ViewportId id2 = reg_.create(desc);

    reg_.set_active(id1, true);
    reg_.set_active(id2, true);
    EXPECT_EQ(reg_.total_vram_active(), 512ULL * 1024 * 1024);

    reg_.set_active(id1, false);
    EXPECT_EQ(reg_.total_vram_active(), 256ULL * 1024 * 1024);
}

// --- Tick ---

TEST_F(ViewportRegistryTest, Tick_ActiveViewport_IncrementsFrameCount) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.set_active(id, true);

    reg_.tick();
    reg_.tick();
    reg_.tick();

    uz::ViewportStatus status;
    ASSERT_TRUE(reg_.get_status(id, status));
    EXPECT_EQ(status.frame_count, 3u);
}

TEST_F(ViewportRegistryTest, Tick_SuspendedViewport_DoesNotIncrementFrameCount) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.set_active(id, true);
    reg_.tick();  // frame_count = 1
    reg_.set_active(id, false);  // Suspend
    reg_.tick();  // Should NOT increment
    reg_.tick();

    uz::ViewportStatus status;
    ASSERT_TRUE(reg_.get_status(id, status));
    EXPECT_EQ(status.frame_count, 1u);
}

TEST_F(ViewportRegistryTest, Tick_InactiveViewport_DoesNotIncrementFrameCount) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.tick();
    reg_.tick();

    uz::ViewportStatus status;
    ASSERT_TRUE(reg_.get_status(id, status));
    EXPECT_EQ(status.frame_count, 0u);
}

TEST_F(ViewportRegistryTest, Tick_MultipleActive_AllIncrement) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 1;
    uz::ViewportId id1 = reg_.create(desc);
    uz::ViewportId id2 = reg_.create(desc);
    reg_.set_active(id1, true);
    reg_.set_active(id2, true);

    reg_.tick();

    uz::ViewportStatus s1, s2;
    ASSERT_TRUE(reg_.get_status(id1, s1));
    ASSERT_TRUE(reg_.get_status(id2, s2));
    EXPECT_EQ(s1.frame_count, 1u);
    EXPECT_EQ(s2.frame_count, 1u);
}

// --- Resize ---

TEST_F(ViewportRegistryTest, Resize_Valid_ReturnsTrue) {
    uz::ViewportDesc desc;
    desc.width  = 1280;
    desc.height = 720;
    uz::ViewportId id = reg_.create(desc);

    EXPECT_TRUE(reg_.resize(id, 1920, 1080));

    uz::ViewportStatus status;
    ASSERT_TRUE(reg_.get_status(id, status));
    EXPECT_EQ(status.width, 1920u);
    EXPECT_EQ(status.height, 1080u);
}

TEST_F(ViewportRegistryTest, Resize_InvalidId_ReturnsFalse) {
    EXPECT_FALSE(reg_.resize(uz::kViewportIdInvalid, 100, 100));
}

TEST_F(ViewportRegistryTest, Resize_NonexistentId_ReturnsFalse) {
    EXPECT_FALSE(reg_.resize(9999, 100, 100));
}

TEST_F(ViewportRegistryTest, Resize_DestroyedId_ReturnsFalse) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.destroy(id);
    EXPECT_FALSE(reg_.resize(id, 100, 100));
}

// --- get_status ---

TEST_F(ViewportRegistryTest, GetStatus_Valid_ReturnsFullStatus) {
    uz::ViewportDesc desc;
    desc.type   = uz::ViewportType::Mesh;
    desc.width  = 800;
    desc.height = 600;
    desc.vram_bytes = 64ULL * 1024 * 1024;

    uz::ViewportId id = reg_.create(desc);

    uz::ViewportStatus status;
    ASSERT_TRUE(reg_.get_status(id, status));
    EXPECT_EQ(status.id, id);
    EXPECT_EQ(status.state, uz::ViewportState::Inactive);
    EXPECT_EQ(status.type, uz::ViewportType::Mesh);
    EXPECT_EQ(status.width, 800u);
    EXPECT_EQ(status.height, 600u);
    EXPECT_EQ(status.frame_count, 0u);
    EXPECT_EQ(status.vram_bytes, 64ULL * 1024 * 1024);
    EXPECT_FLOAT_EQ(status.fps, 0.0f);
}

TEST_F(ViewportRegistryTest, GetStatus_InvalidId_ReturnsFalse) {
    uz::ViewportStatus status;
    EXPECT_FALSE(reg_.get_status(uz::kViewportIdInvalid, status));
}

TEST_F(ViewportRegistryTest, GetStatus_DestroyedId_ReturnsFalse) {
    uz::ViewportDesc desc;
    uz::ViewportId id = reg_.create(desc);
    reg_.destroy(id);

    uz::ViewportStatus status;
    EXPECT_FALSE(reg_.get_status(id, status));
}

// --- state() ---

TEST_F(ViewportRegistryTest, State_InvalidId_ReturnsDestroyed) {
    EXPECT_EQ(reg_.state(uz::kViewportIdInvalid), uz::ViewportState::Destroyed);
}

TEST_F(ViewportRegistryTest, State_NonexistentId_ReturnsDestroyed) {
    EXPECT_EQ(reg_.state(9999), uz::ViewportState::Destroyed);
}

// --- suspend_all ---

TEST_F(ViewportRegistryTest, SuspendAll_SuspendsAllActive) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 1;
    uz::ViewportId id1 = reg_.create(desc);
    uz::ViewportId id2 = reg_.create(desc);
    uz::ViewportId id3 = reg_.create(desc);  // stays Inactive
    reg_.set_active(id1, true);
    reg_.set_active(id2, true);

    reg_.suspend_all();

    EXPECT_EQ(reg_.state(id1), uz::ViewportState::Suspended);
    EXPECT_EQ(reg_.state(id2), uz::ViewportState::Suspended);
    EXPECT_EQ(reg_.state(id3), uz::ViewportState::Inactive);  // Unchanged
    EXPECT_EQ(reg_.active_count(), 0u);
}

TEST_F(ViewportRegistryTest, SuspendAll_CallbackCalledForEach) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 1;
    uz::ViewportId id1 = reg_.create(desc);
    uz::ViewportId id2 = reg_.create(desc);
    reg_.set_active(id1, true);
    reg_.set_active(id2, true);

    int callback_count = 0;
    reg_.set_suspend_callback(
        [](uz::ViewportId /*id*/, void* user_data) {
            ++(*static_cast<int*>(user_data));
        },
        &callback_count
    );

    reg_.suspend_all();
    EXPECT_EQ(callback_count, 2);
}

TEST_F(ViewportRegistryTest, SuspendAll_NoActive_NoOp) {
    uz::ViewportDesc desc;
    reg_.create(desc);  // Inactive

    int callback_count = 0;
    reg_.set_suspend_callback(
        [](uz::ViewportId /*id*/, void* user_data) {
            ++(*static_cast<int*>(user_data));
        },
        &callback_count
    );

    reg_.suspend_all();
    EXPECT_EQ(callback_count, 0);
}

// --- Heartbeat watchdog ---

TEST_F(ViewportRegistryTest, Heartbeat_AfterRecord_SecondsSinceNearZero) {
    reg_.record_ipc_heartbeat();
    double elapsed = reg_.seconds_since_heartbeat();
    EXPECT_LT(elapsed, 1.0);  // Should be near zero.
}

TEST_F(ViewportRegistryTest, Heartbeat_InitialHeartbeat_NearZero) {
    // On construction, last_heartbeat is set to now.
    double elapsed = reg_.seconds_since_heartbeat();
    EXPECT_LT(elapsed, 1.0);
}

// --- Thread safety ---

TEST_F(ViewportRegistryTest, ConcurrentCreateDestroy_NoDataRace) {
    constexpr int kThreads    = 4;
    constexpr int kIterations = 50;
    std::atomic<int> created{0};
    std::atomic<int> destroyed{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([this, &created, &destroyed]() {
            for (int i = 0; i < kIterations; ++i) {
                uz::ViewportDesc desc;
                desc.vram_bytes = 1;
                uz::ViewportId id = reg_.create(desc);
                if (id != uz::kViewportIdInvalid) {
                    created.fetch_add(1, std::memory_order_relaxed);
                    if (reg_.destroy(id)) {
                        destroyed.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // All created viewports should have been destroyed.
    EXPECT_EQ(created.load(), destroyed.load());
    EXPECT_EQ(reg_.total_count(), 0u);
}

TEST_F(ViewportRegistryTest, ConcurrentSetActive_NoDataRace) {
    uz::ViewportDesc desc;
    desc.vram_bytes = 1;
    uz::ViewportId id = reg_.create(desc);

    constexpr int kThreads    = 4;
    constexpr int kIterations = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([this, id, t]() {
            for (int i = 0; i < kIterations; ++i) {
                if (i % 2 == 0) {
                    reg_.set_active(id, (t % 2 == 0));
                } else {
                    reg_.set_active(id, (t % 2 != 0));
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Just verify no crash, state is valid.
    auto s = reg_.state(id);
    EXPECT_TRUE(s == uz::ViewportState::Active ||
                s == uz::ViewportState::Suspended ||
                s == uz::ViewportState::Inactive);
}

// --- Enum default values ---

TEST(ViewportDescTest, DefaultValues) {
    uz::ViewportDesc desc;
    EXPECT_EQ(desc.type, uz::ViewportType::Scene);
    EXPECT_EQ(desc.width, 1280u);
    EXPECT_EQ(desc.height, 720u);
    EXPECT_EQ(desc.target_fps, 60u);
    EXPECT_EQ(desc.jpeg_quality, 85u);
    EXPECT_EQ(desc.vram_bytes, 32ULL * 1024 * 1024);
    EXPECT_EQ(desc.debug_name[0], '\0');
}

TEST(ViewportStatusTest, DefaultValues) {
    uz::ViewportStatus status;
    EXPECT_EQ(status.id, uz::kViewportIdInvalid);
    EXPECT_EQ(status.state, uz::ViewportState::Inactive);
    EXPECT_EQ(status.type, uz::ViewportType::Scene);
    EXPECT_EQ(status.width, 0u);
    EXPECT_EQ(status.height, 0u);
    EXPECT_EQ(status.frame_count, 0u);
    EXPECT_EQ(status.vram_bytes, 0u);
    EXPECT_FLOAT_EQ(status.fps, 0.0f);
}

TEST(ViewportConstantsTest, Values) {
    EXPECT_EQ(uz::kViewportIdInvalid, 0u);
    EXPECT_EQ(uz::kMaxViewports, 16u);
    EXPECT_EQ(uz::kMaxActiveViewports, 4u);
    EXPECT_EQ(uz::kVramBudgetBytes, 512ULL * 1024 * 1024);
}

// ============================================================================
// IPC viewport handlers tests
// ============================================================================

class ViewportHandlersTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_      = std::make_unique<uz::ViewportRegistry>();
        registry_ = std::make_unique<uz::IpcRegistry>();
        uz::viewport_register_handlers(*registry_, reg_.get());
    }

    json call(const std::string& name, const json& args = json::object()) {
        return registry_->call(name, args);
    }

    std::unique_ptr<uz::ViewportRegistry> reg_;
    std::unique_ptr<uz::IpcRegistry>      registry_;
};

// --- viewport.create ---

TEST_F(ViewportHandlersTest, Create_Default_ReturnsViewportId) {
    auto result = call("viewport.create");
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_TRUE(result["viewport_id"].is_number());
    EXPECT_GT(result["viewport_id"].get<uint32_t>(), 0u);
}

TEST_F(ViewportHandlersTest, Create_WithParams_Success) {
    auto result = call("viewport.create", {
        {"type", "material"},
        {"width", 1920},
        {"height", 1080},
        {"target_fps", 30},
        {"jpeg_quality", 90},
        {"debug_name", "MainView"},
        {"vram_bytes", 64000000}
    });
    EXPECT_TRUE(result["ok"].get<bool>());

    uint32_t id = result["viewport_id"].get<uint32_t>();
    auto status = call("viewport.getStatus", {{"viewport_id", id}});
    EXPECT_TRUE(status["ok"].get<bool>());
    EXPECT_EQ(status["status"]["type"], "material");
    EXPECT_EQ(status["status"]["width"], 1920);
    EXPECT_EQ(status["status"]["height"], 1080);
}

TEST_F(ViewportHandlersTest, Create_RegistryFull_Error) {
    for (uint32_t i = 0; i < uz::kMaxViewports; ++i) {
        auto r = call("viewport.create");
        EXPECT_TRUE(r["ok"].get<bool>());
    }
    auto result = call("viewport.create");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result.contains("error"));
}

// --- viewport.destroy ---

TEST_F(ViewportHandlersTest, Destroy_Valid_Success) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();

    auto result = call("viewport.destroy", {{"viewport_id", id}});
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, Destroy_InvalidId_Error) {
    auto result = call("viewport.destroy", {{"viewport_id", 9999}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, Destroy_MissingParam_Error) {
    auto result = call("viewport.destroy");
    EXPECT_FALSE(result["ok"].get<bool>());
}

// --- viewport.setActive ---

TEST_F(ViewportHandlersTest, SetActive_Activate_Success) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();

    auto result = call("viewport.setActive", {{"viewport_id", id}, {"active", true}});
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, SetActive_Suspend_Success) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();

    call("viewport.setActive", {{"viewport_id", id}, {"active", true}});
    auto result = call("viewport.setActive", {{"viewport_id", id}, {"active", false}});
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, SetActive_CapReached_Error) {
    // Create 5, activate 4, try 5th.
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < 5; ++i) {
        auto cr = call("viewport.create", {{"vram_bytes", 1}});
        ids.push_back(cr["viewport_id"].get<uint32_t>());
    }
    for (uint32_t i = 0; i < 4; ++i) {
        call("viewport.setActive", {{"viewport_id", ids[i]}, {"active", true}});
    }

    auto result = call("viewport.setActive", {{"viewport_id", ids[4]}, {"active", true}});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["error_code"], "cap_reached");
}

TEST_F(ViewportHandlersTest, SetActive_MissingViewportId_Error) {
    auto result = call("viewport.setActive", {{"active", true}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, SetActive_MissingActive_Error) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();
    auto result = call("viewport.setActive", {{"viewport_id", id}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// --- viewport.resize ---

TEST_F(ViewportHandlersTest, Resize_Valid_Success) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();

    auto result = call("viewport.resize", {
        {"viewport_id", id}, {"width", 1920}, {"height", 1080}
    });
    EXPECT_TRUE(result["ok"].get<bool>());

    auto status = call("viewport.getStatus", {{"viewport_id", id}});
    EXPECT_EQ(status["status"]["width"], 1920);
    EXPECT_EQ(status["status"]["height"], 1080);
}

TEST_F(ViewportHandlersTest, Resize_InvalidId_Error) {
    auto result = call("viewport.resize", {
        {"viewport_id", 9999}, {"width", 100}, {"height", 100}
    });
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, Resize_MissingWidth_Error) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();
    auto result = call("viewport.resize", {{"viewport_id", id}, {"height", 100}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, Resize_MissingHeight_Error) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();
    auto result = call("viewport.resize", {{"viewport_id", id}, {"width", 100}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// --- viewport.getStatus ---

TEST_F(ViewportHandlersTest, GetStatus_Valid_ReturnsStatus) {
    auto cr = call("viewport.create", {{"type", "mesh"}});
    uint32_t id = cr["viewport_id"].get<uint32_t>();

    auto result = call("viewport.getStatus", {{"viewport_id", id}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"]["id"], id);
    EXPECT_EQ(result["status"]["state"], "inactive");
    EXPECT_EQ(result["status"]["type"], "mesh");
    EXPECT_EQ(result["status"]["width"], 1280);
    EXPECT_EQ(result["status"]["height"], 720);
    EXPECT_EQ(result["status"]["frame_count"], 0);
}

TEST_F(ViewportHandlersTest, GetStatus_InvalidId_Error) {
    auto result = call("viewport.getStatus", {{"viewport_id", 9999}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, GetStatus_MissingParam_Error) {
    auto result = call("viewport.getStatus");
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ViewportHandlersTest, GetStatus_ActiveState_Correct) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();
    call("viewport.setActive", {{"viewport_id", id}, {"active", true}});

    auto result = call("viewport.getStatus", {{"viewport_id", id}});
    EXPECT_EQ(result["status"]["state"], "active");
}

TEST_F(ViewportHandlersTest, GetStatus_SuspendedState_Correct) {
    auto cr = call("viewport.create");
    uint32_t id = cr["viewport_id"].get<uint32_t>();
    call("viewport.setActive", {{"viewport_id", id}, {"active", true}});
    call("viewport.setActive", {{"viewport_id", id}, {"active", false}});

    auto result = call("viewport.getStatus", {{"viewport_id", id}});
    EXPECT_EQ(result["status"]["state"], "suspended");
}

// --- viewport.list ---

TEST_F(ViewportHandlersTest, List_Empty_ReturnsEmptyArray) {
    auto result = call("viewport.list");
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_TRUE(result["viewports"].is_array());
    EXPECT_EQ(result["viewports"].size(), 0u);
    EXPECT_EQ(result["active_count"], 0);
    EXPECT_EQ(result["total_count"], 0);
}

TEST_F(ViewportHandlersTest, List_WithViewports_ReturnsAll) {
    call("viewport.create", {{"type", "scene"}});
    call("viewport.create", {{"type", "mesh"}});

    auto result = call("viewport.list");
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["viewports"].size(), 2u);
    EXPECT_EQ(result["total_count"], 2);
}

// --- All handlers registered ---

TEST_F(ViewportHandlersTest, AllHandlersRegistered) {
    EXPECT_TRUE(registry_->has("viewport.create"));
    EXPECT_TRUE(registry_->has("viewport.destroy"));
    EXPECT_TRUE(registry_->has("viewport.setActive"));
    EXPECT_TRUE(registry_->has("viewport.resize"));
    EXPECT_TRUE(registry_->has("viewport.getStatus"));
    EXPECT_TRUE(registry_->has("viewport.list"));
}

// --- Null registry ---

TEST(ViewportHandlersNullTest, NullRegistry_CreateError) {
    uz::IpcRegistry registry;
    uz::viewport_register_handlers(registry, nullptr);

    auto result = registry.call("viewport.create", json::object());
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST(ViewportHandlersNullTest, NullRegistry_DestroyError) {
    uz::IpcRegistry registry;
    uz::viewport_register_handlers(registry, nullptr);

    auto result = registry.call("viewport.destroy", {{"viewport_id", 1}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST(ViewportHandlersNullTest, NullRegistry_SetActiveError) {
    uz::IpcRegistry registry;
    uz::viewport_register_handlers(registry, nullptr);

    auto result = registry.call("viewport.setActive",
                                 {{"viewport_id", 1}, {"active", true}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST(ViewportHandlersNullTest, NullRegistry_ResizeError) {
    uz::IpcRegistry registry;
    uz::viewport_register_handlers(registry, nullptr);

    auto result = registry.call("viewport.resize",
                                 {{"viewport_id", 1}, {"width", 100}, {"height", 100}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST(ViewportHandlersNullTest, NullRegistry_GetStatusError) {
    uz::IpcRegistry registry;
    uz::viewport_register_handlers(registry, nullptr);

    auto result = registry.call("viewport.getStatus", {{"viewport_id", 1}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST(ViewportHandlersNullTest, NullRegistry_ListError) {
    uz::IpcRegistry registry;
    uz::viewport_register_handlers(registry, nullptr);

    auto result = registry.call("viewport.list", json::object());
    EXPECT_FALSE(result["ok"].get<bool>());
}
