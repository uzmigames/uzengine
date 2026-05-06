## 1. Implementation - Attribute + Widget base

- [ ] 1.1 `runtime/engine/ui/slate_attribute.h`: template `class Attribute<T>` per proposal. Suporta valor literal, lambda getter, atribuição com `=`. `Get()` const.
- [ ] 1.2 `runtime/engine/ui/slate_widget.h`: struct `Geometry` + class `Widget` virtual com `Construct/Tick/Measure/Arrange/Paint`. Construtor protected. `m_geometry` privado com getter.
- [ ] 1.3 `runtime/engine/ui/slate_widget.cpp`: `Arrange(Geometry)` default seta `m_geometry = g` (filhos overridam para propagar a children).

## 2. Implementation - TextBlock primeiro widget

- [ ] 2.1 `runtime/engine/ui/slate_text_block.h`: classe `TextBlock : Widget` com 3 atributos (Text, TextColor, FontSize) + factory `static SP<TextBlock> Create()`.
- [ ] 2.2 `runtime/engine/ui/slate_text_block.cpp`: `Measure(maxSize)` retorna `Vec2{textWidth_approx, fontSize+2}` clamped por maxSize. `Paint(renderer)` emite `DrawElement{kind=Text, rect={geometry.absolutePos, geometry.size}, color=TextColor.Get(), text=Text.Get()}`.

## 3. Implementation - Demo Step 2

- [ ] 3.1 `runtime/demos/slate_ui_step2/main.cpp` baseado em step1. Cria 1 `TextBlock` via factory.
- [ ] 3.2 Per-frame: `text->Tick(dt)` → `text->Measure(screenSize)` → `text->Arrange(Geometry{absolutePos=(centerX, centerY), size=measuredSize, dpi})` → `text->Paint(slateRenderer)` → `slateRenderer.Submit(...)`.
- [ ] 3.3 Suportar Attribute lambda no demo: `text->Text = [time]() { char b[64]; snprintf(b, 64, "t=%.2fs", time); return std::string(b); };` para verificar que o pull funciona — texto atualiza por frame.

## 4. Build + Verify (visualização obrigatória)

- [ ] 4.1 `zig build` runtime — verde.
- [ ] 4.2 Demo .exe construído.
- [ ] 4.3 Screenshot: texto centralizado e atualizando timer. Salvar como `.screenshots/slate-step2.png`.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 5.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/02-widget-base.md`: classe Widget lifecycle (sequence diagram Construct → Tick → Measure → Arrange → Paint) + Attribute<T> patterns + TextBlock anatomia + screenshot demo.
- [ ] 5.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_widget.cpp`: (a) `Attribute<int>{42}.Get() == 42`; (b) lambda attribute is invoked per Get; (c) Widget::Arrange sets m_geometry; (d) TextBlock::Measure returns size proportional to text length × fontSize.
- [ ] 5.3 Run tests and confirm they pass — `--gtest_filter='SlateWidget.*:SlateAttribute.*:SlateTextBlock.*'` 100%.
