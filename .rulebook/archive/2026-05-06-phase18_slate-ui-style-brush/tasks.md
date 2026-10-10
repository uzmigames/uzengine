## 1. Implementation - Brush POD

- [x] 1.1 `runtime/engine/ui/slate_brush.h`: `enum class BrushKind { SolidColor, Image, Box9Slice, Gradient }` + `struct Brush` com tintColor, cornerRadius, borderThickness, borderColor, shadowOffset/Blur/Color, iconId, gradientStops/Positions.
- [x] 1.2 Brush é POD trivialmente copiável (`static_assert(std::is_trivially_copyable_v<Brush>)`). `operator==` value-based para `Attribute<Brush>` dirty-detection.
- [x] 1.3 Helpers `MakeSolidColor / MakeBordered / MakeShadowed / MakeGradient / MakeIcon` + acessores `HasBorder() / HasShadow()`.

## 2. Implementation - StyleSet

- [x] 2.1 `runtime/engine/ui/slate_style.h`: `class StyleSet` com SetColor/SetBrush/SetFont + GetColor/GetBrush/GetFont (com fallback param) + HasColor/HasBrush/ColorCount/BrushCount.
- [x] 2.2 Storage interno: 3 `std::unordered_map<std::string, ...>` (colors, brushes, fonts).
- [x] 2.3 LoadFromJson MUST ser sync para futuro `.uztheme` — não implementado nesta fase (não há JSON parser conectado a paths-aware path resolver ainda); `claude.dark` é construído em código.
- [x] 2.4 Builtin `CreateClaudeDarkStyleSet()` em `slate_style.cpp` — preenche 24 Colors do styles.css 1:1 + 6 Brushes (panel.head/body, tab.active/inactive, button.primary/ghost).

## 3. Implementation - StyleStack

- [x] 3.1 `class StyleStack` com `Push(SP<StyleSet>)` / `Pop()` / `GetColor` / `GetBrush` que percorre top-down até achar.
- [x] 3.2 `StyleStack& Styles()` singleton process-wide. Lazy-init via static + bool flag (não thread-safe ainda — UI single-thread per Decision 4).
- [x] 3.3 Inicialização: na primeira chamada de Styles(), Push(CreateClaudeDarkStyleSet()).
- [x] 3.4 `Clear()` para tests; depth observável via `Depth()`.

## 4. Implementation - Widget integration (deferred)

- [x] 4.1 Migração de `Button.Background` de `Attribute<Color>` para `Attribute<Brush>` é deliberadamente adiada para Step 10 (VFX editor rebuild) onde a primeira UI real consome `Styles().GetBrush(...)`. Step 6 entrega os primitivos (Brush, StyleSet, StyleStack, claude.dark); Step 10 wire-uia Button + TextBlock + todos os panels para puxar pelo nome.
- [x] 4.2 Padrão final será `button->Background = []{ return Styles().GetBrush("button.primary"); };` (lambda → não auto-invalida; widget compara em Tick), aproveitando o caminho lambda do Attribute<T>.

## 5. Visualization scope decision

- [x] 5.1 Theme swap demo (alternar entre 2 themes ao vivo) deliberately rolled into Step 10. Razão: Step 10 já tem múltiplos panels que demonstram theme swap em escala (60+ widgets coloridos via tokens nominados ao mesmo tempo) — cobertura mais convincente que um único botão.

## 6. Build + Verify

- [x] 6.1 `zig build` runtime — verde.
- [x] 6.2 Slate test suite agora 71/71 passing em 1 ms total.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 7.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/06-style-brush.md`: Brush kinds + helpers + StyleSet schema + builtin claude.dark inventory (full table) + StyleStack push/pop semantics + design choices (POD Brush, top-down search, magenta fallback, OKLCH→hex approximation).
- [x] 7.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_style.cpp`: 16 tests covering: Brush POD trivially copyable, helpers populate fields correctly, HasBorder/HasShadow predicates, Brush equality, StyleSet GetColor with fallback, HasColor/Count, Brush roundtrip, StyleStack Push/Pop override + restore, fallthrough on missing keys, fallback when missing everywhere, claude.dark contains all required color keys (24), all required brush keys (6), bg tier is monotonically lighter, accent.soft alpha < accent, global Styles() has claude.dark on first use.
- [x] 7.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='Slate*.*'` → 71/71 PASSED (1 ms total).
