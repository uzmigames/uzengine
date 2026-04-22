#pragma once

/// VFX Editor Menu Data — phase 12.7.0.
///
/// Declarative menu table for the VFX editor's top bar. The overlay
/// reads this definition at render time; tests can include just this
/// header to verify menu layout without standing up the GPU overlay.
///
/// # Contract
///
/// - `VfxMenuCommand` is a flat enum covering every actionable item
///   across every menu. The overlay fires
///   `onMenuCommand(VfxMenuCommand)` exactly once per click on a
///   non-separator, enabled menu item.
/// - `VfxMenuItem` aggregates a row's presentation (label, hotkey
///   hint, icon glyph) plus its action (command) and state flags
///   (enabled, separator).
/// - `VfxTopMenu` is one of the top-level menu bar entries
///   (File / Edit / View / Presets / Help) and owns an ordered list
///   of items.
/// - `VfxMenuBarDefinition()` returns the whole bar as a constant
///   array; it is stable across calls and safe to cache.
///
/// Adding a new menu command: extend the enum, add a row to the
/// appropriate top menu in `vfx_editor_menu.cpp`, then add a case in
/// the host's `onMenuCommand` dispatcher. The overlay renderer needs
/// no code change — it reads the table.

#include <cstddef>
#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VfxMenuCommand — every actionable item in the top menu bar.
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxMenuCommand : uint16_t {
    None = 0,

    // File
    File_New       = 10,
    File_Open      = 11,
    File_Save      = 12,
    File_SaveAs    = 13,
    File_Recent    = 14,   ///< Opens a recent-files submenu.
    File_Exit      = 15,

    // Edit
    Edit_Undo        = 20,
    Edit_Redo        = 21,
    Edit_Cut         = 22,
    Edit_Copy        = 23,
    Edit_Paste       = 24,
    Edit_Delete      = 25,
    Edit_Preferences = 26,

    // View
    View_NiagaraMode      = 30,
    View_ShowEmitterList  = 31,
    View_ShowTimeline     = 32,
    View_ShowCurves       = 33,
    View_ShowParameters   = 34,
    View_ToggleStats      = 35,

    // Presets — the overlay appends one command per loaded preset
    // starting at `Presets_Base` and using the preset index as offset.
    Presets_Base = 100,

    // Help
    Help_Docs  = 200,
    Help_About = 201,
};

// ─────────────────────────────────────────────────────────────────────────────
// VfxMenuItem — one row inside a dropdown.
// ─────────────────────────────────────────────────────────────────────────────

struct VfxMenuItem {
    const char* label        = nullptr;
    const char* hotkey       = nullptr;   ///< "Ctrl+S" etc. (may be empty).
    const char* iconGlyph    = nullptr;   ///< ASCII-safe glyph from vfx_editor_icons.h.
    VfxMenuCommand command   = VfxMenuCommand::None;
    bool        enabled      = true;
    bool        isSeparator  = false;     ///< When true, label / command ignored.
    /// Phase 14.8 — UTF-8 byte offset inside `label` of the
    /// accelerator character (the letter the renderer underlines
    /// and the Alt-chord opens). Default -1 disables the
    /// accelerator for this item.
    int         acceleratorIndex = -1;
};

// ─────────────────────────────────────────────────────────────────────────────
// VfxTopMenu — one of the top-level bar entries with its item list.
// ─────────────────────────────────────────────────────────────────────────────

struct VfxTopMenu {
    const char*         label = nullptr;       ///< e.g. "File", "Edit".
    const VfxMenuItem*  items = nullptr;
    size_t              itemCount = 0;
    /// Phase 14.8 — accelerator character index in `label` (see
    /// `VfxMenuItem::acceleratorIndex`). Default 0 picks the
    /// first character of each top-menu label (UE5 convention).
    int                 acceleratorIndex = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global accessor (stable table)
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the canonical menu-bar layout — File / Edit / View / Presets /
/// Help. The Presets menu's items are generated dynamically by the
/// overlay from the active preset name list; the static entry only
/// contains a single sentinel placeholder.
[[nodiscard]] const VfxTopMenu* VfxMenuBarDefinition(size_t& outTopCount) noexcept;

/// Convenience: returns the total number of top menus.
[[nodiscard]] size_t VfxMenuBarTopCount() noexcept;

/// Convenience: returns a pointer to the top menu at `index`, or
/// nullptr on out-of-range.
[[nodiscard]] const VfxTopMenu* VfxMenuBarTopAt(size_t index) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Accelerator matching (phase 14.12)
// ─────────────────────────────────────────────────────────────────────────────

/// Finds the top-menu index whose `acceleratorIndex` character
/// matches `ascii` (case-insensitive). Returns -1 when no menu
/// matches. Used by `HandleKey` to route Alt+letter to `OpenMenu`.
[[nodiscard]] int VfxMatchAccelerator(char ascii) noexcept;

/// Finds the first item in `items[0..count)` whose
/// `acceleratorIndex` character matches `ascii` (case-insensitive)
/// AND `enabled == true` AND `isSeparator == false`. Returns -1 on
/// miss. Used by `HandleKey` when a dropdown is open — bare letter
/// presses fire the matching command.
[[nodiscard]] int VfxMatchAcceleratorInMenu(char ascii,
                                             const VfxMenuItem* items,
                                             size_t count) noexcept;

} // namespace uz
