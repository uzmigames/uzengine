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

### Requirement: Widget é heap-only (smart-pointer constructible)

`Widget` MUST ter construtor protected. Subclasses MUST expor factory
`static SP<T> Create(...)`. Construir Widget na stack é compile error.

#### Scenario: Stack-construct fails

Given um TextBlock subclass
When o usuário tenta `TextBlock t;` na stack
Then o compilador reporta erro (constructor protegido)
And `auto t = TextBlock::Create();` retorna `SP<TextBlock>` válido

### Requirement: Demo Step 2 mostra texto pull-attribute

#### Scenario: Demo Step 2 screenshot

Given o demo construído
When `slate_ui_step2.exe --headless --screenshot path.png --frames 8` roda
Then path.png contém texto "t=0.NN s" (timer pull-via-lambda) centralizado
And visualmente legível (Inter via bitmap font)
