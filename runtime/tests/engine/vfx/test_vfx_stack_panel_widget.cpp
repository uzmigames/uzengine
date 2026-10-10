/// Unit tests for the Niagara-parity stack panel widget (phase 11.2.0).
///
/// Covers the pure-data interaction math of `VfxStackPanelWidget`:
///   - Default expanded state (every stage starts expanded).
///   - `ToggleStage` + `SetStageExpanded` + `IsStageExpanded`.
///   - `ComputeContentHeight` changes with collapsed stages + populated modules.
///   - `ComputeLayout` emits the expected typed rects in draw order.
///   - Collapsed stages hide their module cards + AddRow from the layout.
///   - `HitTest` picks the most-specific item (checkbox > card > add > header).
///   - `ComputeInsertionIndex` returns the correct slot for various mouse-Y.
///   - `BeginDrag` + `EndDrag` produces the expected reorder, fires the
///     `onModuleMoved` callback, and cancels on cross-stage targets.
///   - `SelectModule` fires `onModuleSelected` only on change.
///
/// No TextRenderer / GPU context is instantiated — the test covers the
/// layout and interaction math, which is what the overlay routing depends on.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_stack_panel_widget.h"
#include "engine/vfx/stack/vfx_stack.h"

#include <string>
#include <vector>

using namespace uz;

namespace {

VfxModuleInstance MakeMod(const char* type, const char* id, bool enabled = true) {
    VfxModuleInstance m;
    m.moduleTypeName = type;
    m.instanceId     = id;
    m.enabled        = enabled;
    return m;
}

/// Build a populated stack for layout / interaction tests.
/// - ParticleSpawn: 1 module (`spawn_init`)
/// - ParticleUpdate: 3 modules (`upd_gravity`, `upd_drag`, `upd_curl`)
/// - Render: 1 module (`render_sprite`)
VfxStack MakeStack() {
    VfxStack s;
    s.GetEntry(VfxStage::ParticleSpawn).Add(MakeMod("Init", "spawn_init"));
    s.GetEntry(VfxStage::ParticleUpdate).Add(MakeMod("Gravity", "upd_gravity"));
    s.GetEntry(VfxStage::ParticleUpdate).Add(MakeMod("Drag", "upd_drag", false));
    s.GetEntry(VfxStage::ParticleUpdate).Add(MakeMod("Curl", "upd_curl"));
    s.GetEntry(VfxStage::Render).Add(MakeMod("Sprite", "render_sprite"));
    return s;
}

constexpr VfxStackRect kPanel{ 0.0f, 0.0f, 280.0f, 4000.0f };

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Expanded state
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelWidget, DefaultAllStagesExpanded) {
    VfxStackPanelWidget w;
    for (uint8_t i = 0; i < kVfxStageCount; ++i) {
        EXPECT_TRUE(w.IsStageExpanded(static_cast<VfxStage>(i)))
            << "Stage " << int(i) << " should default expanded";
    }
}

TEST(VfxStackPanelWidget, ToggleStage_FlipsAndReturns) {
    VfxStackPanelWidget w;
    EXPECT_FALSE(w.ToggleStage(VfxStage::Render));
    EXPECT_FALSE(w.IsStageExpanded(VfxStage::Render));
    EXPECT_TRUE(w.ToggleStage(VfxStage::Render));
    EXPECT_TRUE(w.IsStageExpanded(VfxStage::Render));
}

TEST(VfxStackPanelWidget, SetStageExpanded_WritesDirect) {
    VfxStackPanelWidget w;
    w.SetStageExpanded(VfxStage::SystemSpawn, false);
    EXPECT_FALSE(w.IsStageExpanded(VfxStage::SystemSpawn));
}

// ─────────────────────────────────────────────────────────────────────────────
// Binding & content height
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelWidget, Unbound_LayoutIsEmpty_HeightZero) {
    VfxStackPanelWidget w;
    EXPECT_EQ(w.Stack(), nullptr);
    EXPECT_FLOAT_EQ(w.ComputeContentHeight(), 0.0f);
    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_TRUE(layout.empty());
}

