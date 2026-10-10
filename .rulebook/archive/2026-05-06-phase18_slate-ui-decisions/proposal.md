# Proposal: phase18_slate-ui-decisions (gate)

## Why

Sistema de UI inspirado no Slate (Unreal) é um projeto grande. Decidir
linguagem, memory model, threading, DPI, text shaping ANTES de qualquer
linha de código. Errar essas decisões cedo = retrabalho de milhares de
linhas. Errar tarde = não tem volta.

Esta task é gate: nenhuma das 9 tasks de build seguintes começa até este
documento de decisões estar congelado + revisado + pushed.

## What Changes

Documento `docs/engine/45-slate-ui/00-decisions.md` que congela 6 decisões
críticas (Section 4 do plano-master):

1. **Linguagem do core** — C++ puro (alinhado com o restante do runtime).
   Bindings (Lua) são camada superior, fora do escopo do framework.
2. **Memory management** — `std::shared_ptr<Widget>` + `std::weak_ptr`
   estilo Slate (`TSharedPtr` / `TWeakPtr`). Sem GC. RAII para tudo
   abaixo de Widget. Lifetime contracts documentados em cada API que
   captura ponteiros.
3. **Modelo de dados** — Pull (atributo reativo estilo `TAttribute<T>`).
   Cada `Attribute<T>` aceita valor literal OU lambda; widget invoca
   o lambda no tick para puxar valor atualizado. Push (binding manual)
   é fallback para casos onde o widget precisa avisar o host.
4. **Threading** — UI single-thread (event/tick/paint). Render emit é
   thread-safe (gera draw lists imutáveis que o renderer consome
   na render thread). Sem mutex no core do widget tree.
5. **Coordenadas** — DPI-aware desde o início. Todas as APIs aceitam
   "Slate units" (= virtual pixels @ 96 DPI). Conversão para pixel
   físico ocorre no compositor / renderer. `kDpiScale` lido da janela.
6. **Text shaping** — FreeType (raster) + HarfBuzz (shape). Fallback
   simplificado para ASCII via bitmap font do TextRenderer existente
   se HarfBuzz não estiver presente. Localizable + RTL desde o dia 1.

Plus tabela "deferred decisions" (acessibilidade, animation, hot-reload
de styles) com data-limite explícita pra revisitar.

## Impact

- Affected specs: `slate-ui-decisions` (NEW).
- Affected code: nenhum (apenas documentação).
- Breaking change: NÃO.
- User benefit: as 9 tasks de build entram com base sólida; sem
  retrabalho mid-flight por decisão errada.

## Anti-padrões a evitar

```
NÃO FAÇA:
- Adiar a decisão de DPI ("a gente vê depois") — RTL e DPI são pesadelo
  de adicionar tarde.
- Misturar push e pull aleatoriamente — escolha pull como default.
- Single-thread por preguiça — single-thread por DESIGN (com motivo
  documentado).
- Aceitar implementação parcial de text shaping ("ASCII por enquanto") —
  documentar fallback EXPLICITAMENTE como fallback.

FAÇA:
- DPI-aware do dia 1.
- Lifetime contracts em cada `Widget*` exposto.
- Pull como default; Push só quando widget precisa NOTIFICAR host.
- Decisões SCATTERED — cada uma com 1 parágrafo de razão + alternativa
  rejeitada.
```

## Dependencies

- Sem prerequisites. Esta é a fase 0 do sistema Slate UI.
- `runtime/editor/font/text_renderer.h` (existente) será o backend de
  Camada 1. AddRoundedRect / AddRect / AddText já funcionais.
