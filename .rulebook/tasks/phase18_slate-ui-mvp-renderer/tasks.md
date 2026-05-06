## 1. Implementation - Camada 1 SlateRenderer

- [ ] 1.1 Criar `runtime/engine/ui/slate_core.h` com aliases: `using SP<T> = std::shared_ptr<T>; using WP<T> = std::weak_ptr<T>;` + `struct Rect { Vec2 pos, size; }` + `using Color = Vec4`.
- [ ] 1.2 Criar `runtime/engine/ui/slate_renderer.h` com `enum class DrawKind { Quad, Line, Text, Sprite, ScissorPush, ScissorPop }` + `struct DrawElement` POD com todos os campos da proposal.
- [ ] 1.3 `class SlateRenderer { void BeginFrame(); void PushElement(const DrawElement&); void Submit(TextRenderer&, const BitmapFont&, const MsdfFont*, float dpiScale); void Clear(); }`.
- [ ] 1.4 Criar `runtime/engine/ui/slate_renderer.cpp` com `Submit` que itera elementos: para `Quad` chama `text.AddRoundedRect(rect.pos.x*dpi, ..., e.cornerRadius, e.color, e.borderThickness, e.borderColor, e.shadowOffset, e.shadowBlur, e.shadowColor)`. Para `Line` chama `AddRect` com height=1px. Para `Text` chama `AddText`. Para `Sprite` chama `AddSprite`. Para `ScissorPush/Pop` usa stack interno (clip TODO Step 6).
- [ ] 1.5 Renderer scissor stack: `std::vector<Rect> m_scissorStack`. ScissorPush empurra; ScissorPop pop. Submit ignora elementos fora do top-of-stack rect.
- [ ] 1.6 Adicionar `engine/ui` ao build via `addCppDir(runtime_mod, b, "engine/ui", cpp_flags)` em `build.zig`.

## 2. Implementation - Demo Step 1

- [ ] 2.1 Criar `runtime/demos/slate_ui_step1/main.cpp` + `build.zig` + `build.zig.zon` baseados em `runtime/demos/triangle/`.
- [ ] 2.2 Demo cria `Application`, abre janela 1280x720, instancia `TextRenderer` + `BitmapFont` (mesma init que `vfx_viewport_app`), instancia `SlateRenderer`.
- [ ] 2.3 Per-frame: `slateRenderer.BeginFrame()` → push 3 elementos: (a) Quad amarelo 100x100 em (50,50) com cornerRadius=8; (b) Quad azul 200x80 em (200,50) com border 2px branco; (c) Text "Slate UI Step 1 OK" em (50,200) cor white. → `Submit(textRenderer, font, nullptr, dpiScale=1.0f)` → `textRenderer.UploadAndDraw()`.
- [ ] 2.4 Suportar `--screenshot <path>` reusando o pattern do `Application`.

## 3. Build + Verify (visualização obrigatória)

- [ ] 3.1 `zig build` runtime — build verde.
- [ ] 3.2 `cd runtime/demos/slate_ui_step1 && zig build` — demo .exe gerado.
- [ ] 3.3 Capturar screenshot: `./build/bin/slate_ui_step1.exe --headless --screenshot E:/UzmiEngine/uzengine/.screenshots/slate-step1.png --frames 8`.
- [ ] 3.4 Verificar visualmente: 1 quad amarelo arredondado, 1 quad azul com border branco, 1 texto branco. Se não estiver, debugar antes de marcar [x].

## 4. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 4.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/01-mvp-renderer.md` documentando: tipos `DrawElement` + `SlateRenderer` API + diagrama Submit pipeline + DPI handling + screenshot do demo.
- [ ] 4.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_renderer.cpp`: (a) `PushElement` aumenta count; (b) `Clear` zera; (c) Submit chama TextRenderer.AddRoundedRect com pos*dpi quando dpiScale=2.0; (d) Scissor stack push/pop balance; (e) Elementos fora do scissor são pulados.
- [ ] 4.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='SlateRenderer.*'` 100%.
