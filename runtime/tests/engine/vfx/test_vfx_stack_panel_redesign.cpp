/// Unit tests for phase 12.2.0: `VfxStackPanelWidget` Niagara-parity
/// redesign — emitter header card, stage headers with inline add
/// buttons, module cards with drag-handle + checkbox + overflow menu
/// sub-rects, context-menu callback, `VfxModuleIconFor` helper.

#include <gtest/gtest.h>

#include <string>

#include "engine/vfx/editor/vfx_editor_icons.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"
#include "engine/vfx/stack/vfx_stack.h"

using namespace uz;

// Forward-declaration of the helper exported from the widget .cpp so
// tests can exercise the module-icon resolution without touching
// internal headers.
namespace uz {
    const char* VfxModuleIconFor(const std::string& moduleTypeName) noexcept;
}

namespace {

constexpr VfxStackRect kPanel{0.0f, 0.0f, 300.0f, 600.0f};

VfxStack MakeStackWith(const char* type, VfxStage stage,
                       const char* id = "m_1") {
    VfxStack s;
    VfxModuleInstance mod;
    mod.moduleTypeName = type;
    mod.instanceId     = id;
    s.GetEntry(stage).Add(std::move(mod));
    return s;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Emitter header
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelRedesign, EmitterHeader_NotEmittedWhenNameEmpty) {
    VfxStack empty;
    VfxStackPanelWidget w;
    w.Bind(&empty);

    const auto layout = w.ComputeLayout(kPanel);
    for (const auto& it : layout) {
        EXPECT_NE(it.type, VfxStackItemType::EmitterHeader)
            << "Emitter header emitted with empty name";
    }
}

TEST(VfxStackPanelRedesign, EmitterHeader_PrecedesAllStages) {
    VfxStack empty;
    VfxStackPanelWidget w;
    w.Bind(&empty);
    w.SetEmitterName("Fireball");

    const auto layout = w.ComputeLayout(kPanel);
    ASSERT_FALSE(layout.empty());
    EXPECT_EQ(layout[0].type, VfxStackItemType::EmitterHeader);
    // Subsequent items should be stage headers / add buttons.
    for (size_t i = 1; i < layout.size(); ++i) {
        EXPECT_NE(layout[i].type, VfxStackItemType::EmitterHeader)
            << "Emitter header duplicated at index " << i;
    }
}

TEST(VfxStackPanelRedesign, EmitterHeader_GrowsContentHeight) {
    VfxStack empty;
    VfxStackPanelWidget w;
    w.Bind(&empty);
    const float hNoName = w.ComputeContentHeight();
    w.SetEmitterName("Fireball");
    const float hWithName = w.ComputeContentHeight();
    EXPECT_GT(hWithName, hNoName);
    EXPECT_NEAR(hWithName - hNoName,
                w.emitterHeaderHeight + w.stageGap, 0.001f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Inline StageAddButton replaces AddRow
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelRedesign, ComputeLayout_NoAddRowItems) {
    VfxStack stk;
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    for (const auto& it : layout) {
        EXPECT_NE(it.type, VfxStackItemType::AddRow)
            << "AddRow emitted for stage " << static_cast<int>(it.stage);
    }
}

TEST(VfxStackPanelRedesign, ComputeLayout_OneStageAddButtonPerStage) {
    VfxStack stk;
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    int addButtons = 0;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::StageAddButton) ++addButtons;
    }
    EXPECT_EQ(addButtons, kVfxStageCount);
}

TEST(VfxStackPanelRedesign, StageAddButton_SitsAtRightEdgeOfHeader) {
    VfxStack stk;
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    const VfxStackLayoutItem* header = nullptr;
    const VfxStackLayoutItem* add    = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::StageHeader &&
            it.stage == VfxStage::ParticleSpawn) {
            header = &it;
        } else if (it.type == VfxStackItemType::StageAddButton &&
                   it.stage == VfxStage::ParticleSpawn) {
            add = &it;
        }
    }
    ASSERT_NE(header, nullptr);
    ASSERT_NE(add, nullptr);
    // The add button must sit within the header's vertical range and
    // past the header's midpoint horizontally.
    EXPECT_GE(add->rect.y, header->rect.y - 0.01f);
    EXPECT_LE(add->rect.y + add->rect.height,
              header->rect.y + header->rect.height + 0.01f);
    EXPECT_GT(add->rect.x, header->rect.x + header->rect.width * 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Module card sub-rects
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelRedesign, ModuleCard_EmitsAllFourSubRects) {
    VfxStack stk = MakeStackWith("turbulence", VfxStage::ParticleUpdate, "m_x");
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    bool gotCard = false, gotGrip = false, gotCheckbox = false, gotOverflow = false;
    for (const auto& it : layout) {
        if (it.instanceId != "m_x") continue;
        switch (it.type) {
        case VfxStackItemType::ModuleCard:       gotCard     = true; break;
        case VfxStackItemType::CardDragHandle:   gotGrip     = true; break;
        case VfxStackItemType::CardCheckbox:     gotCheckbox = true; break;
        case VfxStackItemType::CardOverflowMenu: gotOverflow = true; break;
        default: break;
        }
    }
    EXPECT_TRUE(gotCard);
    EXPECT_TRUE(gotGrip);
    EXPECT_TRUE(gotCheckbox);
    EXPECT_TRUE(gotOverflow);
}

