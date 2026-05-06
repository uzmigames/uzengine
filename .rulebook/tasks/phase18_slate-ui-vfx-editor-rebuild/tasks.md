## 1. Skeleton — VfxEditorSlate class + 4-row chrome

- [ ] 1.1 `runtime/engine/vfx/editor/vfx_editor_slate.{h,cpp}`: class VfxEditorSlate com ctor(VfxSystem*, ParticleEmitter*), BuildRoot(), Tick(dt), Paint(renderer), HandleInput(event).
- [ ] 1.2 BuildRoot() retorna VBox com 4 slots: TitleBar(28px fixed) + Toolbar(36px fixed) + Workbench(fill) + StatusBar(22px fixed).
- [ ] 1.3 BuildTitleBar(): HBox bg=panel.head Brush gradient bg-2→bg-1, traffic-lights (3 círculos 12px), TextBlock centered "particle.uzfx", profile circle 22px à direita.
- [ ] 1.4 BuildStatusBar(): HBox bg=Styles().GetColor("bg.2"), 4 TextBlocks com Attribute lambda puxando FPS/Alive/Max/VRAM do emitter.

## 2. Toolbar (icon row)

- [ ] 2.1 BuildToolbar(): HBox bg=bg.1, padding 8px lateral, 12px between icons.
- [ ] 2.2 Cada icon button = Button com Background=Brush(Box9Slice, iconId=VfxUiIcon::SelectArrow|Move|Rotate|Scale|Play|Pause|Stop|Reset|Capture).
- [ ] 2.3 Active tool state: m_activeTool int → button cuja idx==active usa "tab.active" Brush.
- [ ] 2.4 Tooltip::Register em cada botão com label ("Select", "Move", etc).

## 3. Workbench grid (5 panels)

- [ ] 3.1 BuildWorkbench(): HBox horizontal com 2 colunas: left = VBox(Viewport fill + Graph 200px), right = VBox(Details fill + Stack 200px + Curve 180px).
- [ ] 3.2 Cada panel é Container retornado por método Build*Panel; cada um wrap em "panel.body" Brush + header bar com title + icons.

## 4. ViewportPanel — embed 3D

- [ ] 4.1 `vfx_editor_slate_panels/viewport_panel.{h,cpp}`: NativeViewportWidget que hospeda o 3D render pass (reusa o existing rendering). PaintImpl emite RHI command de blit do viewport texture into widget rect.
- [ ] 4.2 Header: TextBlock "Viewport" + ícones overlay (gizmo toggle / lit/unlit / wireframe).

## 5. GraphPanel — node graph (placeholder MVP)

- [ ] 5.1 `graph_panel.{h,cpp}`: GraphPanel widget desenha 3 nodes (Spawn → Update → Render) com lines bezier. Nodes são quads 120x40 com text inside; lines via SlateRenderer.AddLine.
- [ ] 5.2 Click no node selecciona (m_selectedNode), atualiza border accent.

## 6. DetailsPanel — ScrollBox + propriedade rows

- [ ] 6.1 `details_panel.{h,cpp}`: DetailsPanel = ScrollBox cuja content é VBox de PropertyRow widgets.
- [ ] 6.2 PropertyRow custom: HBox(label TextBlock 40% width, editor 60% width). Editor depende do tipo: float→DragFloat (drag horizontal num quad com value text); Color→ColorButton+Popover; Enum→ComboBox; bool→Checkbox.
- [ ] 6.3 Bindings via Attribute<T> lambda: rows lêem direto do ParticleEmitter (emissionRate, lifetime, spawnVelocity, colorLifeStart, isEmitting, etc).
- [ ] 6.4 Diff highlighting: row cuja value != spec default mostra vertical bar amarela à esquerda (3px wide).

## 7. StackPanel — modules + drag-drop reorder + EditableText name

- [ ] 7.1 `stack_panel.{h,cpp}`: StackPanel = VBox com header (EditableText do nome do emitter) + lista de Modules.
- [ ] 7.2 Cada Module Row = HBox (icon + TextBlock name + spacer + delete X button). Click selecciona; right-click menu (Add Module).
- [ ] 7.3 DragDrop: OnDragDetected em Module Row retorna DragOp{payload=moduleId, typeTag="vfx.module"}; OnDrop em outro Row reordena vector de modules.
- [ ] 7.4 ComboBox no header para Renderer type (Mesh/Sprite/MeshRenderer); OnSelectionChanged trigger emitter.SetRendererType().

## 8. CurvePanel — curve editor

- [ ] 8.1 `curve_panel.{h,cpp}`: CurvePanel desenha grid 8x4 + curva (sequência de Lines via SlateRenderer.AddLine entre keyframes). Drag de keyframe move o ponto.
- [ ] 8.2 Header com ComboBox "Color over Life / Size over Life / etc".

## 9. Wire-up no demo

- [ ] 9.1 Modify `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp`: se `cvar vfx.editor.slate=1`, instancia VfxEditorSlate em vez de VfxEditorOverlay.
- [ ] 9.2 Top-level loop: editor.Tick(dt); editor.Paint(slateRenderer); slateRenderer.Submit().
- [ ] 9.3 InputRouter conecta WM_MOUSEMOVE/etc para editor.HandleInput.

## 10. Build + Verify (visualização obrigatória)

- [ ] 10.1 `vfx_viewport_app.exe --cvar vfx.editor.slate=1` roda verde + screenshot `.screenshots/slate-vfx-editor-final.png`.
- [ ] 10.2 Comparar com `.sandbox/claude-design/vfx-template/preview.png` (referência) — PSNR >= 35 dB OR manual visual approval.
- [ ] 10.3 Sub-screenshots de cada panel: titlebar, toolbar, viewport, graph, details (com row highlighted), stack (com module dragged), curve.
- [ ] 10.4 Screenshot mostrando interactions: EditableText focused with caret, ComboBox aberto, Tooltip em botão da Toolbar.

## 11. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 11.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/10-vfx-editor.md`: full editor architecture (4-row + 5-cell), data binding via Attribute lambda, módulos drag-drop, screenshots de cada panel, tabela "row → emitter field" mapping.
- [ ] 11.2 Write tests covering the new behavior — `runtime/tests/engine/vfx/test_vfx_editor_slate.cpp`: (a) BuildRoot() retorna VBox de 4 slots na ordem correta; (b) StatusBar.FPS row binding atualiza após emitter.SetFps(60); (c) DetailsPanel row destaca vertical bar quando value != default; (d) StackPanel drag-drop reordena vector; (e) ComboBox Renderer type altera emitter.rendererType.
- [ ] 11.3 Run tests and confirm they pass — `--gtest_filter='VfxEditorSlate.*'` 100%.
