# Slate VFX Editor Rebuild (Spec Delta)

## ADDED Requirements

### Requirement: 4-row top chrome com alturas fixas

#### Scenario: BuildRoot() estrutura

Given VfxEditorSlate(system, emitter)
When BuildRoot() é chamado
Then retorna VBox com 4 slots:
  - slot[0].child = TitleBar widget, height = 28px (auto)
  - slot[1].child = Toolbar widget, height = 36px (auto)
  - slot[2].child = Workbench widget, FillHeight=1.0f
  - slot[3].child = StatusBar widget, height = 22px (auto)

### Requirement: 5-cell workbench layout

#### Scenario: Workbench HBox 2 columns

Given Workbench widget
When Arrange é chamado com width=1920, height=900 (excluindo top/status bars)
Then HBox.left = VBox(Viewport fill + Graph 200px), width ≈ 1280
And HBox.right = VBox(Details fill + Stack 200px + Curve 180px), width ≈ 640
And Details panel ocupa toda altura disponível (full-height)

### Requirement: Cores 100% via StyleSet

`VfxEditorSlate` MUST not contain any hard-coded RGB color literal.
All colors MUST be obtained via `Styles().GetColor(name, fallback)`.

#### Scenario: Grep validation

Given source files runtime/engine/vfx/editor/vfx_editor_slate*.cpp
When `grep -E "Color\\{[0-9.]+,[0-9.]+,[0-9.]+,[0-9.]+\\}"` é executado
Then 0 matches são encontrados (todos os Colors via Styles().GetColor)

### Requirement: StatusBar puxa data live do emitter

#### Scenario: FPS atualiza ao vivo

Given StatusBar com Attribute lambda `[]{ return std::to_string(emitter->Fps()); }`
When emitter->Fps() retorna 60
Then próximo Paint mostra "FPS: 60"
When emitter->Fps() retorna 30
Then próximo Paint mostra "FPS: 30"
And NÃO requer rebuild da árvore

### Requirement: DetailsPanel diff highlight

#### Scenario: Row destaca quando value != default

Given PropertyRow para emissionRate, default=10.0f
When emitter->emissionRate = 25.0f
Then PaintImpl da row emite quad amarelo 3px wide à esquerda do label

#### Scenario: Row sem highlight quando value == default

When emitter->emissionRate = 10.0f (default)
Then PaintImpl NÃO emite o quad amarelo

### Requirement: StackPanel drag-drop reordena modules

#### Scenario: Drag module 0 para posição 2

Given Stack com modules ["Spawn", "Update", "Render"], indices 0..2
When drag de Spawn (idx 0) e drop em cima de Render (idx 2)
Then m_emitter->modules order vira ["Update", "Render", "Spawn"]
And StackPanel rebuilda lista de Module Rows na nova ordem

### Requirement: EditableText do nome commitado salva no emitter

#### Scenario: Edit + Enter

Given EditableText.Text = "particle.uzfx"
When usuário digita "explosion.uzfx" e pressiona Enter
Then OnCommitted("explosion.uzfx") é invocado
And m_emitter->SetName("explosion.uzfx") é chamado

### Requirement: ComboBox Renderer type altera emitter

#### Scenario: Switch Mesh → Sprite

Given ComboBox SelectedIndex=0 (Mesh)
When usuário clica em Sprite (idx 1) no popup
Then OnSelectionChanged(1) invoca emitter->SetRendererType(Sprite)
And próximo Paint do Viewport mostra sprite em vez de mesh

### Requirement: Tooltip em todos os botões da Toolbar

#### Scenario: Hover em "Play" mostra tooltip

Given Toolbar tem Button cuja iconId = VfxUiIcon::Play, Tooltip::Register(button, []{ return New<TextBlock>().Text("Play"); }, 0.5f)
When pointer hover sustained 0.6s
Then tooltip "Play" aparece no TopMost layer

### Requirement: Visual fidelity vs Claude design reference

#### Scenario: Final screenshot vs reference

Given `.sandbox/claude-design/vfx-template/preview.png` (referência)
And `.screenshots/slate-vfx-editor-final.png` (gerado pelo demo)
When PSNR é computado entre os dois
Then PSNR >= 35 dB
OR um manual visual review confirma identidade estrutural (4 rows + 5 cells, mesmas cores, mesmo layout)
