#pragma once

/// VFX Node-Graph Keybindings — Ctrl+C / Ctrl+V / Ctrl+D / Ctrl+A wiring
/// on top of the phase 8.6.0 `VfxNodeGraphClipboard` and phase 10.4.0
/// `VfxNodeGraphWidget`.
///
/// Phase 9.3.0. The widget provides `onCopy` / `onPaste` callbacks but
/// does not actually invoke the clipboard helpers or OS-level clipboard.
/// This module ships:
///
/// 1. An **in-process clipboard** (static string) so Copy/Paste works
///    even when the OS clipboard is unavailable or disabled.
/// 2. An **OS clipboard bridge** (via Win32 `CF_TEXT` on Windows, an
///    injectable provider on other platforms) for cross-instance paste.
/// 3. A `VfxGraphKeybindingHandler` class that consumes keyboard state
///    each frame and dispatches to widget + clipboard.
///
/// # Registration pattern
///
/// ```cpp
/// VfxGraphKeybindingHandler keys;
/// keys.Bind(&widget);
///
/// // Each frame after reading input:
/// keys.Process(ctrlHeld, shiftHeld, input->GetTypedChars(), input->GetKeyEvents());
/// ```
///
/// @ref engine/vfx/editor/vfx_node_graph.h (VfxNodeGraphClipboard)
/// @ref engine/vfx/editor/vfx_node_graph_widget.h (widget)

#include "engine/vfx/editor/vfx_node_graph_widget.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// OS clipboard bridge
// ─────────────────────────────────────────────────────────────────────────────

/// OS clipboard accessor. Implementations push/pull the clipboard JSON
/// via the platform API. The default (`VfxInProcessClipboard`) only
/// persists for the process lifetime.
class IVfxClipboardProvider {
public:
    virtual ~IVfxClipboardProvider() = default;

    /// Writes `text` to the clipboard (UTF-8).
    virtual void Write(const std::string& text) = 0;

    /// Reads the clipboard as UTF-8 text. Returns empty string when the
    /// clipboard doesn't contain text.
    [[nodiscard]] virtual std::string Read() const = 0;
};

/// In-process clipboard — stores a single string for the process lifetime.
/// Works without any OS dependencies.
class VfxInProcessClipboard : public IVfxClipboardProvider {
public:
    void Write(const std::string& text) override { data_ = text; }
    [[nodiscard]] std::string Read() const override { return data_; }

    void Clear() noexcept { data_.clear(); }
    [[nodiscard]] bool IsEmpty() const noexcept { return data_.empty(); }

private:
    std::string data_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Keybinding handler
// ─────────────────────────────────────────────────────────────────────────────

/// Translates key events into widget + clipboard actions.
///
/// Mapping:
/// - Ctrl+C        → widget.SelectedIds() → `VfxNodeGraphClipboard::Copy` → provider.Write
/// - Ctrl+V        → provider.Read → `VfxNodeGraphClipboard::Paste` → widget.SelectAll(new ids)
/// - Ctrl+D        → Copy + Paste (duplicate selection, no OS clipboard write)
/// - Ctrl+A        → widget.SelectAll()
/// - Delete        → widget.DeleteSelection()
/// - Escape        → widget.ClearSelection() / CancelWireDrag() / EndRubberBand()
/// - Arrow keys    → widget.NudgeSelection()
class VfxGraphKeybindingHandler {
public:
    /// Binds to a widget. Must outlive the handler. Pass nullptr to unbind.
    void Bind(VfxNodeGraphWidget* widget) noexcept { widget_ = widget; }

    /// Sets the clipboard provider (defaults to in-process).
    void SetClipboardProvider(IVfxClipboardProvider* provider) noexcept {
        provider_ = provider;
    }

    /// Key codes used by `ProcessKeyEvent`. Matches VK_* on Windows at
    /// least for the common ones; the host maps its platform keys to
    /// these before calling Process.
    enum class Key : uint16_t {
        None = 0,
        A, C, V, D,
        Delete,
        Escape,
        Left, Right, Up, Down,
    };

    /// Processes a key-down event. Call once per unique keypress.
    ///
    /// # Arguments
    /// - `key`       — the key pressed.
    /// - `ctrlHeld`  — true if Ctrl (or Cmd on macOS) is held.
    /// - `shiftHeld` — true if Shift is held (currently unused but
    ///                  reserved for Shift+Arrow = range select).
    ///
    /// # Returns
    /// `true` when the handler consumed the key. Host should skip
    /// further dispatch when true.
    bool ProcessKeyEvent(Key key, bool ctrlHeld, bool shiftHeld);

    /// Diagnostic counters (for tests).
    [[nodiscard]] uint32_t CopyCount()   const noexcept { return copyCount_; }
    [[nodiscard]] uint32_t PasteCount()  const noexcept { return pasteCount_; }
    [[nodiscard]] uint32_t DeleteCount() const noexcept { return deleteCount_; }

private:
    bool DoCopy();
    bool DoPaste();
    bool DoDuplicate();
    bool DoDelete();
    bool DoSelectAll();
    bool DoClear();
    bool DoNudge(float dx, float dy);

    VfxNodeGraphWidget*    widget_   = nullptr;
    IVfxClipboardProvider* provider_ = nullptr;
    VfxInProcessClipboard  defaultProvider_;

    uint32_t copyCount_   = 0;
    uint32_t pasteCount_  = 0;
    uint32_t deleteCount_ = 0;
};

} // namespace uz
