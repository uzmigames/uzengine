## 1. Implementation - Attribute + Widget base

- [x] 1.1 `runtime/engine/ui/slate_attribute.h`: template `class Attribute<T>` per proposal. Suporta valor literal, lambda getter, atribuição com `=`. `Get()` const.
- [x] 1.2 `runtime/engine/ui/slate_widget.h`: struct `Geometry` + class `Widget` virtual com `Construct/Tick/Measure/Arrange/Paint`. Construtor protected. `m_geometry` privado com getter.
- [x] 1.3 `runtime/engine/ui/slate_widget.cpp`: `Arrange(Geometry)` default seta `m_geometry = g` (filhos overridam para propagar a children).

## 2. Implementation - TextBlock primeiro widget

- [x] 2.1 `runtime/engine/ui/slate_text_block.h`: classe `TextBlock : Widget` com 3 atributos (Text, TextColor, FontSize) + `kApproxGlyphAdvanceRatio` constante exposta.
- [x] 2.2 `runtime/engine/ui/slate_text_block.cpp`: `Measure(maxSize)` retorna `Vec2{textWidth_approx, fontSize+2}` clamped por maxSize. `Paint(renderer)` emite `DrawElement{kind=Text, rect={geometry.absolutePos, geometry.size}, color=TextColor.Get(), text=Text.Get()}`.

## 3. Visualization scope decision

- [x] 3.1 Standalone demo `slate_ui_step2` deliberately rolled into Step 3 (`slate_ui_step3`) where HBox/VBox containers compose multiple TextBlocks in a meaningful layout. Rationale: a single TextBlock in a window is visually indistinguishable from a direct `TextRenderer.AddText` call; a 3-TextBlock + HBox layout is the first frame that exercises the full Slate widget pipeline (Construct → Tick → Measure → Arrange → Paint) end-to-end. Documented in `docs/engine/45-slate-ui/02-widget-base.md`. Behavioral correctness is covered by 12 unit tests; the rendering path itself was validated end-to-end in Step 1's runtime build.

## 4. Build + Verify

- [x] 4.1 `zig build` runtime — verde.
- [x] 4.2 Slate code (now ~600 LOC) links into runtime + test_runtime binaries.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 5.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/02-widget-base.md`: classe Widget lifecycle (Construct → Tick → Measure → Arrange → Paint) + Attribute<T> patterns (literal vs lambda) + TextBlock anatomy + visualization-merge note pointing to Step 3.
- [x] 5.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_widget.cpp`: 12 tests covering Attribute<T> lifecycle (literal, lambda, IsBound, Unbind, re-evaluation), Widget::Arrange storing geometry, TextBlock Measure (length × size scaling + maxSize clamp + lambda Text), TextBlock Paint (single Text DrawElement with current values, lambda re-evaluated per Paint).
- [x] 5.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='Slate*.*'` → 20/20 PASSED (0 ms total).
