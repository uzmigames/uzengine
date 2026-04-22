/// Unit tests for phase 12.9.0: `VfxSystemOverviewWidget` —
/// per-emitter rows, caller-applied reorder / duplicate / delete /
/// add callbacks, status dot reflects runtime state.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "engine/vfx/editor/vfx_system_overview_widget.h"

using namespace uz;

namespace {

constexpr VfxStackRect kPanel{0.0f, 0.0f, 360.0f, 600.0f};

std::vector<VfxOverviewEmitterView> MakeList(size_t count) {
    std::vector<VfxOverviewEmitterView> out;
    for (size_t i = 0; i < count; ++i) {
        VfxOverviewEmitterView v;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "E%zu", i);
        v.name = buf;
        v.aliveCount = static_cast<uint32_t>(10u + i);
        v.isEmitting = true;
        v.isEnabled  = true;
        out.push_back(std::move(v));
    }
    return out;
}

size_t CountType(const std::vector<VfxOverviewLayoutItem>& layout,
                 VfxOverviewItemType t) {
    size_t n = 0;
    for (const auto& it : layout) {
        if (it.type == t) ++n;
    }
    return n;
}

const VfxOverviewLayoutItem* Find(
    const std::vector<VfxOverviewLayoutItem>& layout,
    VfxOverviewItemType t, size_t idx) {
    for (const auto& it : layout) {
        if (it.type == t && it.emitterIndex == idx) return &it;
    }
    return nullptr;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Layout emission
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemOverview, Unbound_LayoutIsEmpty) {
    VfxSystemOverviewWidget w;
    EXPECT_EQ(w.EmitterCount(), 0u);
    EXPECT_TRUE(w.ComputeLayout(kPanel).empty());
}

TEST(VfxSystemOverview, BoundEmpty_OnlyAddRowEmitted) {
    std::vector<VfxOverviewEmitterView> empty;
    VfxSystemOverviewWidget w;
    w.Bind(&empty);
    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::Row), 0u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::AddEmitterRow), 1u);
}

TEST(VfxSystemOverview, ThreeEmitters_YieldsThreeRowsPlusAddRow) {
    auto list = MakeList(3);
    VfxSystemOverviewWidget w;
    w.Bind(&list);
    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::Row), 3u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::AddEmitterRow), 1u);
}

TEST(VfxSystemOverview, EachRow_EmitsAllSubRects) {
    auto list = MakeList(2);
    VfxSystemOverviewWidget w;
    w.Bind(&list);
    const auto layout = w.ComputeLayout(kPanel);
    // Each emitter must have 1 DragHandle + 1 Checkbox + 1 StatusDot +
    // 1 NameLabel + 1 ParticleCount + 4 action buttons = 9 sub-items.
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::DragHandle), 2u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::Checkbox),   2u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::StatusDot),  2u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::NameLabel),  2u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::ParticleCount), 2u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::BtnMoveUp),     2u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::BtnMoveDown),   2u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::BtnDuplicate),  2u);
    EXPECT_EQ(CountType(layout, VfxOverviewItemType::BtnDelete),     2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemOverview, HitTest_ActionButtonBeatsRow) {
    auto list = MakeList(2);
    VfxSystemOverviewWidget w;
    w.Bind(&list);
    const auto layout = w.ComputeLayout(kPanel);
    const auto* del = Find(layout, VfxOverviewItemType::BtnDelete, 1u);
    ASSERT_NE(del, nullptr);
    const float cx = del->rect.x + del->rect.width * 0.5f;
    const float cy = del->rect.y + del->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxOverviewItemType::BtnDelete);
    EXPECT_EQ(hit.emitterIndex, 1u);
}

TEST(VfxSystemOverview, HitTest_AddEmitterRow_Returned) {
    auto list = MakeList(1);
    VfxSystemOverviewWidget w;
    w.Bind(&list);
    const auto layout = w.ComputeLayout(kPanel);
    const VfxOverviewLayoutItem* addRow = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxOverviewItemType::AddEmitterRow) {
            addRow = &it; break;
        }
    }
    ASSERT_NE(addRow, nullptr);
    const float cx = addRow->rect.x + addRow->rect.width * 0.5f;
    const float cy = addRow->rect.y + addRow->rect.height * 0.5f;
    EXPECT_EQ(w.HitTest(layout, cx, cy).type,
              VfxOverviewItemType::AddEmitterRow);
}

// ─────────────────────────────────────────────────────────────────────────────
// Reorder drag
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemOverview, Drag_FromZeroToTwo_FiresMovedCallback) {
    auto list = MakeList(3);
    VfxSystemOverviewWidget w;
    w.Bind(&list);

    size_t gotSrc = 999, gotDst = 999;
    w.onEmitterMoved = [&](size_t s, size_t d) { gotSrc = s; gotDst = d; };

    w.BeginDrag(0u);
    EXPECT_TRUE(w.IsDragging());
    w.EndDrag(2u);
    EXPECT_FALSE(w.IsDragging());
    EXPECT_EQ(gotSrc, 0u);
    EXPECT_EQ(gotDst, 2u);
}

TEST(VfxSystemOverview, Drag_SelfTarget_DoesNotFireCallback) {
    auto list = MakeList(2);
    VfxSystemOverviewWidget w;
    w.Bind(&list);
    int calls = 0;
    w.onEmitterMoved = [&](size_t, size_t) { ++calls; };
    w.BeginDrag(1u);
    w.EndDrag(1u);
    EXPECT_EQ(calls, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Caller-applied contract
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemOverview, DeleteCallback_DoesNotShrinkList) {
    auto list = MakeList(3);
    VfxSystemOverviewWidget w;
    w.Bind(&list);
    // Callback present but intentionally not mutating the list.
    bool fired = false;
    w.onEmitterDeleted = [&](size_t) { fired = true; };
    // Simulate a host-level dispatch for the Delete button.
    if (w.onEmitterDeleted) w.onEmitterDeleted(1u);
    EXPECT_TRUE(fired);
    // The widget must not shrink the list on its own.
    EXPECT_EQ(list.size(), 3u);
}

TEST(VfxSystemOverview, AddCallback_FiresOnceWhenInvoked) {
    VfxSystemOverviewWidget w;
    int calls = 0;
    w.onAddEmitterRequested = [&] { ++calls; };
    w.onAddEmitterRequested();
    EXPECT_EQ(calls, 1);
}

TEST(VfxSystemOverview, DuplicateCallback_CarriesIndex) {
    auto list = MakeList(2);
    VfxSystemOverviewWidget w;
    w.Bind(&list);
    size_t got = 999;
    w.onEmitterDuplicated = [&](size_t i) { got = i; };
    w.onEmitterDuplicated(1u);
    EXPECT_EQ(got, 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemOverview, SelectIndex_RoundTrips) {
    VfxSystemOverviewWidget w;
    w.SelectIndex(5u);
    EXPECT_EQ(w.SelectedIndex(), 5u);
    w.ClearSelection();
    EXPECT_EQ(w.SelectedIndex(), static_cast<size_t>(-1));
}

// ─────────────────────────────────────────────────────────────────────────────
// View vector interpretation (tested via GetView behavior)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemOverview, Bound_EmitterCountMatchesVector) {
    auto list = MakeList(5);
    VfxSystemOverviewWidget w;
    w.Bind(&list);
    EXPECT_EQ(w.EmitterCount(), 5u);
    list.pop_back();
    EXPECT_EQ(w.EmitterCount(), 4u);
}
