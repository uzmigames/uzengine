# Proposal: phase18_slate-ui-declarative-layer (Step 8)

## Why

Step 8 do plano-master: "Declarative layer — SNew-style DSL". Construir
UI imperativamente (`auto vbox = std::make_shared<VBox>(); vbox->AddSlot(...)`)
funciona mas vira ruído. Slate usa macro `SNew(Widget)` + chained
property setters + `[ ... ]` brackets para children — gera 50% menos
linhas e estrutura visual da árvore fica óbvia no código.

Adaptamos para C++ moderno: `New<W>()` factory + fluent setters + 
`operator[]` para children + macro de conveniência.

## What Changes

```cpp
namespace uz::ui {

// Factory + fluent helper.
template <typename W>
class WidgetBuilder {
public:
    SP<W> ptr;
    WidgetBuilder() : ptr(std::make_shared<W>()) { ptr->Construct(); }

    // Generic setter for any field assignable.
    template <typename M, typename V>
    WidgetBuilder& Set(M W::* member, V&& value) {
        ptr.get()->*member = std::forward<V>(value);
        return *this;
    }

    // Fluent overloads of operator. for common props (Text, Padding, etc.)
    // expand via macros — see UZ_SLATE_PROP below.

    // Children for container widgets:
    WidgetBuilder& operator[](std::initializer_list<SP<Widget>> children) {
        if constexpr (requires { ptr->AddSlot(SP<Widget>{}); }) {
            for (auto& c : children) ptr->AddSlot(c);
        }
        return *this;
    }
    operator SP<W>() const { return ptr; }
    operator SP<Widget>() const { return ptr; }
};

template <typename W, typename... Args>
WidgetBuilder<W> New(Args&&... args) {
    WidgetBuilder<W> b;
    if constexpr (sizeof...(args) > 0) (void)b;  // optional ctor args
    return b;
}

// Property setter macro: makes Set(.Text, "x") writable as .Text("x").
#define UZ_SLATE_PROP(WClass, Name) \
    template <typename V> auto& Name(V&& v) { ptr->Name = std::forward<V>(v); return *this; }

} // namespace uz::ui
```

Uso final no editor:

```cpp
auto root = New<VBox>()
  .Padding(8.0f)
  [{
      New<TextBlock>().Text("Hello").Color(Styles().GetColor("fg.0")),
      New<HBox>()
        [{
            New<Button>().Background(Styles().GetBrush("button.primary"))
              .OnClick([]{ UZ_LOG(...); }),
            New<Spacer>().Width(8.0f),
            New<Button>().Text("Cancel"),
        }],
      New<Spacer>().Height(16.0f),
      New<EditableText>().Text("default value"),
  }];
```

Via templates + builder, isso gera 1:1 o mesmo código imperativo de
antes — sem overhead, só açúcar sintático.

## Impact

- Affected specs: `slate-ui-declarative-layer` (NEW).
- Affected code: NEW `runtime/engine/ui/slate_declarative.h` (header-only,
  templates). Modify cada Widget para adicionar `UZ_SLATE_PROP` macros
  para suas Attribute<T> públicas (e.g. TextBlock declara
  `UZ_SLATE_PROP(TextBlock, Text)`, `UZ_SLATE_PROP(TextBlock, Color)`).
- Breaking change: NO (DSL é additive; código imperativo continua valendo).
- User benefit: editor montagem fica 50% menor; árvore lógica visível
  no código. Demo Step 8 reescreve Step 7 form usando DSL.

## Anti-padrões a evitar

```
NÃO FAÇA:
- DSL com efeitos colaterais (heap thrashing, registros globais).
- Macro mágica que esconde a tipagem (cada PROP é typed).
- operator[] mutável que não funciona em widgets non-container
  (precisa SFINAE/concept para AddSlot).
- Side-effect order indefinido — DSL é puramente construção.

FAÇA:
- WidgetBuilder retorna by-value (move-friendly).
- operator[] usa initializer_list<SP<Widget>>.
- Macro UZ_SLATE_PROP só lê o membro Attribute<T> e atribui.
- Conversão implícita SP<W> + SP<Widget> via duas operator() conv.
```

## Dependencies

- `phase18_slate-ui-composite-widgets` arquivado.
