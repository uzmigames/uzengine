# Proposal: phase18_slate-ui-mvp-renderer (Step 1)

## Why

Antes de qualquer widget existir, precisa de um RENDERER abstrato de UI
que aceite "draw elements" (quads, linhas, texto, polígonos, scissor)
e que delegue para o backend da engine (`TextRenderer`).

Sem essa camada, widgets emitiriam GPU calls direto, acoplando o
framework ao DX12 e impedindo o backend abstraction (Camada 1 do
plano-master).

Step 1 do plano-master: "MVP de renderer — desenhar quad colorido e
texto numa janela".

## What Changes

Camada 1 do framework Slate UI: `runtime/engine/ui/slate_renderer.{h,cpp}`.

Tipos novos:

```cpp
struct DrawElement {
    enum class Kind { Quad, Line, Text, Sprite, Scissor };
    Kind kind;
    Rect rect;          // Slate units
    Vec4 color;
    float cornerRadius{0.0f};
    Vec4 borderColor{};
    float borderThickness{0.0f};
    Vec2 shadowOffset{};
    float shadowBlur{0.0f};
    Vec4 shadowColor{};
    std::string text;          // Kind=Text only
    VfxUiIcon iconId{};        // Kind=Sprite only
};

class SlateRenderer {
public:
    void BeginFrame();
    void PushElement(const DrawElement& e);
    void Submit(TextRenderer& text, const BitmapFont& font,
                const MsdfFont* msdf, float dpiScale);
    void Clear();
private:
    std::vector<DrawElement> m_elements;
};
```

`Submit` é a ponte: percorre elementos, escala por `dpiScale`, chama
`text.AddRoundedRect` / `AddRect` / `AddText` / `AddSprite`.

Suporte a clipping/scissor via `kind=Scissor` que abre uma região; o
push de elementos subsequentes só desenha dentro da região até o
próximo `Scissor` com `rect=full`. (Implementação inicial: track
top-of-stack scissor; clip não emitido ao renderer ainda — TODO marcado
para Step 6 quando o invalidation system precisar disso.)

## Impact

- Affected specs: `slate-ui-mvp-renderer` (NEW).
- Affected code: NEW `runtime/engine/ui/slate_renderer.{h,cpp}` +
  `runtime/engine/ui/slate_core.h` (Vec2/Rect/Color aliases + smart-ptr
  aliases per decisões).
- Breaking change: NÃO. Camada nova, sem modificar nada existente.
- User benefit: foundation para todo widget. Visualizable: demo
  programa que abre uma janela e desenha 3 elementos (quad colorido,
  linha, texto) via SlateRenderer.

## Anti-padrões a evitar

```
NÃO FAÇA:
- DrawElement com ponteiros para texturas owned (use VfxUiIcon enum
  de atlas, não std::shared_ptr<Texture>).
- Submit que faz draw call por elemento — TextRenderer já bateia.
- Acoplar SlateRenderer a DX12.
- Pular o demo de Step 1 ("a gente testa quando tiver Button") — o
  demo é a verificação visual desta camada.

FAÇA:
- DrawElement plain-data (POD). Coloque serializável trivialmente.
- Submit emite chamadas no TextRenderer, deixa ele bater.
- Acompanhar com demo `runtime/demos/slate_ui_step1/main.cpp` que
  abre janela, desenha 3 quads + 1 texto, screenshot.
- DPI-aware: sempre passar `dpiScale` no Submit.
```

## Dependencies

- `phase18_slate-ui-decisions` arquivado (gate task).
- `runtime/editor/font/text_renderer.h` (TextRenderer existente).
