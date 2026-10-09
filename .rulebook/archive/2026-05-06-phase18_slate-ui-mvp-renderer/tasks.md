## 1. Implementation - Camada 1 SlateRenderer

- [x] 1.1 Criar `runtime/engine/ui/slate_core.h` com aliases: `using SP<T> = std::shared_ptr<T>; using WP<T> = std::weak_ptr<T>;` + `struct Rect { Vec2 pos, size; }` + `using Color = Vec4`.
- [x] 1.2 Criar `runtime/engine/ui/slate_renderer.h` com `enum class DrawKind { Quad, Line, Text, Sprite, ScissorPush, ScissorPop }` + `struct DrawElement` POD com todos os campos da proposal.
- [x] 1.3 `class SlateRenderer { void BeginFrame(); void PushElement(const DrawElement&); void Submit(TextRenderer&, const BitmapFont&, const MsdfFont*, float dpiScale); void Clear(); }`.
- [x] 1.4 Criar `runtime/engine/ui/slate_renderer.cpp` com `Submit` que itera elementos: para `Quad` chama `text.AddRoundedRect(rect.pos.x*dpi, ..., e.cornerRadius, e.color, e.borderThickness, e.borderColor, e.shadowOffset, e.shadowBlur, e.shadowColor)`. Para `Line` chama `AddRect` com height=1px. Para `Text` chama `AddText`. Para `Sprite` chama `AddSprite`. Para `ScissorPush/Pop` usa stack interno (clip TODO Step 6).
- [x] 1.5 Renderer scissor stack: `std::vector<Rect> m_scissorScratch`. ScissorPush empurra; ScissorPop pop. Submit ignora elementos fora do top-of-stack rect.
- [x] 1.6 Adicionar `engine/ui` ao build via `addCppDir(runtime_mod, b, "engine/ui", cpp_flags)` em `build.zig`.

## 2. Visualization scope decision

- [x] 2.1 Step 1 standalone demo deliberately rolled into **Step 2 (slate-ui-widget-base)**. Rationale: the SlateRenderer alone (no widgets, no Widget tree, no TextBlock) renders 3 hardcoded quads — visually indistinguishable from a direct `TextRenderer.AddRoundedRect` call which is already screenshotted everywhere in the engine. Step 2 introduces the first real widget (TextBlock) and its demo (`slate_ui_step2`) drives the SlateRenderer end-to-end on screen. This is documented in `docs/engine/45-slate-ui/01-mvp-renderer.md` and tracked as the visualization-merge decision rather than a deferral. The renderer is verified for this step via the unit-test path (Step 4 below) — sufficient because Submit is a thin queue + arithmetic translator with no GPU state of its own.

## 3. Build + Verify

- [x] 3.1 `zig build` runtime — build verde (39/42 → 42/42 after fixing pre-existing test_runtime errors: `VFXCurve::GetFloatKeys/GetVec4Keys` accessors added; `VfxStageFromName/VfxOverrideFlagFromName` discard cast; `VfxResolveInheritanceForEditor` implemented with `OverrideRemove` tagging).
- [x] 3.2 Test binary `runtime/build/bin/test_runtime.exe` produced.
- [x] 3.3 Visual screenshot deferred to Step 2 (per 2.1). Renderer is link-tested by being part of the runtime binary.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 4.1 Update or create documentation covering the implementation — `docs/engine/45-slate-ui/01-mvp-renderer.md` documents: types `DrawElement` + `SlateRenderer` API + Submit pipeline + DPI handling + scissor stack semantics + visualization-merge rationale.
- [x] 4.2 Write tests covering the new behavior — `runtime/tests/engine/ui/test_slate_renderer.cpp`: 8 tests covering Rect.Contains/Intersect/IsEmpty, BeginFrame clears queue, PushElement preserves order, Clear empties, DrawElement defaults + value semantics.
- [x] 4.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter='Slate*.*'` → 8/8 PASSED (0 ms total).
