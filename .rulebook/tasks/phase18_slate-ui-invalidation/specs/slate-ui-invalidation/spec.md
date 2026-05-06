# Slate UI — Invalidation (Spec Delta)

## ADDED Requirements

### Requirement: Layout invalidation bubbles, Paint não

#### Scenario: Child Layout invalidation atinge root

Given um VBox contendo HBox contendo TextBlock
When TextBlock.Invalidate(Layout) é chamado
Then HBox.IsDirty(Layout) == true
And VBox.IsDirty(Layout) == true

#### Scenario: Child Paint invalidation NÃO atinge parent

Given mesma árvore
When TextBlock.Invalidate(Paint) é chamado
Then TextBlock.IsDirty(Paint) == true
And HBox.IsDirty(Paint) == false
And VBox.IsDirty(Paint) == false

### Requirement: Measure / Paint usam cache se clean

#### Scenario: Repeated Measure sem invalidação retorna cache

Given Widget.MeasureImpl override custosa (e.g. counter)
When Measure(maxSize) é chamado 100 vezes seguidas sem Invalidate(Layout)
Then MeasureImpl é invocada exatamente 1×
And m_cachedDesiredSize é o valor consistente

#### Scenario: Paint clean reusa m_paintCache

Given um Widget com m_paintCache preenchido (Paint chamado antes)
When Paint(renderer) é chamado sem Invalidate
Then PaintImpl NÃO é invocada
And os DrawElements em m_paintCache são pushed ao renderer

### Requirement: Attribute<T> set invalida owner SE valor mudou

#### Scenario: Mesmo valor não invalida

Given Attribute<int> bound to widget owner com flag Paint, valor atual = 42
When `attr = 42` é atribuído (mesmo valor)
Then owner.IsDirty(Paint) == false (não foi setado)

#### Scenario: Valor diferente invalida

Given Attribute<int> bound, valor 42
When `attr = 43`
Then owner.IsDirty(Paint) == true

#### Scenario: Lambda atribuição NÃO invalida automaticamente

Given Attribute<int> bound
When `attr = []{ return 7; }` é atribuído
Then owner.IsDirty(Paint) == false (lambda é puxado a cada Get; widget
     decide se invalida via comparação manual no Tick)

### Requirement: Step 5 demo prova perf gain

#### Scenario: Invalidation reduz custo de Paint em 1000 widgets

Given demo Step 5 com 1000 TextBlocks
When mode A (--no-invalidation) roda 5 segundos
And mode B (default) roda 5 segundos
Then FPS médio em B é >= 3× FPS médio em A
And no commit message a comparação está documentada
