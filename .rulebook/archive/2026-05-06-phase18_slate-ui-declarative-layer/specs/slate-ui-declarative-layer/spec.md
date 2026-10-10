# Slate UI — Declarative Layer (Spec Delta)

## ADDED Requirements

### Requirement: New<W>() factory cria SP<W> construído

`New<W>()` MUST retornar um `WidgetBuilder<W>` cujo `ptr` é um
`SP<W>` recém-criado via `std::make_shared<W>()` e que MUST ter
chamado `Construct()` exatamente uma vez antes de retornar.

#### Scenario: New retorna builder com ptr válido

Given `auto b = New<TextBlock>()`
When `SP<TextBlock> tb = b` (conversão implícita)
Then `tb` é não-nullptr
And `tb->Construct()` foi chamado uma vez (durante New)

### Requirement: Property setter chainable retorna builder

Os setters gerados pelo macro `UZ_SLATE_PROP(Type, Member)` MUST
retornar `*this` (referência ao builder) e MUST aceitar tanto literal
`Type` quanto `std::function<Type()>` (lambda) para mirroring exato
do `Attribute<T>::operator=`.

#### Scenario: Set chainable

Given `auto tb = New<TextBlock>().Text("hello").FontSize(13.0f)`
When inspecionamos o widget final
Then `tb->Text.Get() == "hello"`
And `tb->FontSize.Get() == 13.0f`

### Requirement: operator[] adiciona children em ordem

`WidgetBuilder<W>::operator[](initializer_list<SP<Widget>>)` MUST
chamar `ptr->AddSlot(child)` para cada elemento da lista, em ordem,
quando `W` tem `AddSlot(SP<Widget>)` (verificado por SFINAE). Para
widgets sem `AddSlot`, MUST ser no-op (compile-time).

#### Scenario: Children em ordem de declaração

Given
```
auto a = New<TextBlock>().Text("a");
auto b = New<TextBlock>().Text("b");
auto c = New<TextBlock>().Text("c");
auto vbox = New<VBox>()[{a, b, c}];
```
When percorremos vbox->m_slots
Then slot[0].widget == a
And slot[1].widget == b
And slot[2].widget == c

### Requirement: Conversão implícita ambígua-segura

`WidgetBuilder<W>` MUST oferecer ambas conversões implícitas
`operator SP<W>() const` e `operator SP<Widget>() const` sem causar
ambiguidades em uso normal (atribuições e elementos de
`initializer_list<SP<Widget>>`).

#### Scenario: SP<W> direta

Given `auto wb = New<Button>()`
When `SP<Button> btn = wb`
Then compila sem warning
And `btn` aponta para o mesmo objeto que `wb.ptr`

#### Scenario: SP<Widget> base

Given mesmo wb
When `SP<Widget> w = wb`
Then compila e w aponta para o mesmo objeto

### Requirement: Slot helpers anotam slot params

A função `Slot(SP<Widget>)` MUST retornar um `SlotBuilder` que carrega
um `SlotBase` editável via setters fluentes (`HAlign`, `VAlign`,
`Padding`, `FillSize`, `AutoSize`). O `SlotBuilder` MUST ser
implicitamente convertível para `SP<Widget>` e MUST preservar a
descrição do slot para containers descobrirem.

#### Scenario: FillSize propaga via SlotBuilder

Given
```
auto t = New<TextBlock>().Text("x");
auto sb = Slot(t).FillSize(2.0f).HAlign(HAlign::Right);
```
When inspecionamos sb.spec
Then sb.spec.sizeRule == SizeRule::Fill
And sb.spec.fillFactor == 2.0f
And sb.spec.hAlign == HAlign::Right
And sb.widget == static_cast<SP<Widget>>(t)

### Requirement: DSL produz output idêntico ao imperativo

A árvore construída via DSL MUST ter a mesma estrutura, mesmos
slot widgets, e mesma ordem que a equivalente imperativa
(`std::make_shared<W>()` + `AddSlot` chamados manualmente). O
SlateRenderer não pode distinguir as duas árvores.

#### Scenario: Step-10 demo verifica visual parity

Given a árvore equivalente do Step 10 escrita em ambas formas
When ambas são submetidas ao SlateRenderer com a mesma geometria
Then os DrawElements emitidos são idênticos (em ordem e em campo)
And no commit message do Step 10 a comparação LOC imperativo↔DSL
    está documentada
