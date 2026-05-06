# Slate UI — Layout Boxes (Spec Delta)

## ADDED Requirements

### Requirement: Layout em 2 passes Measure → Arrange

Containers MUST implementar `Measure(maxSize) -> Vec2` (sem mutar
estado de filhos) e `Arrange(geometry)` (atribui geometry final aos
filhos). Mistura nas duas passes (e.g. mutação durante Measure)
PROIBIDA.

#### Scenario: Measure não modifica state de children

Given um HBox com 3 TextBlocks
When `Measure(maxSize=(1000,500))` é chamado 100 vezes seguidas
Then nenhuma m_geometry de child é modificada
And o desired-size retornado é determinístico (mesmo valor todas vezes)

### Requirement: HBox com Fill distribui proporcionalmente

#### Scenario: 2 slots Fill 1.0 dividem o espaço extra igualmente

Given um HBox em geometry com width=300, sem padding
And 2 slots: Auto (TextBlock width=50) + Fill(1.0) + Fill(1.0)
When Arrange é chamado
Then slot Auto recebe x=0 width=50
And primeiro Fill recebe x=50 width=125
And segundo Fill recebe x=175 width=125

#### Scenario: FillFactor diferentes proporcionam

Given HBox width=300, 2 slots Fill: factor 1.0 + factor 3.0
When Arrange é chamado
Then primeiro slot recebe width=75 (1/4 do total)
And segundo slot recebe width=225 (3/4 do total)

### Requirement: Padding subtrai do espaço alocado ao child

#### Scenario: Padding não vaza pro child

Given um HBox slot com padding {10, 5, 10, 5} e child=TextBlock
And o slot foi arranged em rect={0,0,100,50}
When Paint roda
Then o child recebe geometry pos=(10,5) size=(80,40)

### Requirement: Demo Step 3 visual obrigatório

#### Scenario: 3 textos em HBox respondem a resize

Given demo Step 3 rodando
When janela é redimensionada de 800 para 1600 px wide
Then o TextBlock "Center" (Fill 1.0) expande para ocupar largura adicional
And os textos "Left" e "Right" mantêm seu tamanho Auto
