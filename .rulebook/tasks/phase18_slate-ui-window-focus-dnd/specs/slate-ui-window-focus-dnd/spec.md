# Slate UI — Window + Focus + Tooltip + DragDrop (Spec Delta)

## ADDED Requirements

### Requirement: Window cria HWND válido + swapchain

#### Scenario: Window construct cria HWND visível

Given WindowDesc{title="Test", size={400,300}, modal=false}
When `Window w(desc, root); w.Show();`
Then GetHwnd() != nullptr
And IsWindowVisible(GetHwnd()) == true

### Requirement: Modal Window bloqueia input no parent

#### Scenario: Click em parent enquanto modal aberta

Given parent Window com Button + modal Window aberta
When PointerDown ocorre dentro do retângulo do botão (mas o foco está no modal)
Then o Button.OnClick NÃO é invocado
And o evento é descartado pelo App (gated por IsModalShowing())

### Requirement: FocusManager dispara hooks na transição

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

#### Scenario: Hover < delay não mostra

Given Tooltip::Register(button, factory, delaySec=0.5f)
When pointer entra no button e fica 0.3s
Then nenhum tooltip é adicionado ao TopMost

#### Scenario: Hover > delay materializa

Given mesmo registro
When pointer entra e permanece 0.6s
Then factory() é chamado uma vez
And o resultado é adicionado ao TopMost layer at cursor pos

#### Scenario: Hover leave remove tooltip

Given tooltip mostrado
When pointer sai do button
Then RemoveTopMost(tooltip) é chamado
And m_hoverTimer é reset para 0

### Requirement: DragDrop threshold 4px

#### Scenario: Move < 4px não dispara drag

Given PointerDown em (100,100) e callback OnDragDetected retorna DragOp
When PointerMove para (102, 101) (distance ≈ 2.2px)
Then DragDrop().IsDragging() == false

#### Scenario: Move >= 4px dispara

Given mesmo setup
When PointerMove para (104, 103) (distance ≈ 5.0px)
Then OnDragDetected é invocado
And DragDrop().Begin(op) é chamado
And ghost é adicionado ao TopMost

### Requirement: DragDrop OnDragEnter/Leave durante move

#### Scenario: Cursor entra/sai de drop targets

Given DragDrop ativo, pointer at (100, 100), receiver R em retângulo (200..300, 100..200)
When pointer move para (250, 150) (dentro de R)
Then R.OnDragEnter(op) é invocado
When pointer move para (350, 150) (fora de R)
Then R.OnDragLeave() é invocado

### Requirement: DragDrop drop chama OnDrop

#### Scenario: Drop em receiver válido

Given DragDrop ativo, pointer sobre R
When PointerUp ocorre
Then R.OnDrop(op) é invocado
And ghost é removido do TopMost
And DragDrop().IsDragging() == false