TEST(VfxStackPanelWidget, Bound_EmptyStack_HeadersAndInlineAddButtons) {
    // Phase 12.2.0: the standalone "+ Add Module" row was folded into the
    // stage header's inline `StageAddButton`, so an empty stack emits
    // exactly one StageHeader + one StageAddButton per stage and no
    // standalone AddRow items.
    VfxStack empty;
    VfxStackPanelWidget w;
    w.Bind(&empty);
    const auto layout = w.ComputeLayout(kPanel);

    int headers = 0, addButtons = 0, cards = 0, checkboxes = 0, legacyAddRows = 0;
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxStackItemType::StageHeader:      ++headers;      break;
        case VfxStackItemType::StageAddButton:   ++addButtons;   break;
        case VfxStackItemType::ModuleCard:       ++cards;        break;
        case VfxStackItemType::CardCheckbox:     ++checkboxes;   break;
        case VfxStackItemType::AddRow:           ++legacyAddRows; break;
        default: break;
        }
    }
    EXPECT_EQ(headers, kVfxStageCount);
    EXPECT_EQ(addButtons, kVfxStageCount);
    EXPECT_EQ(legacyAddRows, 0);
    EXPECT_EQ(cards, 0);
    EXPECT_EQ(checkboxes, 0);
}

TEST(VfxStackPanelWidget, Bound_Populated_ContentHeightGreaterThanEmpty) {
    VfxStack empty;
    VfxStack populated = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&empty);
    const float hEmpty = w.ComputeContentHeight();
    w.Bind(&populated);
    const float hFull = w.ComputeContentHeight();
    EXPECT_GT(hFull, hEmpty);
}

TEST(VfxStackPanelWidget, CollapsedStageHidesCards) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    w.SetStageExpanded(VfxStage::ParticleUpdate, false);

    const auto layout = w.ComputeLayout(kPanel);
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::ModuleCard ||
            it.type == VfxStackItemType::CardCheckbox ||
            it.type == VfxStackItemType::AddRow) {
            EXPECT_NE(it.stage, VfxStage::ParticleUpdate)
                << "Collapsed ParticleUpdate must not emit card/checkbox/add rows";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout structure
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelWidget, Layout_CardsAreIndentedFromHeaders) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    float headerX = -1.0f;
    float cardX   = -1.0f;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::StageHeader && headerX < 0.0f) {
            headerX = it.rect.x;
        }
        if (it.type == VfxStackItemType::ModuleCard && cardX < 0.0f) {
            cardX = it.rect.x;
        }
        if (headerX >= 0.0f && cardX >= 0.0f) break;
    }
    ASSERT_GE(headerX, 0.0f);
    ASSERT_GE(cardX, 0.0f);
    EXPECT_GT(cardX, headerX) << "Module cards must be indented vs headers";
}

TEST(VfxStackPanelWidget, Layout_CheckboxInsideCard) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    // Find the card + checkbox for "upd_gravity".
    const VfxStackLayoutItem* card = nullptr;
    const VfxStackLayoutItem* cb   = nullptr;
    for (const auto& it : layout) {
        if (it.instanceId == "upd_gravity") {
            if (it.type == VfxStackItemType::ModuleCard) card = &it;
            if (it.type == VfxStackItemType::CardCheckbox) cb = &it;
        }
    }
    ASSERT_NE(card, nullptr);
    ASSERT_NE(cb, nullptr);
    // Checkbox rect must be inside card rect.
    EXPECT_GE(cb->rect.x, card->rect.x);
    EXPECT_LE(cb->rect.x + cb->rect.width, card->rect.x + card->rect.width);
    EXPECT_GE(cb->rect.y, card->rect.y);
    EXPECT_LE(cb->rect.y + cb->rect.height, card->rect.y + card->rect.height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test precedence
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelWidget, HitTest_CheckboxBeatsCardInsideCheckbox) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    // Find the checkbox for the first Gravity module.
    const VfxStackLayoutItem* cb = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::CardCheckbox &&
            it.instanceId == "upd_gravity") {
            cb = &it; break;
        }
    }
    ASSERT_NE(cb, nullptr);

    const float cx = cb->rect.x + cb->rect.width * 0.5f;
    const float cy = cb->rect.y + cb->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxStackItemType::CardCheckbox);
    EXPECT_EQ(hit.instanceId, "upd_gravity");
}

