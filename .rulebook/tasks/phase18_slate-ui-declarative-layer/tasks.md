## 1. Implementation - WidgetBuilder

- [ ] 1.1 `runtime/engine/ui/slate_declarative.h`: template `WidgetBuilder<W>` com `SP<W> ptr` membro, ctor que faz `make_shared<W>()` + Construct().
- [ ] 1.2 `template <typename W, typename... Args> WidgetBuilder<W> New(Args&&...)` factory.
- [ ] 1.3 Conversões implícitas: `operator SP<W>()`, `operator SP<Widget>()` (const).
- [ ] 1.4 `operator[](std::initializer_list<SP<Widget>>)` que itera + chama `ptr->AddSlot(c)` se W tem AddSlot (concept/requires).

## 2. Implementation - Property setter macro

- [ ] 2.1 `#define UZ_SLATE_PROP(WClass, PropName)` no slate_declarative.h: gera `template <typename V> auto& PropName(V&& v) { ptr->PropName = std::forward<V>(v); return *this; }`.
- [ ] 2.2 Para cada widget existente, declarar PROPS dentro do builder via specialização parcial OU via injection — escolher 1 abordagem; ideal: `template<> class WidgetBuilder<TextBlock> : public WidgetBuilderBase<TextBlock> { UZ_SLATE_PROP(TextBlock, Text); UZ_SLATE_PROP(TextBlock, Color); ... }`.
- [ ] 2.3 Aplicar a TextBlock, HBox, VBox, Overlay, Spacer, Button, ScrollBox, EditableText, ComboBox.

## 3. Implementation - Slot helpers (alignment / size)

- [ ] 3.1 `Slot::HAlign(HAlign)` / `VAlign(VAlign)` / `FillWidth(float)` chainable: `New<HBox>()[{ Slot(text).FillWidth(1.0f), Slot(button).HAlign(Right) }]`.
- [ ] 3.2 `Slot(SP<Widget>)` helper que retorna SlotBuilder; SlotBuilder é convertível para SP<Widget> retornando o mesmo widget mas com slot params anotados num side struct.

## 4. Implementation - Demo Step 8 (rewrite Step 7 in DSL)

- [ ] 4.1 `runtime/demos/slate_ui_step8/main.cpp`: rewrite do form do Step 7 inteiro usando New<>/operator[] DSL.
- [ ] 4.2 Comparar contagens de linha: imperativo Step 7 (~150 linhas) vs DSL Step 8 (espera ≤80 linhas).
- [ ] 4.3 Output visual idêntico ao Step 7 (mesmos screenshots reproduzíveis).

## 5. Build + Verify (visualização obrigatória)

- [ ] 5.1 Demo verde + 1 screenshot (`.screenshots/slate-step8-dsl.png`) idêntico ao step7-form.png.
- [ ] 5.2 Documentar contagem de linhas Step7 vs Step8 no commit message.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 6.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/08-declarative.md`: New<W>() factory + UZ_SLATE_PROP macro + operator[] children + Slot helpers + side-by-side imperativo↔DSL example.
- [ ] 6.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_declarative.cpp`: (a) New<TextBlock>().Text("x") produz SP<TextBlock> com Text=="x"; (b) New<VBox>()[{a,b,c}] tem 3 slots na ordem; (c) Conversão implícita SP<W> e SP<Widget> compila; (d) Slot(w).FillWidth(2.0) propaga FillWidth no slot.
- [ ] 6.3 Run tests and confirm they pass — `--gtest_filter='SlateDeclarative.*'` 100%.
