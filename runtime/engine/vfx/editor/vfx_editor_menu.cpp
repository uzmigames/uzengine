/// VFX Editor Menu Data — static tables (phase 12.7.0).
///
/// Declarative definition of the File / Edit / View / Presets / Help
/// top-menu bar. The overlay reads this at render time.

#include "vfx_editor_menu.h"

#include "vfx_editor_icons.h"

namespace uz {

namespace {

// ─── File ──────────────────────────────────────────────────────────────────

constexpr VfxMenuItem kFileItems[] = {
    {"New",      "Ctrl+N",       nullptr,         VfxMenuCommand::File_New,       true, false, 0},
    {"Open",     "Ctrl+O",       nullptr,         VfxMenuCommand::File_Open,      true, false, 0},
    {"Save",     "Ctrl+S",       nullptr,         VfxMenuCommand::File_Save,      true, false, 0},
    {"Save As",  "Ctrl+Shift+S", nullptr,         VfxMenuCommand::File_SaveAs,    true, false, 5},  // "Save _A_s"
    {nullptr,    nullptr,        nullptr,         VfxMenuCommand::None,           true, true,  -1},
    {"Recent Files", nullptr,    VFX_ICON_CHEVRON_RIGHT,
                                                  VfxMenuCommand::File_Recent,   true, false, 0},
    {nullptr,    nullptr,        nullptr,         VfxMenuCommand::None,           true, true,  -1},
    {"Exit",     "Alt+F4",       nullptr,         VfxMenuCommand::File_Exit,      true, false, 1},  // "E_x_it"
};

// ─── Edit ──────────────────────────────────────────────────────────────────

constexpr VfxMenuItem kEditItems[] = {
    {"Undo",        "Ctrl+Z",       nullptr, VfxMenuCommand::Edit_Undo,        true, false, 0},
    {"Redo",        "Ctrl+Y",       nullptr, VfxMenuCommand::Edit_Redo,        true, false, 0},
    {nullptr, nullptr, nullptr,              VfxMenuCommand::None,             true, true, -1},
    {"Cut",         "Ctrl+X",       nullptr, VfxMenuCommand::Edit_Cut,         true, false, 2}, // "Cu_t_"
    {"Copy",        "Ctrl+C",       nullptr, VfxMenuCommand::Edit_Copy,        true, false, 0},
    {"Paste",       "Ctrl+V",       nullptr, VfxMenuCommand::Edit_Paste,       true, false, 0},
    {"Delete",      "Del",          nullptr, VfxMenuCommand::Edit_Delete,      true, false, 0},
    {nullptr, nullptr, nullptr,              VfxMenuCommand::None,             true, true, -1},
    {"Preferences", nullptr,        nullptr, VfxMenuCommand::Edit_Preferences, true, false, 0},
};

// ─── View ──────────────────────────────────────────────────────────────────

constexpr VfxMenuItem kViewItems[] = {
    {"Niagara Mode",      "F11", nullptr, VfxMenuCommand::View_NiagaraMode},
    {nullptr, nullptr, nullptr,            VfxMenuCommand::None, true, true},
    {"Show Emitter List", nullptr, nullptr, VfxMenuCommand::View_ShowEmitterList},
    {"Show Timeline",     nullptr, nullptr, VfxMenuCommand::View_ShowTimeline},
    {"Show Curves",       nullptr, nullptr, VfxMenuCommand::View_ShowCurves},
    {"Show Parameters",   nullptr, nullptr, VfxMenuCommand::View_ShowParameters},
    {nullptr, nullptr, nullptr,            VfxMenuCommand::None, true, true},
    {"Toggle Stats",      "F2",  nullptr, VfxMenuCommand::View_ToggleStats},
};

// ─── Presets (placeholder — overlay generates per-preset rows) ────────────

constexpr VfxMenuItem kPresetsItems[] = {
    {"(loaded at runtime)", nullptr, nullptr, VfxMenuCommand::None, false, false},
};

// ─── Help ──────────────────────────────────────────────────────────────────

constexpr VfxMenuItem kHelpItems[] = {
    {"Documentation", "F1", nullptr, VfxMenuCommand::Help_Docs},
    {"About",         nullptr, nullptr, VfxMenuCommand::Help_About},
};

// ─── Top-bar ───────────────────────────────────────────────────────────────

constexpr VfxTopMenu kTopMenus[] = {
    {"File",    kFileItems,    sizeof(kFileItems)    / sizeof(kFileItems[0]),    0},
    {"Edit",    kEditItems,    sizeof(kEditItems)    / sizeof(kEditItems[0]),    0},
    {"View",    kViewItems,    sizeof(kViewItems)    / sizeof(kViewItems[0]),    0},
    {"Presets", kPresetsItems, sizeof(kPresetsItems) / sizeof(kPresetsItems[0]), 0},
    {"Help",    kHelpItems,    sizeof(kHelpItems)    / sizeof(kHelpItems[0]),    0},
};

constexpr size_t kTopMenuCount = sizeof(kTopMenus) / sizeof(kTopMenus[0]);

} // namespace

const VfxTopMenu* VfxMenuBarDefinition(size_t& outTopCount) noexcept {
    outTopCount = kTopMenuCount;
    return kTopMenus;
}

size_t VfxMenuBarTopCount() noexcept {
    return kTopMenuCount;
}

const VfxTopMenu* VfxMenuBarTopAt(size_t index) noexcept {
    if (index >= kTopMenuCount) return nullptr;
    return &kTopMenus[index];
}

// ─────────────────────────────────────────────────────────────────────────────
// Accelerator matching (phase 14.12)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

inline char ToLowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

} // namespace

int VfxMatchAccelerator(char ascii) noexcept {
    const char target = ToLowerAscii(ascii);
    for (size_t i = 0; i < kTopMenuCount; ++i) {
        const VfxTopMenu& m = kTopMenus[i];
        if (m.label == nullptr) continue;
        const int idx = m.acceleratorIndex;
        if (idx < 0) continue;
        if (m.label[idx] == '\0') continue;
        if (ToLowerAscii(m.label[idx]) == target) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int VfxMatchAcceleratorInMenu(char ascii, const VfxMenuItem* items,
                               size_t count) noexcept {
    if (items == nullptr) return -1;
    const char target = ToLowerAscii(ascii);
    for (size_t i = 0; i < count; ++i) {
        const VfxMenuItem& it = items[i];
        if (it.isSeparator)      continue;
        if (!it.enabled)          continue;
        if (it.label == nullptr) continue;
        const int ai = it.acceleratorIndex;
        if (ai < 0) continue;
        if (it.label[ai] == '\0') continue;
        if (ToLowerAscii(it.label[ai]) == target) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace uz
