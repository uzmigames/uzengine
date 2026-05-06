## 1. Implementation - InvalidationFlag + Widget hooks

- [ ] 1.1 `slate_widget.h`: enum class `InvalidationFlag` com bitfield ops + helpers `IsDirty(flag)` / `ClearDirty(flag)` / `Invalidate(flag)`.
- [ ] 1.2 Add `WP<Widget> m_parent` + `Vec2 m_cachedDesiredSize` + `uint8_t m_invalidation` + `std::vector<DrawElement> m_paintCache` membros.
- [ ] 1.3 Container widgets (HBox/VBox/Overlay) settam parent ptr nos children quando `AddSlot` é chamado.
- [ ] 1.4 `Invalidate(Layout)` bubbles up para parent. `Invalidate(Paint)` NÃO bubbles.

## 2. Implementation - Cached Measure + Paint

- [ ] 2.1 Widget::Measure default impl: se `IsDirty(Layout)`, chama `MeasureImpl(maxSize)` (subclass override), salva em m_cachedDesiredSize, ClearDirty(Layout). Senão retorna m_cachedDesiredSize.
- [ ] 2.2 Widget::Paint default impl: se `IsDirty(Paint) || IsDirty(Layout)`, chama `PaintImpl(scratchList)` (subclass override), atualiza m_paintCache, ClearDirty(Paint). Empurra m_paintCache no renderer.
- [ ] 2.3 Refatorar TextBlock / HBox / VBox / Button / Overlay / Spacer para implementar `MeasureImpl` + `PaintImpl` em vez de Measure / Paint direto.

## 3. Implementation - Attribute integration

- [ ] 3.1 `Attribute<T>::BindOwner(Widget* owner, InvalidationFlag onChange = Paint)` salva ptr + flag.
- [ ] 3.2 `operator=(T)` se m_owner setado e novo valor difere do anterior, chama `m_owner->Invalidate(m_invalidationFlag)`.
- [ ] 3.3 Lambda getter NÃO invalida automaticamente (puxa a cada Tick; widget compara com last + invalida se mudou).
- [ ] 3.4 No Tick default, widget pode fazer `if (m_lastTextSnapshot != Text.Get()) { Invalidate(Paint); m_lastTextSnapshot = Text.Get(); }` (responsabilidade do widget).

## 4. Implementation - Demo Step 5 (perf)

- [ ] 4.1 `runtime/demos/slate_ui_step5/main.cpp`: cria 1000 TextBlocks num VBox + horizontal scroll wrapper. Counter top mostra FPS.
- [ ] 4.2 Modo A (`--no-invalidation`): força Invalidate(Layout|Paint) em cada frame em todos os widgets.
- [ ] 4.3 Modo B (default): só invalida quando texto muda (1 widget por frame).
- [ ] 4.4 Comparar FPS A vs B (espera ≥3× speedup).

## 5. Build + Verify (visualização obrigatória)

- [ ] 5.1 Demo verde + 2 screenshots (modo A `.screenshots/slate-step5-no-invalidation.png` mostrando FPS X, modo B `.screenshots/slate-step5-with-invalidation.png` mostrando FPS Y >> X).
- [ ] 5.2 Documentar FPS deltas no commit message.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 6.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/05-invalidation.md`: dirty flags + bubble rules (Layout bubbles, Paint não) + cache pipeline + Attribute hook + perf table do demo.
- [ ] 6.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_invalidation.cpp`: (a) Invalidate(Layout) bubbles; (b) Invalidate(Paint) NÃO bubbles; (c) clean Measure retorna cache; (d) Attribute = newvalue invalida owner; (e) Attribute = mesmoValue NÃO invalida.
- [ ] 6.3 Run tests and confirm they pass — `--gtest_filter='SlateInvalidation.*'` 100%.
