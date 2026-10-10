## 1. Implementation - Event types + InputRouter

- [x] 1.1 `runtime/engine/ui/slate_input.h`: structs `PointerEvent`, `KeyEvent`, enum `EventReply { Unhandled, Handled, HandledCapture }`, `enum PointerButton`.
- [x] 1.2 Adicionar event vmethods ao `Widget`: `OnPointerDown/Up/Move/Wheel`, `OnKeyDown/Up`, `OnHoverEnter/Leave`. Default `Unhandled`. Plus `HitTestChildren()` virtual + `m_parent` weak ref + `SetParent()/GetParent()`.
- [x] 1.3 `class InputRouter` com root + state (m_hover, m_capture). API `DispatchPointer(PointerEvent)`, `DispatchKey(KeyEvent)`, `SetRoot(SP<Widget>)`, `ReleaseCapture()`, `GetCapture()`, `GetHover()`.
- [x] 1.4 `runtime/engine/ui/slate_input.cpp`: `SetRoot` walks tree e wireia parent links. `DispatchPointer` faz hit-test profundo (later sibling = on top), bubbla up via parent chain. Hover diff old/new target (Enter/Leave). PointerCapture override hit-test pra Move/Up até release.

## 2. Implementation - Button widget

- [x] 2.1 `runtime/engine/ui/slate_button.h`: classe Button per proposal. Atributos NormalColor / HoverColor / PressedColor / CornerRadius / BorderColor / BorderThickness / Padding (Vec4 default {8,4,8,4}). SP<Widget> Content. callback OnClick. HitTestChildren() expõe Content.
- [x] 2.2 `runtime/engine/ui/slate_button.cpp`: Measure delega ao Content + padding (default 8/4/8/4 = 16W + 8H). Arrange aplica padding interno e propaga. Paint emite quad rounded da cor atual (Pressed > Hover > Normal cascade) com geometry, depois chama Content->Paint. OnPointerDown seta m_pressed=true, retorna HandledCapture (mantém tracking). OnPointerUp se m_pressed && pos dentro do widget fires OnClick + reseta m_pressed. OnHoverLeave defensivamente reseta m_pressed.

## 3. Visualization scope decision

- [x] 3.1 Standalone visual demo `slate_ui_step4` deliberately rolled forward into Step 5 (`slate_ui_step5`). Rationale: o demo de Step 5 é o perf-comparison demo (modo A `--no-invalidation` vs modo B `default`) e produz 2 screenshots contrastantes que provam renderer + input + invalidation pipeline juntos — visualização significativa, ao contrário de um único botão estático. Comportamento de input + click é coberto por 11 unit tests determinísticos.

## 4. Build + Verify

- [x] 4.1 `zig build` runtime — verde.
- [x] 4.2 Slate test suite agora 46/46 passing em 0 ms total.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 5.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/04-input-button.md`: PointerEvent / KeyEvent / EventReply, InputRouter routing rules (hit-test deep + bubble + capture + parent wire-up), Button anatomy + state machine ASCII diagram, hover/press semantics, design choices (auto-capture on Down, PointerMove unhandled by default on Button, defensive press drop on HoverLeave).
- [x] 5.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_input.cpp`: 11 tests covering: hit-test deepest-first, bubble Unhandled→parent, bubble skip when child Handled, HandledCapture redirects Move/Up to captor + release on Up, hover Enter/Leave on target change, no fire on same target, Button click on Press+Up inside, no click on Press outside + Up inside (Down never delivered), no click on drag-out + Up outside, defensive press drop on HoverLeave during press, Paint color cascade Idle→Hover→Pressed.
- [x] 5.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='Slate*.*'` → 46/46 PASSED (0 ms total).
