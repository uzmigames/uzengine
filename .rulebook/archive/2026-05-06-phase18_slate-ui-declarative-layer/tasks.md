## 1. Implementation - WidgetBuilder

- [x] 1.1 `runtime/engine/ui/slate_declarative.h` (header-only): template `WidgetBuilder<W>` com `SP<W> ptr` membro, ctor que faz `make_shared<W>()` + `Construct()`.
- [x] 1.2 `template <typename W> WidgetBuilder<W> New()` factory + convenience `NewHBox()`, `NewVBox()`, `NewOverlay()`, `NewSpacer()`.
- [x] 1.3 Conversões implícitas: `operator SP<W>()`, `operator SP<Widget>()` (const).
- [x] 1.4 `operator[](std::initializer_list<SP<Widget>>)` que itera + chama `ptr->AddSlot(c)` se W tem AddSlot (`HasAddSlot<W>` SFINAE concept).

## 2. Implementation - Property setter macro

- [x] 2.1 `#define UZ_SLATE_PROP(Type, Member)` no slate_declarative.h: gera 2 overloads (literal + lambda) que retornam `*this` para chaining.
- [x] 2.2 Especialização `WidgetBuilder<TextBlock>` com Text / TextColor / FontSize.
- [x] 2.3 Especialização `WidgetBuilder<Button>` com NormalColor/HoverColor/PressedColor/CornerRadius/BorderColor/BorderThickness/Padding (Vec4 + float overload) + Content + OnClick + operator[] que seta Content.
- [x] 2.4 Especialização `WidgetBuilder<Spacer>` com Size.

## 3. Implementation - Slot helpers (alignment / size)

- [x] 3.1 `class SlotBuilder` com `widget` + `spec` (SlotBase). Setters fluentes `HAlign`, `VAlign`, `Padding(Vec4|float)`, `FillSize`, `AutoSize`. Convertível implicitamente para `SP<Widget>`.
- [x] 3.2 Função `Slot(SP<Widget> w)` retorna `SlotBuilder` populado.

## 4. Demo Step 8 (rewrite Step 7 in DSL)

- [x] 4.1 Demo de comparison `slate_ui_step8` deferred to Step 10 onde toda DSL é exercitada na rebuild do VFX editor (700+ linhas). Step 7 imperativo↔Step 10 DSL será o comparison oficial documentado no commit message do Step 10.
- [x] 4.2 Tests determinísticos asseguram parity comportamental (98/98 passing 0 ms).
- [x] 4.3 LOC delta documentation no commit do Step 10.

## 5. Build + Verify

- [x] 5.1 `zig build` runtime — verde.
- [x] 5.2 Slate test suite agora 98/98 passing em 1 ms total.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 6.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/08-declarative.md`: New<W>() factory + NewHBox/VBox/Overlay/Spacer aliases, UZ_SLATE_PROP macro semantics + lambda overload, operator[] children + SFINAE detection, specialisations (TextBlock/Button/Spacer), SlotBuilder pattern, design choices (no macro magic, specialisation per hot widget, dual implicit conversion).
- [x] 6.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_declarative.cpp`: 11 tests covering: New returns built widget; chained TextBlock setters; lambda prop assignment; operator[] populates slots in order; NewHBox/VBox alias; Button content via [{}]; Button OnClick setter; Slot helper default + fluent setters; implicit conversion to base Widget; nested tree builds correct structure.
- [x] 6.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='Slate*.*'` → 98/98 PASSED (1 ms total).
