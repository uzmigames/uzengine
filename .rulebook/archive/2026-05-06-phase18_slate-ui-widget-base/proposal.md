# Proposal: phase18_slate-ui-widget-base (Step 2)

## Why

Step 2 do plano-master: "Widget base + TextBlock estático — ciclo Paint
funcional". Sem a classe base `Widget`, não dá pra ter árvore retida
nem layout nem invalidation. `TextBlock` é o widget mais simples
possível e valida o ciclo de vida completo (Construct → Tick → Paint).

## What Changes

Camada 2 do framework (Core de Widgets):

```cpp
namespace uz::ui {

struct Geometry {
    Vec2 absolutePos;     // Position in screen Slate units
    Vec2 size;            // Allocated size
    float dpiScale{1.0f};
};

class Widget {
public:
    virtual ~Widget() = default;

    // Lifecycle (called by parent / app):
    virtual void Construct() {}                       // Once after ctor
    virtual void Tick(float dt) {}                    // Per-frame data pull
    virtual Vec2 Measure(Vec2 maxSize) = 0;           // Layout pass 1
    virtual void Arrange(Geometry geometry);          // Layout pass 2
    virtual void Paint(SlateRenderer& renderer) = 0;  // Emit DrawElements

    // Geometry accessors (set by Arrange).
    [[nodiscard]] const Geometry& GetGeometry() const { return m_geometry; }

protected:
    Geometry m_geometry{};
};

class TextBlock : public Widget {
public:
    Attribute<std::string> Text;          // Pull-style data binding
    Attribute<Color>       TextColor{Vec4{1,1,1,1}};
    Attribute<float>       FontSize{11.5f};

    Vec2 Measure(Vec2 maxSize) override;  // Approximate text width
    void Paint(SlateRenderer&) override;
};

} // namespace uz::ui
```

`Attribute<T>` (Camada 0 helper):

```cpp
template <typename T>
class Attribute {
public:
    Attribute() = default;
    Attribute(T value) : m_value(std::move(value)) {}
    Attribute(std::function<T()> getter) : m_getter(std::move(getter)) {}

    Attribute& operator=(T value) { m_value = std::move(value); m_getter = nullptr; return *this; }
    Attribute& operator=(std::function<T()> g) { m_getter = std::move(g); return *this; }

    [[nodiscard]] T Get() const { return m_getter ? m_getter() : m_value; }

private:
    T m_value{};
    std::function<T()> m_getter;
};
```

## Impact

- Affected specs: `slate-ui-widget-base` (NEW).
- Affected code: NEW `runtime/engine/ui/slate_widget.h`, `slate_widget.cpp`,
  `slate_attribute.h`, `slate_text_block.{h,cpp}`.
- Breaking change: NÃO.
- User benefit: foundation pra todos os widgets das próximas tasks.
  Visualizable: demo Step 2 renderiza um TextBlock no centro, valida
  ciclo Measure → Arrange → Paint.

## Anti-padrões a evitar

```
NÃO FAÇA:
- Widget com state mutável dentro de Paint (Paint deve ser const-ish).
- Misturar Tick (puxa data) com Paint (emite geometry).
- TextBlock que chama TextRenderer direto — sempre via SlateRenderer.

FAÇA:
- Pull via Attribute<T> em Tick (não em Paint).
- Geometry resolvida em Arrange, lida em Paint.
- Widget é heap-only (use SP<Widget>) — construtor protected, factory
  `static SP<TextBlock> Create(...)`.
```

## Dependencies

- `phase18_slate-ui-mvp-renderer` arquivado (SlateRenderer existe).
- `phase18_slate-ui-decisions` arquivado (memory model + pull pattern lockados).
