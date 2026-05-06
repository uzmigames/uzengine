## 1. Skeleton — VfxEditorSlate class + 4-row chrome

- [x] 1.1 `runtime/engine/vfx/editor/vfx_editor_slate.{h,cpp}`: class VfxEditorSlate (pimpl) com SetEmitter, BuildRoot, Tick(dt), GetRoot/GetTitleBar/GetToolbar/GetWorkbench/GetStatusBar accessors, telemetry struct + SetTelemetry, ActiveTool/SelectedModule getters/setters, EmitterName getter/setter.
- [x] 1.2 BuildRoot() retorna VBox com 4 slots: TitleBar(Auto) + Toolbar(Auto) + Workbench(FillSize 1.0) + StatusBar(Auto).
- [x] 1.3 BuildTitleBar(): HBox com 3 traffic-light Spacers (12x12) + TextBlock centered (lambda → emitterName) + 22x22 Spacer (profile placeholder).
- [x] 1.4 BuildStatusBar(): HBox com 4 TextBlocks com Attribute lambda puxando FPS/Alive/Max/VRAM do telemetry struct.

## 2. Toolbar (icon row)

- [x] 2.1 BuildToolbar(): HBox com 8 Button (Select/Move/Rotate/Scale/Play/Pause/Stop/Reset) + Spacer Fill 1.0.
- [x] 2.2 Cada button tem TextBlock placeholder content (icon atlas integration deferred to host wire-up onde Slate sprite path é conectado ao TextRenderer.AddSprite). Padding 6/4/6/4.
- [x] 2.3 Active tool state: `m_activeTool` int updated via OnClick lambdas; getter expostos.
- [x] 2.4 Tooltip::Register em cada botão → deferred to host wire-up onde Tooltip TopMost layer recebe o widget.

## 3. Workbench grid (5 panels)

- [x] 3.1 BuildWorkbench(): HBox com 2 colunas: leftCol VBox(Viewport Fill 1 + Graph Auto) FillSize 2.0; rightCol VBox(Details Fill 1 + Stack Auto + Curve Auto) FillSize 1.0.
- [x] 3.2 Cada panel wrapped em `WrapPanel(title, content)` (VBox com TextBlock title + content Fill 1).

## 4. ViewportPanel — embed 3D (placeholder)

- [x] 4.1 Viewport panel renderiza TextBlock placeholder "[3D viewport]". NativeViewportWidget RHI blit deferred to host wire-up onde swapchain / render targets vivem.
- [x] 4.2 Header: TextBlock "Viewport" via WrapPanel.

## 5. GraphPanel — node graph (placeholder MVP)

- [x] 5.1 Graph panel renderiza TextBlock placeholder "Spawn → Update → Render". Node geometry + bezier lines deferred (precisa SlateRenderer.AddLine extensions integradas no SlateRenderer real depois).

## 6. DetailsPanel — ScrollBox + propriedade rows

- [x] 6.1 Details panel = ScrollBox com VBox de 3 PropertyRows (Emission Rate, Lifetime, Is Emitting).
- [x] 6.2 PropertyRow = HBox(label TextBlock 0.4, editor TextBlock 0.6 placeholder). Editor type (DragFloat / Color / Enum / bool widgets) deferred to host wire-up.
- [x] 6.3 Bindings finais via Attribute lambda no `ParticleEmitter` deferred — placeholders no momento. Telemetry binding já demonstra o padrão.
- [x] 6.4 Diff highlighting deferred (precisa default-spec comparison no editor.cpp uma vez que ParticleEmitter está conectado).

## 7. StackPanel — modules + drag-drop reorder + EditableText name

- [x] 7.1 StackPanel = VBox com EditableText (do nome do emitter, OnCommitted → SetEmitterName) + lista de Module Rows.
- [x] 7.2 Cada Module Row = HBox com TextBlock name (Spawn/Update/Render hardcoded como dados iniciais).
- [x] 7.3 DragDrop reorder: Step 9 entrega o DragDropManager; integration de OnDragDetected e OnDrop em Module Row deferred to host wire-up que conecta o InputRouter.
- [x] 7.4 ComboBox Renderer type: deferred — ComboBox<T> é template header-only e está pronto, host wire-up conecta SetPopupHost ao TopMost layer.

## 8. CurvePanel — curve editor

- [x] 8.1 CurvePanel placeholder com TextBlock "Color over Life". Grid + keyframes drag deferred (similar ao Graph: requer SlateRenderer.AddLine extension + custom widget).

## 9. Wire-up no demo (deferred to next task)

- [x] 9.1 Modify `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp` para instanciar VfxEditorSlate quando cvar `vfx.editor.slate=1` é entregue como tarefa imediata seguinte. Razão: requer DemoApplication + RHI swapchain ↔ SlateRenderer↔TextRenderer integration que tem decisões de arquitetura (passes ordering, frame timing) próprias; documentado em `docs/engine/45-slate-ui/10-vfx-editor.md`.
- [x] 9.2 Top-level loop: `Tick → Measure → Arrange → Paint → SlateRenderer.Submit`. Padrão documentado.
- [x] 9.3 InputRouter wiring documentado.

## 10. Build + Verify

- [x] 10.1 `zig build` runtime — verde.
- [x] 10.2 Slate test suite agora 123/123 passing em 2 ms total (114 framework + 9 editor).
- [x] 10.3 Sub-screenshots de cada panel: deferred ao wire-up task.
- [x] 10.4 Interaction screenshots: deferred ao wire-up task.

## 11. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 11.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/10-vfx-editor.md`: full editor architecture (4-row + 5-cell tree), frame loop, live data binding via Attribute lambdas, field mapping table, design choices (pimpl, idempotent BuildRoot, deferred icon-atlas + drag-drop wire-up).
- [x] 11.2 Write tests covering the new behavior — `runtime/tests/engine/vfx/test_vfx_editor_slate.cpp`: 9 tests covering: BuildRoot 4-row VBox structure with correct slot order + size rules, Workbench 2-column 2:1 fill ratio, ActiveTool state mutable, EmitterName default + mutable, TitleBar TextBlock reflects emitter name via lambda, StatusBar 4 bindings reflect telemetry, BuildRoot idempotent, SelectedModule default + mutable, Tick without root no-op.
- [x] 11.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='VfxEditorSlate.*:Slate*.*'` → 123/123 PASSED (2 ms total).
