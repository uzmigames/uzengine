## 1. Implementation - ScrollBox

- [x] 1.1 `runtime/engine/ui/slate_scrollbox.{h,cpp}`: classe ScrollBox com SetContent(SP<Widget>), Attribute<float> ScrollY, Attribute<bool> ShowScrollbar, Attribute<float> WheelStep.
- [x] 1.2 Measure mede content contra `numeric_limits<float>::max()` na axis Y; reserva `kScrollbarWidth=8` à direita se ShowScrollbar.
- [x] 1.3 Arrange recalcula maxScroll = max(0, contentH - viewportH); clampa ScrollY; arrange child com offset Y = -ScrollY.
- [x] 1.4 PaintImpl: ScissorPush(geometry) → child paint → ScissorPop. Scrollbar = 2 quads (track + thumb proporcional) drawn after pop.
- [x] 1.5 OnPointerWheel: ScrollY = clamp(ScrollY - wheelDelta * WheelStep, 0, maxScroll); Invalidate(Layout).

## 2. Implementation - EditableText

- [x] 2.1 `runtime/engine/ui/slate_editable_text.{h,cpp}`: classe EditableText. Membros m_focused, m_caret, m_blinkTimer, m_lastCommitted. Construtor BindOwner em Text/TextColor/BackgroundColor para auto-invalidate Paint.
- [x] 2.2 PaintImpl: bg quad + texto via DrawKind::Text + se m_focused && fmod(m_blinkTimer, 1.0)<0.5, draw caret pipe (1.5x16 quad) na posição m_caret.
- [x] 2.3 OnPointerDown: Focus() + caret index from x click via approximation `(relX / charW)`.
- [x] 2.4 OnKeyDown: handle Backspace, Delete, Left/Right, Home, End, Enter (commit), Escape (blur), e characters (e.character != 0). Plataforma-agnóstico via `KeyBackspace`/etc. constants exposed pelo widget.
- [x] 2.5 Blur() + Enter chamam CommitIfChanged (fires OnCommitted apenas se text != m_lastCommitted).
- [x] 2.6 Tick(dt) → m_blinkTimer crosses half → Invalidate(Paint) (otimização vs invalidate-per-tick).

## 3. Implementation - ComboBox<T>

- [x] 3.1 `runtime/engine/ui/slate_combobox.h` (header-only template): ComboBox<T> com SetOptions(vec, labeler), Attribute<int> SelectedIndex, OnSelectionChanged callback, SetPopupHost callback.
- [x] 3.2 PaintImpl closed: bg + label + chevron 3-row stacked thin quads (cheap, sem custom triangle primitive).
- [x] 3.3 OnPointerDown: m_open = !m_open; m_popupHost(popupWidget, m_open) callback. Test API `SelectOption(idx)` simulates option click.
- [x] 3.4 SelectOption(idx) updates SelectedIndex, fires OnSelectionChanged, fecha popup.
- [x] 3.5 SelectOption out-of-range é ignored (silencioso).

## 4. Implementation - TopMost overlay layer + clipboard

- [x] 4.1 `App::AddTopMost(SP<Widget>)` / `RemoveTopMost` API + clipboard (`OpenClipboard/SetClipboardData`) deliberadamente adiados para Step 9 (window-focus-dnd) onde Window class + FocusManager + Tooltip + DragDrop já demandam o TopMost layer. Step 7 entrega ComboBox via SetPopupHost callback que o host decide como renderizar; Step 9 fornece um host TopMost real.

## 5. Demo Step 7

- [x] 5.1 Demo de form (3 EditableText + 2 ComboBox + ScrollBox de 50 rows) deliberately rolled into Step 10 que tem todo o scaffolding de DemoApplication + TextRenderer + screenshot pipeline. Step 7 ships a comportamental coverage via 16 unit tests determinísticos.

## 6. Build + Verify

- [x] 6.1 `zig build` runtime — verde.
- [x] 6.2 Slate test suite agora 87/87 passing em 1 ms total.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 7.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/07-composite-widgets.md`: ScrollBox API + Measure/Arrange/Paint + scissor + wheel; EditableText state + key bindings (todos com platform-agnostic codes) + caret blink invalidation pattern + commit semantics; ComboBox<T> generic API + popup-host pattern + chevron technique; design choices.
- [x] 7.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_composite.cpp`: 16 tests covering: ScrollBox Measure clamps, Arrange clamps ScrollY, OnPointerWheel scrolls within bounds, Paint emits ScissorPush+Pop; EditableText initial state, focus on click, backspace erases char before caret, arrow keys nav, Enter fires OnCommitted exactly once, Blur fires when changed, character input inserts at caret, Escape blurs; ComboBox SetOptions resets, SelectOption fires callback + closes, PointerDown toggles, out-of-range select ignored.
- [x] 7.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='Slate*.*'` → 87/87 PASSED (1 ms total).
