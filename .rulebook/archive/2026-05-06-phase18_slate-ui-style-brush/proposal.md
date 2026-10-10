# Proposal: phase18_slate-ui-style-brush (Step 6)

## Why

Step 6 do plano-master: "Style/Brush system — desacoplar visual do
widget". Sem este sistema, cada widget hard-codeia cores/radii/borders.
Trocar tema = touch em 50 arquivos. Slate resolve isso com Brushes
(visual primitives) + StyleSet (named lookup of brushes/colors/fonts).

Bonus: este step também aporta o equivalente do "design tokens" — uma
única `StyleSet` carrega a paleta Claude design (5 bg, oklch accent,
axes, magenta/purple/cyan) e qualquer widget pega via name lookup.

## What Changes

```cpp
namespace uz::ui {

struct Brush {
    enum class Kind { SolidColor, Image, Box9Slice, Gradient };
    Kind  kind{Kind::SolidColor};
    Color tintColor{1,1,1,1};
    float cornerRadius{0.0f};
    float borderThickness{0.0f};
    Color borderColor{};
    Vec2  shadowOffset{};
    float shadowBlur{0.0f};
    Color shadowColor{};
    // Image / Box9Slice / Gradient extra fields:
    VfxUiIcon iconId{VfxUiIcon::None};
    Vec4      gradientStops{};   // 4 stops max
    Vec4      gradientPositions{};
};

class StyleSet {
public:
    void SetColor(const std::string& name, Color c);
    void SetBrush(const std::string& name, Brush b);
    void SetFont(const std::string& name, const BitmapFont* font, float size);

    [[nodiscard]] Color  GetColor(const std::string& name, Color fallback = {}) const;
    [[nodiscard]] Brush  GetBrush(const std::string& name, Brush fallback = {}) const;
    [[nodiscard]] std::pair<const BitmapFont*, float> GetFont(const std::string& name) const;

    // Theme swap: load from JSON / .uztheme.
    bool LoadFromJson(const std::string& path);

private:
    std::unordered_map<std::string, Color> m_colors;
    std::unordered_map<std::string, Brush> m_brushes;
    // ...
};

// Global style stack (allows nested theme overrides).
class StyleStack {
public:
    void Push(SP<StyleSet> set);
    void Pop();
    [[nodiscard]] Color  GetColor(const std::string& name, Color fallback = {}) const;
    [[nodiscard]] Brush  GetBrush(const std::string& name, Brush fallback = {}) const;
};

inline StyleStack& Styles();  // process-wide singleton

} // namespace uz::ui
```

Built-in StyleSet "claude.dark" populated from styles.css 1:1:
- Colors: `bg.0`..`bg.4`, `line.0`..`line.2`, `fg.0`..`fg.3`, `accent`,
  `accent.soft`, `accent.line`, `warn`, `ok`, `err`, `magenta`, `purple`,
  `cyan`, `axis.x`, `axis.y`, `axis.z`.
- Brushes: `panel.head` (gradient bg-2→bg-1, radius 4, border 1px line),
  `panel.body` (bg-1, radius 4, border 1px line), `tab.active` (3-band
  alpha gradient + accent border), `button.primary` (accent-soft fill +
  accent-line border), etc.
- Fonts: `body` (Inter 11.5px), `mono` (JetBrains Mono 11px).

Widgets pegam por nome:
```cpp
button->Background = Styles().GetBrush("button.primary");
// or via Attribute lambda for hot-reload:
button->Background = []{ return Styles().GetBrush("button.primary"); };
```

## Impact

- Affected specs: `slate-ui-style-brush` (NEW).
- Affected code: NEW `runtime/engine/ui/slate_brush.{h,cpp}`,
  `slate_style.{h,cpp}`. Modify Button/TextBlock para aceitar `Attribute<Brush>`.
- Breaking change: NÃO (atributos novos com defaults).
- User benefit: tema swap em runtime (compact/regular/dark/light), demo
  Step 6 alterna entre 2 themes (botão "Toggle theme") ao vivo.

## Anti-padrões a evitar

```
NÃO FAÇA:
- Hardcodar cores em Paint (sempre Brush via StyleSet).
- StyleSet com pointer raw para BitmapFont sem ownership doc.
- LoadFromJson async — síncrono apenas (theme load é raro).
- StyleSet stack profundo demais — máximo 4 levels (override em modal).

FAÇA:
- Brush é POD (serializable trivialmente).
- StyleSet é SP (compartilhável + swap thread-safe via atomic ptr).
- "claude.dark" embed como C++ constexpr na primeira impl, JSON depois.
- Hot-reload de styles via Attribute<Brush> lambda (puxa do Styles() a
  cada frame; mudança de Push/Pop é detectada via comparação).
```

## Dependencies

- `phase18_slate-ui-invalidation` arquivado.
