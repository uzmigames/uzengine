# Slate UI — Decisions Gate (Spec Delta)

## ADDED Requirements

### Requirement: 6 Decisões Críticas Documentadas Antes do Código

A pasta `runtime/engine/ui/` MUST não conter qualquer arquivo até que
`docs/engine/45-slate-ui/00-decisions.md` esteja committed + revisado e
contenha as 6 decisões: linguagem, memory, data model, threading,
coordinates, text shaping. Cada decisão MUST ter razão + alternativa
rejeitada documentadas.

#### Scenario: CI gate refuses code if decisions doc is incomplete

Given um PR que adiciona arquivos sob `runtime/engine/ui/`
When o CI script `check-slate-decisions.sh` roda
Then se uma das 6 seções está ausente, exit code != 0 e o PR não merge
And se todas as 6 seções têm "Alternativa rejeitada" + razão, exit 0

### Requirement: DPI-Aware From Day One

Decision 5 (coordinates) MUST establecer que toda API de medida na
Slate UI aceita "Slate unit" (= virtual pixel @ 96 DPI), nunca pixel
físico cru. Conversão para pixel físico ocorre apenas no submit ao
renderer.

#### Scenario: API recebe Slate units, renderer faz scale

Given um widget criado com `SetSize(Vec2{200.0f, 100.0f})`
When o monitor está em DPI 200% (scale 2.0)
Then no submit ao renderer, o quad é emitido em (400, 200) pixels físicos
And nenhum widget calcula `* dpiScale` em sua lógica de layout

### Requirement: Pull Default, Push Para Eventos

Decision 3 (data model) MUST especificar que widgets puxam valor via
`Attribute<T>` no tick. Push (callback do widget para o host) MUST estar
limitado a eventos discretos (click, focus change, value committed).

#### Scenario: Attribute aceita literal e lambda

Given um `Attribute<float> opacity`
When `opacity = 0.5f` é atribuído
Then `opacity.Get()` retorna 0.5
And quando `opacity = []{ return GetTime() * 0.1f; }` é atribuído
Then `opacity.Get()` invoca o lambda a cada call

#### Scenario: Push é callback only

Given um Button com `OnClick` callback
When o usuário clica
Then `OnClick(args)` é invocado
And o widget NÃO força um Push de qualquer outro estado para o host
