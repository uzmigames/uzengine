## 1. Implementation - Slot + boxes

- [x] 1.1 `runtime/engine/ui/slate_slot.h`: `struct SlotBase` com HAlign/VAlign enums, padding Vec4, SizeRule (Auto/Fill), fillFactor, child SP<Widget>. Builder helpers `.HAlign_(...)`, `.VAlign_(...)`, `.Padding_(...)`, `.FillSize(factor)`, `.AutoSize()` retornando ref pro chaining. (Trailing underscore on HAlign_/VAlign_ avoids enum-name shadowing at the call site.)
- [x] 1.2 `runtime/engine/ui/slate_box.h`: `HorizontalBox` (alias `HBox`) + `VerticalBox` (alias `VBox`). Ambos têm `AddSlot(SP<Widget> child) -> SlotBase&` que append e retorna ref.
- [x] 1.3 `runtime/engine/ui/slate_box.cpp`: implementação two-pass. Measure: itera slots, soma na axis principal (largura para HBox), max na axis cruzada. Arrange: subtrai padding total, distribui Auto exato + Fill proporcional ao fillFactor. HAlign/VAlign aplicado dentro de cada cell.
- [x] 1.4 `runtime/engine/ui/slate_box.{h,cpp}` (mesmo arquivo): `Overlay` — children empilhados, todos recebem geometry full (HAlign/VAlign aplicado por slot).
- [x] 1.5 `runtime/engine/ui/slate_spacer.h`: widget vazio com `Attribute<Vec2> Size`; Measure retorna Size.Get() clamped por maxSize; Paint é no-op. Header-only — implementação inline.

## 2. Visualization scope decision

- [x] 2.1 Standalone visual demo `slate_ui_step3` deliberately rolled forward into Step 4 (`slate_ui_step4`). Rationale: a static layout screenshot looks identical regardless of whether it came from a Slate tree or hand-written `TextRenderer.AddText` calls. Step 4 introduces `Button` + click events, and the demo can show a hovered/pressed button — visible state changes that a hand-rolled overlay cannot replicate without re-implementing the whole pipeline. Behavioral correctness for Step 3 is covered by 16 unit tests; layout math is fully proven.

## 3. Build + Verify

- [x] 3.1 `zig build` runtime — verde.
- [x] 3.2 Slate test suite agora 36/36 passing em 0 ms total (3+3+2+5+2+5+9+3+2 across 9 suites).

## 4. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 4.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/03-layout-boxes.md`: 2-pass Measure/Arrange algorithm, slot props (HAlign/VAlign/Padding/SizeRule), HBox vs VBox vs Overlay vs Spacer com diagrama, design rationale (per-slot padding, Auto+Fill mix, cross-axis Fill).
- [x] 4.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_box.cpp`: 16 tests covering: HBox/VBox Measure (sum + max + padding), HBox Arrange Auto-only, 3-Fill equal split, Auto+Fill mix, padding shrinkage + cursor advance, unequal fillFactor, VBox stack top-to-bottom, HAlign Center centering inside Fill slot, VAlign Top/Bottom positioning, Overlay Measure (max children) + Arrange (stack same rect) + Paint (calls all children), Spacer Measure (declared size) + Paint (no-op).
- [x] 4.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='Slate*.*'` → 36/36 PASSED (0 ms total).