TEST(VfxStackPanelWidget, HitTest_CardHitOutsideCheckbox) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    const VfxStackLayoutItem* card = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::ModuleCard &&
            it.instanceId == "upd_gravity") {
            card = &it; break;
        }
    }
    ASSERT_NE(card, nullptr);

    // Hit the far-right side of the card (outside the checkbox region).
    const float cx = card->rect.x + card->rect.width - 4.0f;
    const float cy = card->rect.y + card->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxStackItemType::ModuleCard);
    EXPECT_EQ(hit.instanceId, "upd_gravity");
}

TEST(VfxStackPanelWidget, HitTest_NoneWhenOutsidePanel) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);
    const auto layout = w.ComputeLayout(kPanel);
    const auto hit = w.HitTest(layout, -10.0f, -10.0f);
    EXPECT_FALSE(hit.IsHit());
}

TEST(VfxStackPanelWidget, HitTest_StageHeaderClick) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    const VfxStackLayoutItem* hdr = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::StageHeader &&
            it.stage == VfxStage::Render) {
            hdr = &it; break;
        }
    }
    ASSERT_NE(hdr, nullptr);
    const float cx = hdr->rect.x + 5.0f;
    const float cy = hdr->rect.y + hdr->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxStackItemType::StageHeader);
    EXPECT_EQ(hit.stage, VfxStage::Render);
}

TEST(VfxStackPanelWidget, HitTest_StageAddButton) {
    // Phase 12.2.0: the add action lives on the stage header's inline
    // `StageAddButton` sub-rect.
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    const VfxStackLayoutItem* add = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::StageAddButton &&
            it.stage == VfxStage::ParticleUpdate) {
            add = &it; break;
        }
    }
    ASSERT_NE(add, nullptr);
    const float cx = add->rect.x + add->rect.width * 0.5f;
    const float cy = add->rect.y + add->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxStackItemType::StageAddButton);
    EXPECT_EQ(hit.stage, VfxStage::ParticleUpdate);
}

// ─────────────────────────────────────────────────────────────────────────────
// Insertion index math
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelWidget, InsertionIndex_EmptyStageReturnsZero) {
    VfxStack empty;
    VfxStackPanelWidget w;
    w.Bind(&empty);
    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_EQ(
        w.ComputeInsertionIndex(layout, VfxStage::ParticleUpdate, 50.0f),
        0u);
}

TEST(VfxStackPanelWidget, InsertionIndex_CollapsedStageReturnsZero) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);
    w.SetStageExpanded(VfxStage::ParticleUpdate, false);
    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_EQ(
        w.ComputeInsertionIndex(layout, VfxStage::ParticleUpdate, 1000.0f),
        0u);
}

TEST(VfxStackPanelWidget, InsertionIndex_AboveFirstCardReturnsZero) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);
    const auto layout = w.ComputeLayout(kPanel);

    const VfxStackLayoutItem* first = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::ModuleCard &&
            it.stage == VfxStage::ParticleUpdate) {
            first = &it; break;
        }
    }
    ASSERT_NE(first, nullptr);
    const float yAbove = first->rect.y + 1.0f;  // above mid-row
    EXPECT_EQ(
        w.ComputeInsertionIndex(layout, VfxStage::ParticleUpdate, yAbove),
        0u);
}

TEST(VfxStackPanelWidget, InsertionIndex_BelowLastCardReturnsSize) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);
    const auto layout = w.ComputeLayout(kPanel);

    const VfxStackEntry& entry = stk.GetEntry(VfxStage::ParticleUpdate);
    const size_t count = entry.modules.size();
    ASSERT_EQ(count, 3u);

    // y very large → below all cards
    const float yFar = 9000.0f;
    EXPECT_EQ(
        w.ComputeInsertionIndex(layout, VfxStage::ParticleUpdate, yFar),
        count);
}

// ─────────────────────────────────────────────────────────────────────────────
// Drag reorder
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelWidget, Drag_NoOp_WhenIdMissing) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    w.BeginDrag("nonexistent");
    EXPECT_FALSE(w.IsDragging());
}

