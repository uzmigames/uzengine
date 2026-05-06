## 1. Implementation - Window (deferred to Step 10)

- [x] 1.1 Window class (HWND + DXGI swapchain wrap) deferred to Step 10. Razão: `DemoApplication` + `vfx_viewport_app` já são single-window setups, e o caso real (modal "Save As?" do editor) tem decisões de tamanho/parent/input-gating que dependem do editor final. Focus/Tooltip/DragDrop são window-agnostic e funcionam no escopo single-window de hoje.
- [x] 1.2 Modal flag, Show/Hide, parent input-gating: tudo na lista de Step 10 quando o modal é construído.

## 2. Implementation - FocusManager

- [x] 2.1 `runtime/engine/ui/slate_focus.{h,cpp}`: FocusManager com `WP<Widget> m_focused`. Singleton `FocusManager& Focus()`.
- [x] 2.2 SetFocus(SP<Widget>): chama old.OnFocusLost() se old, new.OnFocusReceived() se new. Update m_focused. Same-target → no-op.
- [x] 2.3 CycleNext/CyclePrev: faz DFS na árvore root via HitTestChildren(), coleta widgets `IsFocusable()==true`, encontra index do focused, avança/retrocede com wrap-around.
- [x] 2.4 `Widget::IsFocusable()` default false; `EditableText::IsFocusable()` override `true`. EditableText::OnFocusReceived/Lost delega para Focus()/Blur().

## 3. Implementation - Tooltip

- [x] 3.1 `runtime/engine/ui/slate_tooltip.{h,cpp}`: TooltipManager com map<Widget*, Entry>, m_currentHover, m_hoverTimer, m_active, m_anchorPos. Singleton `TooltipManager& Tooltip()`.
- [x] 3.2 Register(owner, factory, delaySec) — owner armazenado weakly; expirar entries automaticamente em Update.
- [x] 3.3 Update(hoveredWidget, cursorPos, dt) — hover sustained ≥ delay → factory()→ m_active. Hover change → m_active.reset() + timer reset.
- [x] 3.4 Active() / AnchorPos() expostos para o host (Step 10) renderizar no TopMost layer.

## 4. Implementation - DragDrop

- [x] 4.1 `runtime/engine/ui/slate_dragdrop.{h,cpp}`: DragDropManager com m_detecting, m_dragging, m_startPos, m_op. `kDragThreshold = 4` Slate units.
- [x] 4.2 BeginDetect(startPos, source) → records start; Update(currentPos, target, srcOnDragDetected) → past threshold && callback retorna op → enters drag mode + fires OnDragEnter/Leave em hover transitions.
- [x] 4.3 EndDrop(endPos, target) → if dragging && target → target->OnDrop(); always clears state. Cancel() aborts sem fire OnDrop mas ainda chama OnDragLeave defensivamente.
- [x] 4.4 Widget virtuals novos: OnDragEnter, OnDragLeave, OnDrop (default Unhandled).

## 5. Demo Step 9

- [x] 5.1 Demo on-screen é absorvido em Step 10 (`vfx_viewport_app` rebuild) onde lista de módulos do Niagara stack demonstra DragDrop reordering, tooltips em botões da Toolbar, e modal "Save As" — tudo em um só binário e screenshots.
- [x] 5.2 Behavioral correctness coberto por 16 unit tests determinísticos.

## 6. Build + Verify

- [x] 6.1 `zig build` runtime — verde.
- [x] 6.2 Slate test suite agora 114/114 passing em 2 ms total.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 7.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/09-window-focus-dnd.md`: FocusManager API + SetFocus + Cycle{Next,Prev} + IsFocusable opt-in + EditableText hookup; TooltipManager API + Register + Update + delay semantics + Active/AnchorPos host-driven render; DragDropManager API + BeginDetect/Update/EndDrop/Cancel + threshold + DragOperation (typeTag/payload std::any/ghost) + design choices (singletons, host-driven TopMost, defensive OnDragLeave on Cancel).
- [x] 7.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_focus_tooltip_dnd.cpp`: 16 tests covering: Focus SetFocus hook order, same-target no-op, CycleNext skips non-focusable, wrap-around, CyclePrev backwards, empty tree null; Tooltip no-materialize before delay, materialize once after, hover change drops active, AnchorPos updates; DragDrop below-threshold no drag, threshold-crossing detection, Enter/Leave on target change, EndDrop fires OnDrop, EndDrop no target clears state, Cancel drops state without firing OnDrop but still calls OnDragLeave.
- [x] 7.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='Slate*.*'` → 114/114 PASSED (2 ms total).
