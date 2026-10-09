# Slate UI — Widget Base + TextBlock (Spec Delta)

## ADDED Requirements

### Requirement: Widget lifecycle is Construct → Tick → Measure → Arrange → Paint

Cada widget MUST seguir o ciclo: `Construct()` 1×, `Tick(dt)` por frame
(pull data via Attribute), `Measure(maxSize)` retorna desired size,
`Arrange(geometry)` salva geometry final, `Paint(renderer)` emite
DrawElements.

#### Scenario: TextBlock pull via lambda attribute

Given um TextBlock com `Text = []{ return GetTickStr(); }`
When `Tick(dt)` roda 60 vezes
Then o lambda foi invocado >= 60 vezes (uma vez por frame na primeira
implementação ou lazy via Paint — qualquer dos dois é OK)
And o `Paint` emite DrawElement com `text` igual ao último valor pull

### Requirement: Attribute<T> aceita literal OU lambda

`Attribute<T>` MUST aceitar tanto valor literal quanto `std::function<T()>`
e MUST retornar o valor correto via `Get()` em ambos os casos.

#### Scenario: Atribuição com literal

Given `Attribute<int> foo`
When `foo = 42` é atribuído
Then `foo.Get() == 42`
And `foo.Get() == 42` na próxima call (sem getter ativo)

#### Scenario: Atribuição com lambda overrides literal

Given `Attribute<int> foo = 42`
When `foo = []{ return 7; }` é atribuído depois
Then `foo.Get() == 7`
And o lambda é chamado em cada Get

### Requirement: Widget é heap-amigável (smart-pointer constructible)

`Widget` MUST ter construtor protected na classe base. Subclasses
concretas (e.g. `TextBlock`) podem ter ctor público para integração
com `std::make_shared<W>()` (Decision 2). Construção via `make_shared`
ou via factory `New<W>()` (Step 8) MUST retornar `SP<W>` pronto para
uso após `Construct()`.

#### Scenario: TextBlock construído via make_shared

Given a header `slate_text_block.h` incluído
When o usuário escreve `auto t = std::make_shared<TextBlock>();`
Then `t` é um `SP<TextBlock>` não-nulo
And `t->Text.Get()` retorna a string vazia (default literal)

### Requirement: Demo Step 2 mostra texto pull-attribute

A visualização do widget tree completo (Construct → Tick → Measure →
Arrange → Paint) MUST ser entregue até o final de Step 3 — Step 2
sozinho compõe um único TextBlock que é visualmente indistinguível
de uma chamada direta a `TextRenderer.AddText`. Step 3 introduz
HBox/VBox e seu demo mostra múltiplos TextBlocks em layout, exercendo
o pipeline Slate end-to-end.

#### Scenario: Demo Step 3 cobre o pipeline visual de Step 2

Given o demo `slate_ui_step3` construído
When `slate_ui_step3.exe --headless --screenshot path.png --frames 8` roda
Then path.png contém ≥ 2 TextBlocks renderizados pelo SlateRenderer
And cada TextBlock teve sua Attribute<std::string> Text pull-resolvida
    (literal ou lambda) durante o frame
