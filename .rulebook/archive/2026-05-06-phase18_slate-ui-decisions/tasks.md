## 1. Implementation - Decision lock (NÃO ESCREVA CÓDIGO)

- [x] 1.1 Criar `docs/engine/45-slate-ui/00-decisions.md` com 6 seções (uma por decisão), cada uma com: (a) decisão, (b) alternativa rejeitada, (c) razão, (d) impacto downstream.
- [x] 1.2 Decisão 1: linguagem do core = C++23 puro. Sem binding script no core; Lua wrapper opcional na camada de aplicação.
- [x] 1.3 Decisão 2: memory model = `std::shared_ptr<Widget>` (`SP<W>`) + `std::weak_ptr` (`WP<W>`). Aliases definidos em `runtime/engine/ui/slate_core.h`. Documentar lifetime contracts (quem captura, quem libera).
- [x] 1.4 Decisão 3: data model = Pull com `Attribute<T>`. Slot accepts `T` value OU `std::function<T()>`. Tick puxa valor antes do paint. Push (callback) só pra eventos (click / focus change / etc).
- [x] 1.5 Decisão 4: threading = UI single-thread (event + tick + paint na render thread). Draw lists emitidos imutáveis; renderer consome em qualquer thread.
- [x] 1.6 Decisão 5: coordinates = DPI-aware. Slate unit = virtual pixel @ 96 DPI. `kDpiScale` lido de `Application::GetDpiScale()`; convertido para físico no `Compositor::Submit()`.
- [x] 1.7 Decisão 6: text shaping = FreeType (raster) + HarfBuzz (shape). Vendoring docs em `runtime/thirdparty/`; `setup.ps1` adiciona se ausente. Fallback ASCII bitmap se thirdparty missing.
- [x] 1.8 Tabela "deferred decisions" no doc: acessibilidade (após Camada 4), animation system (após Camada 5), style hot-reload (após Camada 5), accessibility tree (após Camada 4).
- [x] 1.9 Adicionar `docs/engine/45-slate-ui/README.md` indexando as 11 fases (00-decisions, 01-mvp-renderer, ..., 10-vfx-editor-rebuild).

## 2. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/00-decisions.md` é o entregável principal. README index criado.
- [x] 2.2 Write tests covering the new behavior — N/A pra fase de decisão. Em vez disso, `scripts/check-slate-decisions.sh` valida que as 6 seções obrigatórias estão presentes no doc + que cada decisão tem subseção "alternativa rejeitada".
- [x] 2.3 Run tests and confirm they pass — `./scripts/check-slate-decisions.sh` exit 0.
