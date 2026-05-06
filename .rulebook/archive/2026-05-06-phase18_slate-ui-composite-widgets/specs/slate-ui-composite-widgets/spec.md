# Slate UI — Composite Widgets (Spec Delta)

## ADDED Requirements

### Requirement: ScrollBox clipa content via ScissorPush/Pop

`ScrollBox::Paint` MUST emitir `DrawKind::ScissorPush(geometry.ToRect())`
antes de chamar o paint do conteúdo, e `DrawKind::ScissorPop` depois,
garantindo que o conteúdo overflow não vaze além do retângulo do viewport.

#### Scenario: Content maior que viewport não vaza

Given ScrollBox 200x200 com content VBox 200x500
When PaintImpl é executado
Then primeiro DrawElement é ScissorPush(geometry)
And último DrawElement é ScissorPop
And o renderer não emite triângulos fora do retângulo

### Requirement: ScrollBox wheel ajusta ScrollY clamped

`ScrollBox::OnPointerWheel(e)` MUST atualizar `ScrollY` por
`-wheelDelta * WheelStep`, clampando o resultado em
`[0, m_maxScroll]`. Quando o offset muda, MUST invalidar o flag
`Layout` para re-arrange.

#### Scenario: Wheel up reduz ScrollY

Given ScrollBox com m_maxScroll=300 e ScrollY=100
When OnPointerWheel com delta=+1 (wheel up = scroll up)
Then ScrollY = clamp(100 - 30, 0, 300) = 70

#### Scenario: Wheel atingindo limite clampa

Given ScrollY=10
When OnPointerWheel com delta=+1
Then ScrollY = 0 (não vai negativo)

### Requirement: EditableText foca no click + claim focus

`EditableText::OnPointerDown` MUST chamar `Focus()` e retornar
`EventReply::Handled`. O caret MUST ser posicionado pelo offset
horizontal do click, com aritmética baseada em `FontSize × 0.55`
(advance ratio aproximada).

#### Scenario: Click foca

Given EditableText sem foco
When OnPointerDown
Then retorna EventReply::HandledFocusable
And InputRouter seta este widget como focused
And próximo Paint mostra caret

#### Scenario: Backspace remove char antes do caret

Given Text="hello", caret=3
When OnKeyDown(Backspace)
Then Text="helo"
And caret=2
And OnTextChanged é invocado

#### Scenario: Ctrl+A seleciona tudo

Given Text="hello", caret=2
When OnKeyDown(A, modifiers=Ctrl)
Then m_selStart=0
And m_selEnd=5
And próximo Paint desenha highlight quad atrás dos chars

#### Scenario: Enter dispara OnCommitted

Given Text="hello"
When OnKeyDown(Enter)
Then OnCommitted("hello") é invocado
And focus permanece (não desfoca por default; widget pode escolher)

#### Scenario: Caret blink via Tick

Given EditableText focused, m_caretBlinkTimer=0
When Tick(0.6) é chamado
Then m_caretBlinkTimer=0.6
And como fmod(0.6, 1.0)=0.6 > 0.5, caret não é desenhado no próximo Paint

### Requirement: ComboBox abre popup no TopMost (host-driven)

`ComboBox<T>::OnPointerDown` MUST alternar `m_open` e MUST notificar
o host registrado em `SetPopupHost` com o widget de popup e o flag
`m_open`. `SelectOption(idx)` MUST atualizar `SelectedIndex`, fechar
o popup, e disparar `OnSelectionChanged(idx)`. O TopMost layer real
do App é entregue até Step 9.

#### Scenario: First click abre popup

Given ComboBox closed, m_open=false
When OnPointerDown
Then m_open=true
And m_popup é instanciado como VBox de Buttons (um por opção)
And App::AddTopMost(m_popup) é chamado

#### Scenario: Click em option seleciona + fecha

Given ComboBox open, popup mostrado
When usuário clica no Button da opção idx=2
Then SelectedIndex = 2
And OnSelectionChanged(2) é invocado
And m_open=false
And App::RemoveTopMost(m_popup) é chamado

#### Scenario: Click fora do popup fecha

Given ComboBox open
When usuário clica fora do retângulo do popup
Then m_open=false
And popup é removido do TopMost
And SelectedIndex NÃO muda

### Requirement: TopMost layer renderiza acima de tudo (Step 9)

O TopMost overlay layer MUST ser entregue até o final de Step 9
(window-focus-dnd). Garante que tooltips, popovers do ComboBox, e
arrastadas de DragDrop sejam desenhadas após o resto da árvore com
z-order maior, ignorando o scissor stack das hierarquias acima.

#### Scenario: TopMost ignora ScissorPush dos parents

Given ComboBox dentro de ScrollBox 200x200
And ComboBox open com popup 200x300 (overflow)
When App pinta o frame
Then popup é desenhado por completo (não clipped pelo ScrollBox)
And popup é desenhado APÓS o resto da árvore (z order respeitado)
