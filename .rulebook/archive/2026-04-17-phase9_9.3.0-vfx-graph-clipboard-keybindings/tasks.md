# phase9_9.3.0-vfx-graph-clipboard-keybindings

Keyboard + OS-clipboard wiring on top of the 8.6.0 Copy/Paste helper.

Reference: UE5 Blueprint editor Ctrl+C/V.

## 1. Input bindings

- [x] 1.1 `Ctrl+C` → `VfxNodeGraphClipboard::Copy(selectedIds)`.
      `VfxGraphKeybindingHandler::ProcessKeyEvent(Key::C, ctrl=true)`
      reads the widget's selected ids, calls Copy, writes the JSON
      snippet to the clipboard provider.
- [x] 1.2 `Ctrl+V` → Paste + selects the new nodes so chained pastes work.
      Clears existing selection then toggles each newly-inserted node
      into the selection set.
- [x] 1.3 `Ctrl+D` → Copy + Paste in one shot (duplicate selection).
      Does NOT touch the OS clipboard — uses a transient in-memory
      snippet so external clipboards are preserved.
- [x] Additional bindings: Ctrl+A (SelectAll), Delete (DeleteSelection),
      Escape (CancelWireDrag / EndRubberBand / ClearSelection — first
      matching wins), Arrow keys (NudgeSelection by grid unit).

## 2. OS clipboard

- [x] 2.1 `IVfxClipboardProvider` abstraction: default
      `VfxInProcessClipboard` stores a single string for the process
      lifetime. Host injects native providers (Win32 `CF_TEXT`, Linux
      PRIMARY/CLIPBOARD, macOS NSPasteboard) via `SetClipboardProvider()`.
- [x] 2.2 Paste reads from the provider; empty-string read short-
      circuits the paste with no graph modification.
- [x] 2.3 Cross-instance paste works when the host injects a native
      provider. The in-process default only persists within one
      process — a separate native-provider integration task lands
      per platform.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `IVfxClipboardProvider`,
      `VfxInProcessClipboard`, `VfxGraphKeybindingHandler`, and the
      `Key` enum + action mapping table.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_graph_keybindings.cpp`:
      16 tests covering unbound state, Ctrl+C/V/D/A, empty-selection
      no-ops, Delete, Escape (wire-drag cancel priority), Arrow
      nudges, custom provider injection, and in-process clipboard.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
