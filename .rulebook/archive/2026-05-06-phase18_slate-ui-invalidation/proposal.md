# Proposal: phase18_slate-ui-invalidation (Step 5)

## Why

Step 5 do plano-master: "Invalidation system — só repaint do que
mudou". Recomputar Measure/Arrange/Paint para a árvore inteira a cada
frame é viável em 100 widgets, mas vira gargalo em 10k+ (Details panel
do Niagara tem facilmente 200+ widgets).

Slate UI usa duas formas de invalidation:
- **Layout invalidation**: filhos mudam tamanho → re-Measure + re-Arrange.
- **Paint invalidation**: cor/texto muda mas tamanho não → re-Paint só.

Com cache de Measure + cache de DrawElements, frames sem mudança são
quase free (só Tick + Paint final).

## What Changes

```cpp
namespace uz::ui {

enum class InvalidationFlag : uint8_t {
    None       = 0,
    Layout     = 1 << 0,   // Re-Measure + Re-Arrange + Re-Paint
    Paint      = 1 << 1,   // Re-Paint only (geometry stable)
    Visibility = 1 << 2,   // Toggle visibility
};

class Widget {
    // ...
    void Invalidate(InvalidationFlag flags) {
        m_invalidation = static_cast<uint8_t>(m_invalidation) | static_cast<uint8_t>(flags);
        // Bubble layout invalidation up so parent re-measures.
        if ((flags & InvalidationFlag::Layout) != InvalidationFlag::None) {
            if (auto p = m_parent.lock()) p->Invalidate(InvalidationFlag::Layout);
        }
    }

    // Cached desired size (Measure result). Invalidated on Layout.
    [[nodiscard]] Vec2 GetDesiredSize() const { return m_cachedDesiredSize; }

protected:
    WP<Widget> m_parent;
    Vec2       m_cachedDesiredSize{};
    uint8_t    m_invalidation{static_cast<uint8_t>(InvalidationFlag::Layout) |
                              static_cast<uint8_t>(InvalidationFlag::Paint)}; // Initially dirty
    std::vector<DrawElement> m_paintCache;
};

// Attribute<T> integration: when value changes, mark Paint dirty.
template <typename T>
class Attribute {
    // ... existing methods ...
    void BindOwner(Widget* owner, InvalidationFlag onChange) {
        m_owner = owner;
        m_invalidationFlag = onChange;
    }
private:
    Widget* m_owner{nullptr};
    InvalidationFlag m_invalidationFlag{InvalidationFlag::Paint};
};

class App {
public:
    void RenderFrame() {
        // 1. Tick: pull data via Attribute, may set Invalidation flags.
        m_root->Tick(dt);

        // 2. Measure: only widgets with Layout dirty.
        if (m_root->IsDirty(InvalidationFlag::Layout)) {
            m_root->Measure(screenSize);
            m_root->Arrange({0,0,screenSize, dpi});
        }

        // 3. Paint: only widgets with Paint dirty (or descendants of layout-dirty).
        m_root->Paint(m_renderer);

        // 4. Submit.
        m_renderer.Submit(...);
    }
};

} // namespace uz::ui
```

Paint cache strategy: each widget keeps `m_paintCache` (list of DrawElement
emitted last Paint). On Paint with `Paint=clean && Layout=clean`, just
push the cached vector to renderer (no reallocation). On dirty, re-emit
+ refresh cache.

## Impact

- Affected specs: `slate-ui-invalidation` (NEW).
- Affected code: modify `slate_widget.{h,cpp}`, `slate_attribute.h` to
  hook Invalidate. App-level frame loop updated.
- Breaking change: NO (default flags = always-dirty preserves prior behavior).
- User benefit: framerate stable em UIs grandes. Visualizable: demo
  Step 5 com 1000 TextBlocks; FPS comparison antes/depois (with vs
  without invalidation).

## Anti-padrões a evitar

```
NÃO FAÇA:
- Cache invalidation por dirty bit global ("repinta tudo se alguma coisa
  mudou") — defeats the purpose.
- Layout dirty sem propagar pro parent — child reflow não bate parent.
- Paint cache vazio mas widget visible — widget some.

FAÇA:
- Bubble Layout pro parent (parent precisa re-Measure se child mudou).
- Paint dirty NÃO sobe (parent paint não muda).
- Initial state = dirty (forçar primeira Measure + Paint).
- Attribute<T>::operator= seta Paint dirty no owner; lambda getter NÃO
  invalida (é puxado a cada Tick e comparado pelo widget).
```

## Dependencies

- `phase18_slate-ui-input-button` arquivado.
