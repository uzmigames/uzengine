# Slate UI — Declarative Layer (Spec Delta)

## ADDED Requirements

### Requirement: New<W>() factory cria SP<W> construído

#### Scenario: New retorna builder com ptr válido

Given `auto b = New<TextBlock>()`
When `SP<TextBlock> tb = b` (conversão implícita)
Then `tb` é não-nullptr
And `tb->Construct()` foi chamado uma vez (durante New)

### Requirement: Property setter chainable retorna builder

#### Scenario: Set chainable

Given `auto tb = New<TextBlock>().Text("hello").Color({1,0,0,1})`
When inspecionamos o widget final
Then `tb->Text.Get() == "hello"`
And `tb->Color.Get() == Color(1,0,0,1)`

### Requirement: operator[] adiciona children em ordem

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

#### Scenario: SP<W> direta

Given `WidgetBuilder<Button> wb = New<Button>()`
When `SP<Button> btn = wb`
Then compila sem warning
And `btn->GetType() == Button`

#### Scenario: SP<Widget> base

Given mesmo wb
When `SP<Widget> w = wb`
Then compila e w aponta para o mesmo objeto

### Requirement: Slot helpers anotam slot params

#### Scenario: FillWidth propaga

Given
```
auto t = New<TextBlock>().Text("x");
auto hbox = New<HBox>()[{ Slot(t).FillWidth(2.0f), New<Spacer>() }];
```
When inspecionamos hbox->m_slots[0].fillWidth
Then é 2.0f
And `hbox->m_slots[0].widget == t`

### Requirement: DSL produz output idêntico ao imperativo

#### Scenario: Step 8 demo == Step 7 demo visualmente

Given Step 7 imperativo screenshot e Step 8 DSL screenshot
When comparados pixel-by-pixel
Then PSNR >= 50 dB (efetivamente idêntico, modulo aliasing)
