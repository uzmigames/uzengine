# Slate UI — Invalidation (Spec Delta)

## ADDED Requirements

### Requirement: Layout invalidation bubbles, Paint não

A invocação de `Invalidate(Layout)` em um widget MUST propagar
recursivamente para todos os ancestrais via `Widget::GetParent()`,
forçando re-Measure no parent. A invocação de `Invalidate(Paint)`
MUST NOT propagar — afeta apenas o widget alvo.

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

A integração de cache de `m_cachedDesiredSize` + `m_paintCache` MUST
ser entregue até o final do Step 10 (VFX editor rebuild), aproveitando
o primeiro caller real (200+ widgets do Niagara stack) para validar o
ganho. Step 5 entrega o pré-requisito (dirty flags + Attribute auto-
inval); Step 10 introduz `MeasureImpl` / `PaintImpl` overrides em todos
os widgets concretos de uma vez.

#### Scenario: Step-10 cached Measure retorna o valor anterior

Given Widget com cache implementado em Step 10 e MeasureImpl override
When Measure é chamado 100× consecutivos sem Invalidate(Layout)
Then MeasureImpl é invocada exatamente 1× (no primeiro)
And os 99 calls subsequentes retornam o `m_cachedDesiredSize`

#### Scenario: Step-10 Paint clean reusa m_paintCache

Given Widget com PaintImpl override populando m_paintCache
When Paint é chamado sem Invalidate(Paint) prévio
Then PaintImpl NÃO é invocada
And os DrawElements em m_paintCache são copiados para o renderer

### Requirement: Attribute<T> set invalida owner SE valor mudou

`Attribute<T>::operator=(T value)` MUST comparar o novo valor com o
anterior via `T::operator==`; quando diferente e há `BindOwner` ativo,
MUST chamar `owner->Invalidate(flag)`. Quando o valor é igual, MUST NOT
invalidar. `Attribute<T>::operator=(std::function<T()>)` MUST NOT
auto-invalidar — o widget compara snapshots manualmente em `Tick`.

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

### Requirement: Step-10 demo prova perf gain (visualização absorvida)

O demo perf-comparison de invalidação MUST ser entregue como modo
do `slate_ui_step10` final OR como sub-modo do `vfx_viewport_app` ao
final do Step 10. Razão: requer DemoApplication + TextRenderer +
screenshot pipeline que se amortiza melhor implementando junto com
o demo final do VFX editor.

#### Scenario: Invalidation reduz custo de Paint em N widgets

Given uma árvore de 200+ widgets representando o Niagara stack do VFX editor
When o demo final mostra modo A (cvar slate.invalidation=0) e modo B (cvar slate.invalidation=1)
Then FPS médio em B é >= 3× FPS médio em A
And o commit message do Step 10 documenta os FPS deltas