TEST(VfxStackPanelWidget, Drag_MoveForward_ReordersAndFiresCallback) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    std::string movedId;
    VfxStage    movedStage = VfxStage::SystemSpawn;
    size_t      movedIndex = 0;
    w.onModuleMoved = [&](const std::string& id, VfxStage s, size_t idx) {
        movedId    = id;
        movedStage = s;
        movedIndex = idx;
    };

    w.BeginDrag("upd_gravity");
    ASSERT_TRUE(w.IsDragging());
    EXPECT_EQ(w.DraggedFromStage(), VfxStage::ParticleUpdate);

    const bool moved = w.EndDrag(VfxStage::ParticleUpdate, 2u);
    EXPECT_TRUE(moved);
    EXPECT_FALSE(w.IsDragging());

    const auto& e = stk.GetEntry(VfxStage::ParticleUpdate);
    ASSERT_EQ(e.modules.size(), 3u);
    EXPECT_EQ(e.modules[2].instanceId, "upd_gravity");
    // Others preserved their relative order
    EXPECT_EQ(e.modules[0].instanceId, "upd_drag");
    EXPECT_EQ(e.modules[1].instanceId, "upd_curl");

    EXPECT_EQ(movedId, "upd_gravity");
    EXPECT_EQ(movedStage, VfxStage::ParticleUpdate);
    EXPECT_EQ(movedIndex, 2u);
}

TEST(VfxStackPanelWidget, Drag_MoveBackward_Reorders) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    w.BeginDrag("upd_curl");   // initially index 2
    ASSERT_TRUE(w.EndDrag(VfxStage::ParticleUpdate, 0u));

    const auto& e = stk.GetEntry(VfxStage::ParticleUpdate);
    EXPECT_EQ(e.modules[0].instanceId, "upd_curl");
    EXPECT_EQ(e.modules[1].instanceId, "upd_gravity");
    EXPECT_EQ(e.modules[2].instanceId, "upd_drag");
}

TEST(VfxStackPanelWidget, Drag_SameIndex_NoCallback) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    int calls = 0;
    w.onModuleMoved = [&](const std::string&, VfxStage, size_t) { ++calls; };

    w.BeginDrag("upd_gravity");
    EXPECT_FALSE(w.EndDrag(VfxStage::ParticleUpdate, 0u)); // same slot
    EXPECT_EQ(calls, 0);
}

TEST(VfxStackPanelWidget, Drag_CrossStage_Cancels) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    int calls = 0;
    w.onModuleMoved = [&](const std::string&, VfxStage, size_t) { ++calls; };

    w.BeginDrag("upd_gravity");
    EXPECT_FALSE(w.EndDrag(VfxStage::Render, 0u));
    EXPECT_FALSE(w.IsDragging());
    EXPECT_EQ(calls, 0);

    // Original order preserved
    const auto& pu = stk.GetEntry(VfxStage::ParticleUpdate);
    EXPECT_EQ(pu.modules[0].instanceId, "upd_gravity");
}

TEST(VfxStackPanelWidget, Drag_Cancel_LeavesStackUntouched) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    w.BeginDrag("upd_gravity");
    w.CancelDrag();
    EXPECT_FALSE(w.IsDragging());
    EXPECT_TRUE(w.DraggedInstanceId().empty());

    const auto& e = stk.GetEntry(VfxStage::ParticleUpdate);
    EXPECT_EQ(e.modules[0].instanceId, "upd_gravity");
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelWidget, Select_FiresCallbackOnlyOnChange) {
    VfxStack stk = MakeStack();
    VfxStackPanelWidget w;
    w.Bind(&stk);

    int calls = 0;
    std::string lastId;
    w.onModuleSelected = [&](const std::string& id) {
        ++calls;
        lastId = id;
    };

    w.SelectModule("upd_gravity");
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(lastId, "upd_gravity");
    EXPECT_EQ(w.SelectedInstanceId(), "upd_gravity");

    w.SelectModule("upd_gravity");  // unchanged
    EXPECT_EQ(calls, 1);

    w.SelectModule("render_sprite");
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(lastId, "render_sprite");

    w.SelectModule("");  // clear via callback path
    EXPECT_EQ(calls, 3);
    EXPECT_TRUE(w.SelectedInstanceId().empty());
}
