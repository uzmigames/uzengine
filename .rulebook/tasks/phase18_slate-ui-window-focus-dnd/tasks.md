## 1. Implementation - Window

- [ ] 1.1 `runtime/engine/ui/slate_window.{h,cpp}`: classe Window com WindowDesc { title, size, modal, parent }, ctor cria HWND + DXGI swapchain (reusa SwapchainManager).
- [ ] 1.2 Cada Window tem seu InputRouter, FocusManager local (ou ref ao global por agora).
- [ ] 1.3 Show/Hide via ShowWindow + SetForegroundWindow.
- [ ] 1.4 Modal flag: enquanto m_modal, App rejeita input em outras Windows (WS_EX_TOPMOST + lock at App level).
- [ ] 1.5 Destructor: DestroyWindow + libera swapchain.

## 2. Implementation - FocusManager

- [ ] 2.1 `runtime/engine/ui/slate_focus.{h,cpp}`: FocusManager com WP<Widget> m_focused.
- [ ] 2.2 SetFocus(SP<Widget>): chama old.OnFocusLost() se old, new.OnFocusReceived() se new. Update m_focused.
- [ ] 2.3 CycleNext/CyclePrev: faz DFS na árvore root, coleta widgets `IsFocusable()==true`, encontra index do focused, avança/retrocede.
- [ ] 2.4 `inline FocusManager& Focus()` singleton.
- [ ] 2.5 Widget::IsFocusable() default false; EditableText override true.

## 3. Implementation - Tooltip

- [ ] 3.1 `runtime/engine/ui/slate_tooltip.{h,cpp}`: TooltipManager com map<Widget*, callback+delay>.
- [ ] 3.2 Tooltip::Register(owner, contentFactory, delaySec) — owner morre → unregister via WP<Widget>.
- [ ] 3.3 App::Tick: para o widget atualmente sob mouse, m_hoverTimer += dt; se > delay e não tooltip mostrado, factory()→add to TopMost at cursor.
- [ ] 3.4 OnHoverLeave do widget = remove tooltip do TopMost + reset timer.

## 4. Implementation - DragDrop

- [ ] 4.1 `runtime/engine/ui/slate_dragdrop.{h,cpp}`: DragDropManager com SP<DragOperation> m_active.
- [ ] 4.2 InputRouter detecta: PointerDown → store start pos. PointerMove enquanto Down → se distance > 4px e callback OnDragDetected retornou DragOp, chama Begin.
- [ ] 4.3 Begin: m_active = op; ghost adicionado ao TopMost.
- [ ] 4.4 Update(cursor): ghost geometry follow cursor; hit-test alvo; OnDragEnter/Leave events nos widgets debaixo.
- [ ] 4.5 End(cursor): hit-test final, OnDrop(op), remove ghost, m_active = nullptr.
- [ ] 4.6 Widget virtuais novos: OnDragDetected, OnDragEnter, OnDragLeave, OnDrop (default Unhandled).

## 5. Implementation - Demo Step 9

- [ ] 5.1 `runtime/demos/slate_ui_step9/main.cpp`: main window com:
  - Toolbar com 3 botões registered Tooltip ("New", "Save", "Discard").
  - Lista (VBox) de 5 items drag-and-drop reorderable.
  - Botão "Open Save Dialog" abre Window modal com EditableText + 2 buttons.
- [ ] 5.2 Modal funcional: clicks no parent ignorados; close modal volta foco ao parent.
- [ ] 5.3 Drag de item da lista mostra ghost; drop em outra posição reordena (lista é std::vector reordenado).

## 6. Build + Verify (visualização obrigatória)

- [ ] 6.1 Demo verde + 4 screenshots: `slate-step9-tooltip.png` (botão hovered + tooltip visible), `slate-step9-modal.png` (modal aberto sobre parent), `slate-step9-drag-mid.png` (item sendo arrastado, ghost visible), `slate-step9-drag-end.png` (lista reordenada após drop).
- [ ] 6.2 Verificar via Read tool: tooltip aparece após delay, modal escurece parent (input gated), ghost segue cursor.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 7.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/09-window-focus-dnd.md`: Window + modal flow + FocusManager tab cycle + Tooltip Register + DragDrop full lifecycle (detect/begin/update/end).
- [ ] 7.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_window_focus_dnd.cpp`: (a) FocusManager.SetFocus dispara OnFocusReceived/Lost na ordem correta; (b) CycleNext pula widgets non-focusable; (c) Tooltip Register + hover > delay materializa widget no TopMost; (d) DragDrop threshold 4px (3px move NÃO inicia drag, 5px sim); (e) modal Window bloqueia input no parent.
- [ ] 7.3 Run tests and confirm they pass — `--gtest_filter='SlateWindowFocusDnd.*'` 100%.
