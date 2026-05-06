# Proposal: phase18_slate-ui-layout-boxes (Step 3)

## Why

Step 3 do plano-master: "HorizontalBox/VerticalBox com layout de slots —
valida o sistema de layout". Sem container widgets, a UI degenera num
único widget na tela. HBox/VBox são os blocos primitivos que toda UI
declarativa do Slate usa.

Plus: este step valida que o sistema de layout **two-pass**
(Measure → Arrange) funciona. Pegar isso errado = pesadelo nas próximas
9 tasks.

## What Changes

Containers + slot system:

```cpp
namespace uz::ui {

struct SlotBase {
    enum class HAlign { Left, Center, Right, Fill };
    enum class VAlign { Top, Center, Bottom, Fill };
    HAlign hAlign{HAlign::Fill};
    VAlign vAlign{VAlign::Fill};
    Vec4   padding{};         // {left, top, right, bottom}
    enum class SizeRule { Auto, Fill };
    SizeRule sizeRule{SizeRule::Auto};
    float    fillFactor{1.0f}; // valid only if sizeRule == Fill
    SP<Widget> child;
};

class HorizontalBox : public Widget {
public:
    static SP<HorizontalBox> Create();
    SlotBase& AddSlot(SP<Widget> child);

    Vec2 Measure(Vec2 maxSize) override;   // sum widths, max height
    void Arrange(Geometry g) override;     // distribute X via Auto/Fill
    void Paint(SlateRenderer&) override;   // recursively paint children

private:
    std::vector<SlotBase> m_slots;
};

class VerticalBox : public Widget { /* same but Y axis */ };

class Overlay : public Widget {  // children stacked, all at full geometry
    Vec2 Measure(Vec2 maxSize) override;   // max-of children
    void Arrange(Geometry g) override;     // pass-through
};

class Spacer : public Widget {  // fixed-size empty widget
    Attribute<Vec2> Size{};
    Vec2 Measure(Vec2 maxSize) override { return Size.Get(); }
    void Paint(SlateRenderer&) override {}
};

} // namespace uz::ui
```

Layout algorithm (two-pass, MUST be deterministic):

1. **Measure pass**: Each widget reports `Measure(maxSize)` → desired
   size. Containers query children, aggregate.
2. **Arrange pass**: Parent calls `child->Arrange(geometry)`. HBox/VBox
   distribute remaining space among `Fill` slots proportional to
   `fillFactor`. `Auto` slots get exactly their measured size.

Both passes are pure functions of widget state. No layout state stored
between frames (recompute every frame; invalidation comes in Step 5).

## Impact

- Affected specs: `slate-ui-layout-boxes` (NEW).
- Affected code: NEW `runtime/engine/ui/slate_slot.h`, `slate_box.{h,cpp}`,
  `slate_overlay.{h,cpp}`, `slate_spacer.{h,cpp}`.
- Breaking change: NÃO.
- User benefit: árvore de widgets declarativa começa a fazer sentido.
  Visualizable: demo Step 3 cria HBox com 3 TextBlocks, validando
  alignment + padding.

## Anti-padrões a evitar

```
NÃO FAÇA:
- Layout em uma passada só (mistura measure + arrange) — bug magnet.
- Slot accept de raw `Widget*` — sempre `SP<Widget>`.
- Skipar children Paint quando geometry.size = 0 — emit nothing, mas
  delegate Paint pra eles (eles podem virar visíveis no próximo frame).

FAÇA:
- 2-pass strict: Measure first, all done; THEN Arrange + Paint.
- Slot value-type, não pointer (struct copyável).
- HBox aplica padding ANTES de distribuir (subtrair padding do espaço
  total).
```

## Dependencies

- `phase18_slate-ui-widget-base` arquivado.
