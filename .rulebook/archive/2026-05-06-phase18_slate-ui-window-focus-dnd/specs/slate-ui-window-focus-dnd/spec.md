# Slate UI — Window + Focus + Tooltip + DragDrop (Spec Delta)

## ADDED Requirements

### Requirement: Window cria HWND válido + swapchain (deferred to Step 10)

A classe Window dedicada (HWND + DXGI swapchain wrap) MUST ser
entregue até o final de Step 10 quando o modal "Save As?" do VFX
editor é construído. Step 9 entrega a infraestrutura window-agnostic
(Focus / Tooltip / DragDrop) que funciona dentro do escopo
single-window de hoje (`DemoApplication`).

#### Scenario: Step-10 modal usa Window class

Given Step 10 entrega VFX editor com confirm-discard modal
When o usuário fecha com unsaved changes
Then um Window separado é criado com WS_EX_TOPMOST
And input do parent é gated enquanto o modal está visível

### Requirement: Modal Window bloqueia input no parent (deferred to Step 10)

A modal Window do VFX editor MUST gatear o input do parent enquanto
está visível. Step 10 implementa o input-gating junto com a Window
class quando o modal "Save As?" é construído.

#### Scenario: Click em parent enquanto modal aberta

Given parent Window com Button + modal Window aberta no Step 10
When PointerDown ocorre dentro do retângulo do botão (mas o foco está no modal)
Then o Button.OnClick NÃO é invocado
And o evento é descartado pelo App (gated por IsModalShowing())

### Requirement: FocusManager dispara hooks na transição

`FocusManager::SetFocus(new)` MUST chamar `OnFocusLost` no widget
anteriormente focado (se houver) ANTES de `OnFocusReceived` no novo
widget. Definir o mesmo target já focado MUST ser no-op.

#### Scenario: SetFocus de A para B

Given Focus() com m_focused = A
When `Focus().SetFocus(B)`
Then A.OnFocusLost() é invocado primeiro
And B.OnFocusReceived() é invocado depois
And Focus().GetFocus() == B

#### Scenario: CycleNext pula non-focusable

Given root com VBox[ TextBlock(non-focusable), EditableText E1, TextBlock(non-focusable), EditableText E2 ]
And Focus() = E1
When `Focus().CycleNext()`
Then Focus().GetFocus() == E2 (pulou os TextBlocks)

#### Scenario: CycleNext wrap-around

Given Focus() = E2 (último focusable)
When `Focus().CycleNext()`
Then Focus().GetFocus() == E1 (volta ao primeiro)

### Requirement: Tooltip aparece após delay

`TooltipManager::Update(hovered, pos, dt)` MUST acumular
`m_hoverTimer` apenas enquanto o widget hovered for o mesmo da
chamada anterior. Atingido `delaySec`, MUST chamar a factory
exatamente uma vez e armazenar o resultado em `Active()`. Mudança de
hover MUST resetar o timer e limpar `Active()`.

#### Scenario: Hover < delay não mostra

Given Tooltip().Register(button, factory, delaySec=0.5f)
When pointer entra no button e fica 0.3s
Then Tooltip().Active() == nullptr

#### Scenario: Hover > delay materializa

Given mesmo registro
When pointer entra e permanece 0.6s
Then factory() é chamado uma vez
And Tooltip().Active() != nullptr
And Tooltip().AnchorPos() == cursor pos

#### Scenario: Hover change drops tooltip

Given tooltip mostrado para owner A
When pointer move sobre owner B
Then Tooltip().Active() == nullptr
And m_hoverTimer reset para 0

### Requirement: DragDrop threshold 4px

`DragDropManager` MUST iniciar o estado `dragging` apenas quando o
cursor mover-se ≥ `kDragThreshold` (4 Slate units) do `startPos`
gravado por `BeginDetect`, E o callback `srcOnDragDetected` retornar
um `DragOperation` non-null.

#### Scenario: Move < 4px não dispara drag

Given BeginDetect(startPos=(100,100), source) seguido de Update((102, 101), ...)
When o callback srcOnDragDetected é fornecido
Then DragDrop().IsDragging() == false (distance ≈ 2.2 < 4)

#### Scenario: Move >= 4px dispara

Given mesmo setup
When Update((110, 100), target, callback)
Then o callback é invocado uma vez
And DragDrop().IsDragging() == true
And DragDrop().Active() carrega o DragOperation retornado

### Requirement: DragDrop OnDragEnter/Leave durante move

Enquanto `IsDragging()`, mudanças do `currentTarget` em chamadas
sucessivas a `Update` MUST disparar `OnDragLeave` no target anterior
e `OnDragEnter` no novo target. Mesmo target → nenhum hook.

#### Scenario: Cursor entra/sai de drop targets

Given DragDrop ativo, pointer at (100, 100)
When Update((20, 0), target=A, ...) e depois Update((40, 0), target=B, ...)
Then A.OnDragEnter() é invocado
And A.OnDragLeave() é invocado
And B.OnDragEnter() é invocado

### Requirement: DragDrop drop chama OnDrop

`DragDropManager::EndDrop(pos, target)` MUST chamar `target->OnDrop()`
quando `IsDragging() && target != nullptr`. Em todos os casos MUST
limpar o estado interno (detecting/dragging/op/lastEnter).

#### Scenario: Drop em receiver válido

Given DragDrop ativo, pointer sobre R
When EndDrop(pos, R) é chamado
Then R.OnDrop() é invocado
And DragDrop().IsDragging() == false
And DragDrop().Active() == nullptr

#### Scenario: Cancel não fire OnDrop mas fire OnDragLeave defensivamente

Given DragDrop ativo com lastEnter = T
When DragDrop().Cancel() é chamado
Then T.OnDragLeave() é invocado
And T.OnDrop() NÃO é invocado
And DragDrop().IsDragging() == false
