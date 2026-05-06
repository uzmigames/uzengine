## 1. Implementation - Brush POD

- [ ] 1.1 `runtime/engine/ui/slate_brush.h`: `enum class BrushKind { SolidColor, Image, Box9Slice, Gradient }` + `struct Brush` com tintColor, cornerRadius, borderThickness, borderColor, shadowOffset/Blur/Color, iconId, gradientStops/Positions.
- [ ] 1.2 Brush é POD trivialmente copiável (`static_assert(std::is_trivially_copyable_v<Brush>)`).
- [ ] 1.3 `runtime/engine/ui/slate_brush.cpp`: helper `Brush MakeSolidColor(Color, float radius=0)`, `Brush MakeGradient(Color a, Color b, float radius)`, `Brush MakeBox9Slice(VfxUiIcon icon)`.

## 2. Implementation - StyleSet

- [ ] 2.1 `runtime/engine/ui/slate_style.h`: `class StyleSet` com SetColor/SetBrush/SetFont + GetColor/GetBrush/GetFont (com fallback param).
- [ ] 2.2 Storage interno: 3 `std::unordered_map<std::string, ...>` (colors, brushes, fonts).
- [ ] 2.3 `bool LoadFromJson(const std::string& path)` síncrono — parser simples (chave → cor hex / brush props).
- [ ] 2.4 Builtin StyleSet "claude.dark" via `SP<StyleSet> CreateClaudeDarkStyleSet()` em `slate_style.cpp` — preenche Colors do styles.css 1:1 e Brushes (panel.head/body, tab.active, button.primary).

## 3. Implementation - StyleStack

- [ ] 3.1 `class StyleStack` com `Push(SP<StyleSet>)` / `Pop()` / `GetColor` / `GetBrush` que percorre top-down até achar.
- [ ] 3.2 `inline StyleStack& Styles()` singleton process-wide.
- [ ] 3.3 Inicialização: na primeira chamada de Styles(), Push(CreateClaudeDarkStyleSet()).
- [ ] 3.4 `UZ_ASSERT(stack.size() <= 4)` para evitar stack profundo demais.

## 4. Implementation - Widget integration

- [ ] 4.1 `Button.Background` muda de `Attribute<Color>` para `Attribute<Brush>` (default = `Styles().GetBrush("button.primary")`).
- [ ] 4.2 `TextBlock.Color` aceita name lookup: `text->Color = []{ return Styles().GetColor("fg.0"); }`.
- [ ] 4.3 PaintImpl de Button usa Brush — `renderer.AddRoundedRect(...brush.tintColor, brush.cornerRadius, brush.borderColor, brush.borderThickness)`.

## 5. Implementation - Demo Step 6 (theme swap)

- [ ] 5.1 `runtime/demos/slate_ui_step6/main.cpp`: VBox com header (TextBlock + Button "Toggle theme"), 3 botões samples, 1 panel com gradient.
- [ ] 5.2 Click em "Toggle theme": Pop() + Push(secondTheme) (segundo theme = "claude.light" criado inline com bg=fg invertidos).
- [ ] 5.3 Assert visual: cores trocam imediatamente sem rebuild de widget tree.

## 6. Build + Verify (visualização obrigatória)

- [ ] 6.1 Demo verde + 2 screenshots (`.screenshots/slate-step6-dark.png`, `.screenshots/slate-step6-light.png`) mostrando troca de theme ao vivo.
- [ ] 6.2 Confirmar via Read tool que cores do dark batem com styles.css (bg-0=#15171a, accent=oklch(0.72 0.13 235) ≈ #4ba3d6).

## 7. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 7.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/06-style-brush.md`: Brush kinds + StyleSet schema + builtin claude.dark inventory + StyleStack push/pop semantics + theme swap flow.
- [ ] 7.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_style.cpp`: (a) Brush POD trivially copyable; (b) StyleSet GetColor("missing") retorna fallback; (c) StyleStack push override + pop restore; (d) claude.dark contém todas as keys do styles.css (bg.0..bg.4, accent, etc.).
- [ ] 7.3 Run tests and confirm they pass — `--gtest_filter='SlateStyle.*'` 100%.
