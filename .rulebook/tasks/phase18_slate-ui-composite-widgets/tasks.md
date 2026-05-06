## 1. Implementation - ScrollBox

- [ ] 1.1 `runtime/engine/ui/slate_scrollbox.{h,cpp}`: classe ScrollBox extends Widget com SetContent(SP<Widget>), Attribute<float> ScrollY, Attribute<bool> ShowScrollbar.
- [ ] 1.2 MeasureImpl retorna min(content desired, maxSize) + reserva 8px à direita se ShowScrollbar.
- [ ] 1.3 Arrange aplica translate(-ScrollY) ao child geometry; calcula m_maxScroll = max(0, contentH - viewportH).
- [ ] 1.4 PaintImpl emite ScissorPush(geometry) → child paint → ScissorPop. Scrollbar = 2 quads (track 8x viewportH, thumb 8x (viewportH/contentH * viewportH) at (ScrollY/maxScroll * (viewportH-thumbH))).
- [ ] 1.5 OnPointerWheel: ScrollY = clamp(ScrollY - e.delta * 30, 0, m_maxScroll); Invalidate(Paint).

## 2. Implementation - EditableText

- [ ] 2.1 `runtime/engine/ui/slate_editable_text.{h,cpp}`: classe EditableText extends Widget. Membros m_caret, m_selStart/End, m_focused, m_caretBlinkTimer.
- [ ] 2.2 PaintImpl: bg quad + texto via TextRenderer + se m_focused && fmod(m_caretBlinkTimer, 1.0)<0.5, draw caret pipe à posição m_caret. Se m_selStart != m_selEnd, draw highlight quad atrás.
- [ ] 2.3 OnPointerDown: focus claim via EventReply::HandledFocusable. m_caret = caret index from x click.
- [ ] 2.4 OnKeyDown: handle Backspace, Delete, Left/Right (com Shift = expand selection), Home, End, Ctrl+A, Ctrl+C, Ctrl+V (via clipboard helper), Enter → OnCommitted, Esc → focus lost.
- [ ] 2.5 OnFocusLost → if (Text != lastCommitted) call OnCommitted.
- [ ] 2.6 Tick(dt) → m_caretBlinkTimer += dt; Invalidate(Paint) só se cruzou half (otimização).

## 3. Implementation - ComboBox<T>

- [ ] 3.1 `runtime/engine/ui/slate_combobox.h` (header-only template): ComboBox<T> com SetOptions(vec, labeler), Attribute<int> SelectedIndex, OnSelectionChanged callback.
- [ ] 3.2 PaintImpl closed: bg + label + chevron 8x8 à direita.
- [ ] 3.3 OnPointerDown: m_open = !m_open. Quando abre, builda m_popup como VBox de Buttons (cada label) + adiciona ao TopMost overlay layer.
- [ ] 3.4 Click em option: SelectedIndex = idx; OnSelectionChanged(idx); m_open=false; remove m_popup do TopMost.
- [ ] 3.5 Click fora do popup: também fecha (TopMost OnPointerDownOutside callback).

## 4. Implementation - TopMost overlay layer + clipboard

- [ ] 4.1 Adicionar `m_topMost` como Overlay layer no root App (z=999, sobre tudo).
- [ ] 4.2 `App::AddTopMost(SP<Widget>)` / `RemoveTopMost(SP<Widget>)` API.
- [ ] 4.3 `runtime/engine/ui/slate_clipboard.h`: `std::string GetText()` / `SetText(string)` wrap em OpenClipboard/SetClipboardData (Windows).

## 5. Implementation - Demo Step 7 (form + scroll)

- [ ] 5.1 `runtime/demos/slate_ui_step7/main.cpp`: VBox top com 3 EditableText (Name, Email, Tags) + 2 ComboBox (Renderer={Mesh,Sprite,MeshRenderer}, Material={Lit,Unlit,Translucent}). Bottom = ScrollBox com 50 TextBlocks "Item N".
- [ ] 5.2 EditableText.OnCommitted printa no log + atualiza um TextBlock "Last commit: ...".
- [ ] 5.3 ComboBox OnSelectionChanged printa "Selected: <label>".

## 6. Build + Verify (visualização obrigatória)

- [ ] 6.1 Demo verde + 3 screenshots (`.screenshots/slate-step7-form.png`, `slate-step7-combobox-open.png`, `slate-step7-scroll-mid.png`).
- [ ] 6.2 Verificar via Read tool: caret aparece no EditableText focado, ComboBox aberto sobrepõe form (não fica clipped), ScrollBox scrollbar visível com thumb proporcional.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 7.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/07-composite-widgets.md`: ScrollBox API + EditableText keybinds (lista completa) + ComboBox popup TopMost flow + Clipboard helper.
- [ ] 7.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_composite.cpp`: (a) ScrollBox wheel ajusta ScrollY clamped; (b) EditableText backspace remove char antes do caret; (c) EditableText Ctrl+A seleciona tudo; (d) ComboBox SelectedIndex update dispara callback; (e) ComboBox open/close popup adiciona/remove de TopMost.
- [ ] 7.3 Run tests and confirm they pass — `--gtest_filter='SlateComposite.*'` 100%.
