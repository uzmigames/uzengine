# Slate UI — Style + Brush (Spec Delta)

## ADDED Requirements

### Requirement: Brush é POD copiável

`Brush` MUST satisfy `std::is_trivially_copyable_v<Brush> == true`.

#### Scenario: Brush copy não aloca

Given um Brush b com kind=SolidColor, tintColor=red, cornerRadius=4
When `Brush b2 = b` é executado
Then b2 é cópia independente
And nenhuma alocação heap acontece (memcpy direto)

### Requirement: StyleSet name lookup com fallback

#### Scenario: GetColor existente retorna valor

Given StyleSet com SetColor("accent", oklch_blue)
When GetColor("accent") é chamado
Then retorna oklch_blue

#### Scenario: GetColor missing retorna fallback

Given StyleSet sem chave "missing"
When GetColor("missing", fallback={1,0,1,1}) é chamado
Then retorna fallback magenta (sinaliza erro visual mas não crasha)

### Requirement: claude.dark builtin é completo

`CreateClaudeDarkStyleSet()` MUST contain at minimum:

- Colors: `bg.0`, `bg.1`, `bg.2`, `bg.3`, `bg.4`, `line.0`, `line.1`, `line.2`,
  `fg.0`, `fg.1`, `fg.2`, `fg.3`, `accent`, `accent.soft`, `accent.line`,
  `warn`, `ok`, `err`, `magenta`, `purple`, `cyan`, `axis.x`, `axis.y`, `axis.z`.
- Brushes: `panel.head`, `panel.body`, `tab.active`, `tab.inactive`,
  `button.primary`, `button.ghost`.

#### Scenario: Todas keys obrigatórias presentes

Given StyleSet ss = CreateClaudeDarkStyleSet()
When verificamos cada chave acima
Then ss.GetColor(key, magenta) NÃO retorna magenta para nenhuma das 24 keys
And ss.GetBrush(key, {}) tem kind != SolidColor com cornerRadius==0 default vazio para 6 brush keys

#### Scenario: Cores batem styles.css

Given styles.css define `--bg-0: oklch(0.18 0.005 240)` ≈ `#15171a`
When GetColor("bg.0") é chamado
Then componentes RGB ficam dentro de ±0.01 do valor de referência

### Requirement: StyleStack push/pop respects ordering

#### Scenario: Push override

Given Styles() inicial com claude.dark (accent = blue)
And ss2 = StyleSet vazio + SetColor("accent", red)
When Styles().Push(ss2)
Then Styles().GetColor("accent") == red

#### Scenario: Pop restaura anterior

Given após push acima
When Styles().Pop()
Then Styles().GetColor("accent") == blue (volta ao claude.dark)

#### Scenario: Stack lookup top-down

Given ss2 setando "accent" mas NÃO "fg.0"
When Push(ss2) e Get("fg.0")
Then retorna fg.0 do claude.dark (queda para baixo)

### Requirement: Demo Step 6 troca theme ao vivo

#### Scenario: Toggle theme button

Given demo Step 6 com VBox de samples
When usuário clica em "Toggle theme"
Then Styles() é Pop()/Push() para light theme
And próximo Paint mostra cores invertidas
And NÃO há rebuild de widget tree (mesmas instâncias de Button/TextBlock)
