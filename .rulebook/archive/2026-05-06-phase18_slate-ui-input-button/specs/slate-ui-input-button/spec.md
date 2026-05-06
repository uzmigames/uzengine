# Slate UI — Input + Button (Spec Delta)

## ADDED Requirements

### Requirement: Hit-test profundo (deepest-first)

`InputRouter::DispatchPointer` MUST percorrer a árvore profundamente
e selecionar o widget MAIS PROFUNDO cuja `geometry.Contains(pos)`
retorna true.

#### Scenario: Child contém ponto, recebe evento antes do parent

Given um VBox com Button child em geometry={50,50,100,40}
When DispatchPointer com pos=(100, 70) acontece
Then Button.OnPointerDown é chamado primeiro
And se Button retorna `Handled`, VBox.OnPointerDown NÃO é chamado

### Requirement: Event bubble se child retorna Unhandled

`InputRouter` MUST permitir que eventos bubble up para ancestrais
quando o widget atingido retorna `EventReply::Unhandled`. O primeiro
ancestral que retornar `Handled` ou `HandledCapture` interrompe a
propagação.

#### Scenario: Bubble para parent

Given child que retorna `Unhandled`
When DispatchPointer atinge o child primeiro
Then o parent recebe o evento depois
And se parent retorna `Handled`, ancestrais NÃO recebem

### Requirement: HandledCapture redireciona pointer

Quando um widget retorna `EventReply::HandledCapture` em
`OnPointerDown`, o `InputRouter` MUST redirecionar todos os
subsequentes `OnPointerMove` e `OnPointerUp` exclusivamente para esse
captor (independentemente da posição do cursor) até que o
`OnPointerUp` libere o capture.

#### Scenario: Capture recebe Move/Up até release

Given um widget A retornou `HandledCapture` em PointerDown
When pointer move para fora do widget A
Then widget A AINDA recebe OnPointerMove
And widget B sob o pointer NÃO recebe OnPointerMove
When PointerUp ocorre
Then widget A recebe OnPointerUp + capture é liberado

### Requirement: Hover diff Enter/Leave

`InputRouter` MUST disparar `OnHoverLeave` no widget previamente
hovered e `OnHoverEnter` no novo widget hovered apenas quando o
target hovered muda. Mesmo target → nenhum hook é chamado.

#### Scenario: Hover transition

Given pointer estava sobre widget A (hover ativo)
When pointer move sobre widget B
Then A.OnHoverLeave() é chamado
And B.OnHoverEnter() é chamado
And se B é o mesmo A, nada é chamado

### Requirement: Button onClick só com Press → Up dentro

`Button::OnClick` MUST ser invocado uma e apenas uma vez quando
ambos `OnPointerDown` e `OnPointerUp` ocorrem dentro do retângulo do
botão. Press fora, drag-out + release fora, e release com pressed
state limpo MUST NOT invocar `OnClick`.

#### Scenario: Press fora + Up dentro NÃO clica

Given Button em rect={0,0,100,50}
When PointerDown em (200, 200) (fora) → PointerMove → PointerUp em (50, 25) (dentro)
Then OnClick NÃO é invocado

#### Scenario: Press dentro + drag fora + Up fora NÃO clica

Given PointerDown dentro do button
When usuário arrasta para fora e solta
Then m_pressed é resetado em OnHoverLeave
And OnClick NÃO é invocado

#### Scenario: Press dentro + Up dentro clica

Given um Button em rect={0,0,100,40} e PointerDown em (50,20)
When PointerUp em (50,20) é despachado
Then OnClick() é invocado exatamente 1×
