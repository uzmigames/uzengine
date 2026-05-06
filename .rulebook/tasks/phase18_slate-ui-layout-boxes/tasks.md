## 1. Implementation - Slot + boxes

- [ ] 1.1 `runtime/engine/ui/slate_slot.h`: `struct SlotBase` com HAlign/VAlign enums, padding Vec4, SizeRule (Auto/Fill), fillFactor, child SP<Widget>. Builder helpers `.HAlign(...)`, `.Padding(...)`, `.FillSize(factor)` retornando ref pro chaining.
- [ ] 1.2 `runtime/engine/ui/slate_box.h`: `HorizontalBox` + `VerticalBox` (template impl shared via `class BoxBase<Axis>` ou duas classes irmãs com helper interno). Ambos têm `AddSlot(SP<Widget> child) -> SlotBase&` que append e retorna ref.
- [ ] 1.3 `runtime/engine/ui/slate_box.cpp`: implementação two-pass. Measure: itera slots, soma na axis principal (largura para HBox), max na axis cruzada. Arrange: subtrai padding total, distribui Auto exato + Fill proporcional ao fillFactor.
- [ ] 1.4 `runtime/engine/ui/slate_overlay.{h,cpp}`: children empilhados, todos recebem geometry full (HAlign/VAlign aplicado por slot).
- [ ] 1.5 `runtime/engine/ui/slate_spacer.{h,cpp}`: widget vazio com Attribute<Vec2> Size; Measure retorna Size.Get(); Paint nada.

## 2. Implementation - Demo Step 3

- [ ] 2.1 `runtime/demos/slate_ui_step3/main.cpp`: cria root = `VerticalBox::Create()`. Adiciona TitleBar TextBlock no top + content HBox no middle + Footer TextBlock no bottom.
- [ ] 2.2 Content HBox tem 3 slots: Sidebar (Auto, padding {8,8,8,8}, TextBlock "Left"), Spacer fill=1, Body (Fill 1.0, TextBlock "Center"), Spacer fill=1, Side2 (Auto, TextBlock "Right").
- [ ] 2.3 Per-frame: root.Measure(screenSize) → root.Arrange(Geometry{0,0,screenSize,dpi}) → root.Paint(slateRenderer).

## 3. Build + Verify (visualização obrigatória)

- [ ] 3.1 `zig build` runtime + demo verde.
- [ ] 3.2 Screenshot `.screenshots/slate-step3.png`. Verificação visual: 3 textos lado a lado horizontalmente, Center expand para preencher, padding visível em Left.
- [ ] 3.3 Resize janela 800→1600 width: Center expand mantém proporção. Capturar screenshot resize.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 4.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/03-layout-boxes.md`: 2-pass layout algorithm com diagrama, slot props (HAlign/VAlign/Padding/SizeRule), HBox vs VBox vs Overlay vs Spacer, screenshots demo.
- [ ] 4.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_box.cpp`: (a) HBox.Measure soma child widths + padding; (b) Arrange com 3 slots Fill distribui igualmente; (c) Mix Auto + Fill: Auto recebe seu exato, Fill divide o resto; (d) Padding subtrai do espaço disponível; (e) Overlay.Measure = max children.
- [ ] 4.3 Run tests and confirm they pass — `--gtest_filter='SlateBox.*:SlateOverlay.*'` 100%.