TEST(VfxStackPanelRedesign, HitTest_OverflowMenuWinsOverCard) {
    VfxStack stk = MakeStackWith("turbulence", VfxStage::ParticleUpdate, "m_x");
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    const VfxStackLayoutItem* over = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::CardOverflowMenu &&
            it.instanceId == "m_x") { over = &it; break; }
    }
    ASSERT_NE(over, nullptr);
    const float cx = over->rect.x + over->rect.width * 0.5f;
    const float cy = over->rect.y + over->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxStackItemType::CardOverflowMenu);
    EXPECT_EQ(hit.instanceId, "m_x");
}

TEST(VfxStackPanelRedesign, HitTest_DragHandleWinsOverCard) {
    VfxStack stk = MakeStackWith("turbulence", VfxStage::ParticleUpdate, "m_x");
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    const VfxStackLayoutItem* grip = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::CardDragHandle &&
            it.instanceId == "m_x") { grip = &it; break; }
    }
    ASSERT_NE(grip, nullptr);
    const float cx = grip->rect.x + grip->rect.width * 0.5f;
    const float cy = grip->rect.y + grip->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxStackItemType::CardDragHandle);
}

TEST(VfxStackPanelRedesign, HitTest_StageAddButtonWinsOverHeader) {
    VfxStack stk;
    VfxStackPanelWidget w;
    w.Bind(&stk);

    const auto layout = w.ComputeLayout(kPanel);
    const VfxStackLayoutItem* add = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxStackItemType::StageAddButton &&
            it.stage == VfxStage::EmitterSpawn) { add = &it; break; }
    }
    ASSERT_NE(add, nullptr);
    const float cx = add->rect.x + add->rect.width * 0.5f;
    const float cy = add->rect.y + add->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxStackItemType::StageAddButton);
    EXPECT_EQ(hit.stage, VfxStage::EmitterSpawn);
}

// ─────────────────────────────────────────────────────────────────────────────
// Context-menu callback
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelRedesign, ContextMenuCallback_FieldExistsAndIsAssignable) {
    VfxStackPanelWidget w;
    bool called = false;
    std::string gotId;
    float gotX = 0, gotY = 0;
    w.onModuleContextMenuRequested =
        [&](const std::string& id, float x, float y) {
            called = true; gotId = id; gotX = x; gotY = y;
        };
    w.onModuleContextMenuRequested("m_abc", 100.0f, 200.0f);
    EXPECT_TRUE(called);
    EXPECT_EQ(gotId, "m_abc");
    EXPECT_FLOAT_EQ(gotX, 100.0f);
    EXPECT_FLOAT_EQ(gotY, 200.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxModuleIconFor
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModuleIconFor, KnownModulesMapToCategoryIcons) {
    // `turbulence` is categorized "Simulation" -> lifetime icon.
    EXPECT_STREQ(VfxModuleIconFor("turbulence"), VFX_ICON_CAT_LIFETIME);
    // `attractor` is categorized "Forces" -> force icon.
    EXPECT_STREQ(VfxModuleIconFor("attractor"), VFX_ICON_CAT_FORCE);
    // `sprite_render` is categorized "Render" -> render icon.
    EXPECT_STREQ(VfxModuleIconFor("sprite_render"), VFX_ICON_CAT_RENDER);
}

TEST(VfxModuleIconFor, UnknownModuleFallsBackToGeneric) {
    EXPECT_STREQ(VfxModuleIconFor("nonexistent_module_xyz"),
                 VFX_ICON_CAT_GENERIC);
    EXPECT_STREQ(VfxModuleIconFor(""), VFX_ICON_CAT_GENERIC);
}

// ─────────────────────────────────────────────────────────────────────────────
// Emitter-enabled pointer
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackPanelRedesign, SetEmitterEnabledPtr_StoresPointer) {
    VfxStackPanelWidget w;
    bool flag = true;
    w.SetEmitterEnabledPtr(&flag);
    EXPECT_EQ(w.EmitterEnabledPtr(), &flag);
    w.SetEmitterEnabledPtr(nullptr);
    EXPECT_EQ(w.EmitterEnabledPtr(), nullptr);
}
