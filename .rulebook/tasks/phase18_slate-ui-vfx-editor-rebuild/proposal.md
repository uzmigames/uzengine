# Proposal: phase18_slate-ui-vfx-editor-rebuild (Step 10 — final)

## Why

Step 10 do plano-master: rebuild do VFX Editor (Niagara-style)
inteiramente em cima do framework Slate UI dos Steps 1-9. O editor
existente (`runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}`)
hard-codeia paint loop manual via TextRenderer + state in struct.
Não tem retained-mode, não tem invalidation, não tem Brush — cada
mudança de design exige editar 10+ arquivos.

Resultado final: visual Claude design (4-row top: TitleBar 28 / Toolbar
36 / Workbench 1fr / StatusBar 22; 5-cell workbench: Viewport / Graph
/ Details full-height / Stack / Curve) construído com `New<>()` DSL
+ StyleSet "claude.dark" + ScrollBox no Details + EditableText para
nome de emitter + ComboBox para Renderer type + drag-drop para
reordenar Modules na Stack.

## What Changes

```cpp
// runtime/engine/vfx/editor/vfx_editor_slate.{h,cpp}
namespace uz::vfx {

class VfxEditorSlate {
public:
    VfxEditorSlate(VfxSystem* system, ParticleEmitter* emitter);
    SP<ui::Widget> BuildRoot();   // monta árvore Slate completa

    void Tick(float dt);
    void Paint(ui::SlateRenderer& r);
    void HandleInput(const ui::PointerEvent& e);

private:
    SP<ui::Widget> BuildTitleBar();      // 28px
    SP<ui::Widget> BuildToolbar();       // 36px (icons: select/move/rotate/scale/etc)
    SP<ui::Widget> BuildWorkbench();     // 1fr (5 panels)
    SP<ui::Widget> BuildStatusBar();     // 22px (FPS / alive / max / vram)

    // Workbench cells:
    SP<ui::Widget> BuildViewportPanel();   // C++ embed of existing 3D viewport
    SP<ui::Widget> BuildGraphPanel();      // node graph (top-left small)
    SP<ui::Widget> BuildDetailsPanel();    // full-height right (ScrollBox)
    SP<ui::Widget> BuildStackPanel();      // emitter stack list (modules)
    SP<ui::Widget> BuildCurvePanel();      // curve editor (bottom)

    VfxSystem*       m_system{};
    ParticleEmitter* m_emitter{};
    int              m_selectedModule{-1};
};

} // namespace uz::vfx
```

A árvore Slate é construída uma vez em BuildRoot() e fica retida.
Mudanças de dado (ex: usuário muda emissionRate via slider) chamam
`Invalidate(Paint)` apenas nos widgets afetados (graças à invalidação
do Step 5).

Visual fiel ao styles.css do Claude design:
- TitleBar: bg-2 → bg-1 gradient horizontal, traffic-lights à esquerda,
  centered title "particle.uzfx", profile button à direita.
- Toolbar: bg-1, ícones (24×24 sprites com tint via Brush.tintColor).
  Active tab usa "tab.active" Brush (3-band alpha gradient).
- Workbench: 5 cells via Grid layout (2 columns: viewport+graph stack
  na esquerda, details+stack+curve na direita; details é full-height).
- StatusBar: bg-2, mostra FPS / Alive / Max / VRAM (data via
  ParticleEmitter::GetTelemetry).

Drag-drop: Modules na Stack são reordenáveis via DragDrop.
EditableText no nome do emitter (top da Stack).
ComboBox no Renderer type (Mesh / Sprite / MeshRenderer).
Tooltip em todos os botões da Toolbar.

VfxEditorOverlay (legacy) permanece intacto durante migração; novo
VfxEditorSlate é flag-on via `--cvar vfx.editor.slate=1` para o
demo `vfx_viewport_app`. Após validação visual (PSNR vs reference
screenshots, manual review), legacy é deprecated num phase19.

## Impact

- Affected specs: `slate-ui-vfx-editor-rebuild` (NEW).
- Affected code:
  - NEW `runtime/engine/vfx/editor/vfx_editor_slate.{h,cpp}` (~1200 LOC).
  - NEW `runtime/engine/vfx/editor/vfx_editor_slate_panels/{viewport,graph,details,stack,curve}_panel.{h,cpp}`.
  - Modify `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp` para
    instanciar VfxEditorSlate quando cvar flag setado.
- Breaking change: NO (legacy editor coexiste).
- User benefit: design Claude completo, retained-mode performante,
  base para todas próximas evoluções (timeline, multi-emitter, etc.).

## Anti-padrões a evitar

```
NÃO FAÇA:
- Reusar paint imperativo do legacy VfxEditorOverlay (defeats Slate).
- Hard-codar colors no VfxEditorSlate (sempre via Styles().GetColor).
- Rebuild a árvore a cada Tick (BuildRoot uma vez, Invalidate seletivo).
- Inline 3D viewport via TextRenderer — usa NativeViewport widget que
  hospeda o 3D pass como subresource.
- File >1500 LOC — divide em painéis.

FAÇA:
- BuildRoot() no construtor, Tick só sincroniza data via Attribute lambdas.
- Cada panel widget em arquivo próprio (~250 LOC cada).
- DragDrop para reorder Modules; EditableText para nome; ComboBox para enum.
- StyleSet "claude.dark" como única fonte de cor.
- Visual snapshot test: screenshot final == referência styles.css PSNR≥40dB.
```

## Dependencies

- `phase18_slate-ui-window-focus-dnd` arquivado.
