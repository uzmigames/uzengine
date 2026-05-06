## 1. Implementation - Event types + InputRouter

- [ ] 1.1 `runtime/engine/ui/slate_input.h`: structs `PointerEvent`, `KeyEvent`, enum `EventReply { Unhandled, Handled, HandledCapture }`.
- [ ] 1.2 Adicionar event vmethods ao `Widget`: `OnPointerMove/Down/Up`, `OnKey`, `OnHoverEnter/Leave`. Default `Unhandled`.
- [ ] 1.3 `class InputRouter` com root + state (m_hoverTarget, m_pointerCapture, m_focused). API `DispatchPointer(PointerEvent)`, `DispatchKey(KeyEvent)`, `SetRoot(SP<Widget>)`.
- [ ] 1.4 `runtime/engine/ui/slate_input.cpp`: `DispatchPointer` faz hit-test profundo (child antes parent), bubbla up. Hover diff old/new target. PointerCapture override hit-test pra Move/Up até release.

## 2. Implementation - Button widget

- [ ] 2.1 `runtime/engine/ui/slate_button.h`: classe Button per proposal. Atributos NormalColor / HoverColor / PressedColor / CornerRadius. SP<Widget> Content. callback OnClick.
- [ ] 2.2 `runtime/engine/ui/slate_button.cpp`: Measure delega ao Content (passing através) + 2*padding (4px). Arrange aplica padding interno e propaga. Paint emite quad rounded da cor atual (Pressed > Hover > Normal cascade) com geometry, depois chama Content->Paint. OnPointerDown seta m_pressed=true, retorna Handled. OnPointerUp se m_pressed && m_hovered fires OnClick + reseta.

## 3. Implementation - Demo Step 4

- [ ] 3.1 `runtime/demos/slate_ui_step4/main.cpp`: 1 Button "Click me" com TextBlock content; OnClick incrementa counter exibido em outro TextBlock no VBox abaixo.
- [ ] 3.2 Cria `InputRouter`, seta root = VBox. Per-frame, lê mouse do `InputSystem` e chama `router.DispatchPointer(...)`. Antes do paint, root.Tick → Measure → Arrange.
- [ ] 3.3 Suportar `--screenshot-frame` + headless mock click via CLI flag `--inject-click x y` para validar.

## 4. Build + Verify (visualização obrigatória)

- [ ] 4.1 Demo verde + screenshot baseline (sem hover, sem press) `.screenshots/slate-step4-idle.png`.
- [ ] 4.2 Hover screenshot: `--inject-hover X Y` (botão muda cor) → `.screenshots/slate-step4-hover.png`.
- [ ] 4.3 Press screenshot via inject-click → `.screenshots/slate-step4-press.png`. Counter deve incrementar.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 5.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/04-input-button.md`: PointerEvent / KeyEvent / EventReply, InputRouter routing rules (hit-test deep + bubble + capture), Button anatomy, hover/press state machine.
- [ ] 5.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_input.cpp`: (a) Hit-test retorna deepest widget; (b) bubble se child retorna Unhandled; (c) HandledCapture redireciona Move/Up para captor; (d) hover Enter/Leave fires when target muda; (e) Button OnClick fires only on Press → Up dentro do widget.
- [ ] 5.3 Run tests and confirm they pass — `--gtest_filter='SlateInput.*:SlateButton.*'` 100%.
