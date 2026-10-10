# Slate VFX Editor Rebuild (Spec Delta)

## ADDED Requirements

### Requirement: 4-row top chrome com alturas fixas

`VfxEditorSlate::BuildRoot()` MUST produzir um `VerticalBox` com
exatamente 4 slots na ordem: TitleBar / Toolbar / Workbench / StatusBar.
Os slots TitleBar, Toolbar e StatusBar MUST ser `SizeRule::Auto`; o
slot Workbench MUST ser `SizeRule::Fill` (fillFactor = 1.0).

#### Scenario: BuildRoot() estrutura

Given uma instância nova de VfxEditorSlate
When BuildRoot() é chamado
Then GetRoot() retorna um VerticalBox com SlotCount() == 4
And Slots()[0..3] são respectivamente TitleBar, Toolbar, Workbench, StatusBar
And Slots()[2].sizeRule == SizeRule::Fill (apenas Workbench)

### Requirement: 5-cell workbench layout

O Workbench MUST ser um `HorizontalBox` com 2 colunas. A coluna
esquerda MUST ter fillFactor 2.0 e conter Viewport (Fill 1) + Graph
(Auto). A coluna direita MUST ter fillFactor 1.0 e conter Details
(Fill 1, ScrollBox) + Stack (Auto) + Curve (Auto), respeitando
o full-height do Details panel.

#### Scenario: Workbench HBox 2 columns

Given BuildRoot() rodou
When inspecionamos GetWorkbench()
Then é um HorizontalBox com 2 slots
And Slots()[0].fillFactor == 2.0f
And Slots()[1].fillFactor == 1.0f

### Requirement: Cores 100% via StyleSet

`VfxEditorSlate` MUST não conter literais RGB hard-coded no código.
Todas as cores MUST passar por `Styles().GetColor(name, fallback)`
ou `Styles().GetBrush(name, fallback)`.

#### Scenario: Grep validation

Given source files runtime/engine/vfx/editor/vfx_editor_slate.cpp
When `grep -E "Color\\{[0-9.]+,[0-9.]+,[0-9.]+,[0-9.]+\\}"` é executado
Then 0 matches são encontrados nos call sites de cor
And quaisquer Color literais ficam dentro de helpers/initializers

### Requirement: StatusBar puxa data live do emitter

A StatusBar MUST renderizar 4 TextBlocks com `Attribute<std::string>`
lambda binding que MUST ler `m_impl->telemetry.{fps, aliveCount,
maxCount, vramUsedMb}`. O host MUST atualizar o telemetry struct via
`SetTelemetry(t)`; o próximo Paint MUST refletir o novo valor sem
qualquer rebuild de tree.

#### Scenario: FPS atualiza ao vivo

Given BuildRoot() rodou
When SetTelemetry({fps: 60, aliveCount: 1234, ...}) é chamado
Then o próximo Paint do StatusBar emite TextBlocks com
     "FPS: 60", "Alive: 1234", "Max: 5000", "VRAM: 42.5 MB"
And NÃO requer rebuild da árvore

### Requirement: DetailsPanel diff highlight

A integração de diff highlighting (vertical bar amarela quando
property value != spec default) MUST ser entregue na task seguinte
de wire-up junto com as bindings reais ao `ParticleEmitter`. Step 10
ships a estrutura PropertyRow (HBox label 0.4 + editor 0.6) e
placeholders.

#### Scenario: Step-host wire-up adiciona diff highlight

Given a task seguinte conecta ParticleEmitter
When a row de "Emission Rate" tem value=25.0f e default=10.0f
Then um quad amarelo 3px wide é emitido à esquerda do label

### Requirement: StackPanel drag-drop reordena modules

A integração drag-drop reorder MUST ser entregue na task seguinte
de wire-up. Step 10 entrega o data model (`modules` vector,
`selectedModule` int) e expõe getters/setters; o framework
DragDropManager (Step 9) já existe.

#### Scenario: Step-host wire-up entrega reorder

Given um host conectado ao InputRouter
When usuário arrasta o módulo idx 0 sobre o módulo idx 2
Then `modules` é reordenado conforme a UI Slate emite OnDrop
And o trecho relevante de host código é coberto por test de integração

### Requirement: EditableText do nome commitado salva no emitter

O `EditableText` na Stack header MUST conectar `OnCommitted` ao
`SetEmitterName` do editor. Após Enter ou Blur, o
`emitterName` MUST refletir o valor digitado, e a TitleBar (que
binda via lambda) MUST mostrar o nome novo no próximo Paint.

#### Scenario: Edit + Enter atualiza title

Given EditableText na Stack tem Text="rain.uzfx" e foi focado + Enter
When o EditableText invoca OnCommitted("rain.uzfx")
Then editor.GetEmitterName() == "rain.uzfx"
And o TextBlock central da TitleBar resolve para "rain.uzfx" na próxima leitura

### Requirement: ComboBox Renderer type altera emitter

A integração ComboBox<RendererType> ao `emitter.SetRendererType` MUST
ser entregue na task seguinte de wire-up. Step 10 entrega o
ComboBox<T> template (Step 7) e o slot reservado no Stack panel.

#### Scenario: Step-host wire-up conecta seleção

Given Stack panel com ComboBox<RendererType>
When usuário clica em "Sprite" (idx 1)
Then editor.SetRendererType(Sprite) é invocado
And o próximo paint do Viewport mostra sprites em vez de meshes

### Requirement: Tooltip em todos os botões da Toolbar

`Tooltip().Register(button, factory, 0.5f)` MUST ser chamado para
cada botão da Toolbar pela camada de wire-up que conecta o
TooltipManager (Step 9) ao TopMost layer renderizado. Step 10 entrega
os botões; o registro acontece no host quando ele tem o TopMost layer
já existente.

#### Scenario: Hover sustained mostra tooltip

Given a task seguinte registra tooltips na Toolbar
When pointer hover sobre o botão "Play" sustained 0.6s
Then Tooltip().Active() != nullptr
And o widget materializado contém o texto "Play"

### Requirement: Visual fidelity vs Claude design reference

A screenshot final do editor MUST ser comparável (manualmente OR
via PSNR ≥ 30 dB) à referência
`.sandbox/claude-design/vfx-template/preview.png` na task seguinte
de wire-up. Step 10 entrega a estrutura; a renderização real (cores
exatas, gradientes, fontes Inter) depende do TextRenderer estar
ligado ao SlateRenderer no host runtime.

#### Scenario: Step-host wire-up captura final

Given o wire-up integra VfxEditorSlate ao vfx_viewport_app
When `vfx_viewport_app.exe --cvar vfx.editor.slate=1 --screenshot path.png`
     roda
Then path.png mostra o editor 4-row + 5-cell completo
And manual visual review confirma identidade estrutural com a referência
