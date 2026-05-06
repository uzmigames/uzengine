# Slate UI — MVP Renderer (Spec Delta)

## ADDED Requirements

### Requirement: SlateRenderer é o único bridge para o backend GPU

Toda emissão de geometry da Slate UI MUST passar por `SlateRenderer::PushElement`.
Widgets MUST NOT chamar `TextRenderer` direto. SlateRenderer.Submit é o
único lugar que conhece o backend.

#### Scenario: Submit converte DrawElement em chamadas de TextRenderer

Given um `DrawElement{kind=Quad, rect={10, 20, 100, 80}, cornerRadius=4, color=red}`
And dpiScale = 1.0
When `SlateRenderer::Submit` é chamado
Then `TextRenderer::AddRoundedRect(10, 20, 100, 80, 4, red, 0, 0,0,0,0, 0, 0,0,0,0)` é invocado exatamente uma vez

#### Scenario: dpiScale escala TODAS as coordenadas no submit

Given um `DrawElement{kind=Quad, rect={10, 20, 100, 80}}`
And dpiScale = 2.0
When Submit é chamado
Then a chamada ao TextRenderer recebe (20, 40, 200, 160) — todas multiplicadas por 2

### Requirement: Scissor stack mantém balanceamento

`SlateRenderer` MUST manter `m_scissorStack` consistente. Para cada
`ScissorPush`, exatamente um `ScissorPop` deve seguir antes do Submit.

#### Scenario: ScissorPop sem Push não quebra

Given um SlateRenderer vazio
When `PushElement(ScissorPop)` é chamado e Submit roda
Then nenhum crash; o Pop é silenciosamente ignorado

#### Scenario: Elemento fora do scissor é pulado no submit

Given ScissorPush rect={0,0,100,100} → Quad em rect={200,200,50,50} → ScissorPop
When Submit roda
Then nenhuma chamada a TextRenderer ocorre para o Quad (fora do scissor)
And o Pop fecha o scissor corretamente

### Requirement: Demo Step 1 visualmente verificável

`runtime/demos/slate_ui_step1/main.cpp` MUST renderizar 3 elementos
distintos (quad rounded, quad com border, texto) num frame estável.

#### Scenario: Screenshot do demo bate com baseline

Given o demo construído
When `slate_ui_step1.exe --headless --screenshot path.png --frames 8` roda
Then path.png existe e tem >= 3 KB
And visualmente contém: quad amarelo arredondado @ (50,50), quad azul com border branco @ (200,50), texto "Slate UI Step 1 OK" em y=200
