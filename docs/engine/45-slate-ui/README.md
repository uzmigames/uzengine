# Slate UI — Phase 18 documentation index

UzEngine's retained-mode UI framework, modeled after Unreal's Slate. This
directory holds the full documentation for the framework, divided across
11 phases (00 — decisions, 01..09 — framework layers, 10 — VFX editor
rebuild on top of the framework).

Each phase is a standalone rulebook task under `.rulebook/tasks/phase18_slate-ui-*`
with `proposal.md`, `tasks.md`, and `specs/<id>/spec.md`. Each task ends
with a mandatory tail (docs + tests + verify).

## Phases

| # | ID | Doc | Summary |
|---|---|---|---|
| 0 | `slate-ui-decisions` | [00-decisions.md](00-decisions.md) | Lock the six load-bearing decisions: language, memory, data binding, threading, coordinates, text shaping. Gate task — no code. |
| 1 | `slate-ui-mvp-renderer` | [01-mvp-renderer.md](01-mvp-renderer.md) | `SlateRenderer` + `DrawElement` enum (Quad / Line / Text / Sprite / Scissor). First demo: solid color quads. |
| 2 | `slate-ui-widget-base` | [02-widget-base.md](02-widget-base.md) | Widget base class, `Attribute<T>` pull binding, `TextBlock`. First text-on-screen demo. |
| 3 | `slate-ui-layout-boxes` | [03-layout-boxes.md](03-layout-boxes.md) | `HBox`, `VBox`, `Overlay`, `Spacer` containers with `SlotBase` (HAlign / VAlign / FillWidth / FillHeight). |
| 4 | `slate-ui-input-button` | [04-input-button.md](04-input-button.md) | `PointerEvent`, `KeyEvent`, `EventReply`, deepest-first `InputRouter`, capture/bubble, `Button` widget. |
| 5 | `slate-ui-invalidation` | [05-invalidation.md](05-invalidation.md) | `InvalidationFlag` (Layout bubbles, Paint doesn't). Cached `Measure` + `Paint`. ≥3× FPS speedup demo. |
| 6 | `slate-ui-style-brush` | [06-style-brush.md](06-style-brush.md) | `Brush` POD + `StyleSet` + `StyleStack` + builtin `claude.dark` from `styles.css`. Live theme swap demo. |
| 7 | `slate-ui-composite-widgets` | [07-composite-widgets.md](07-composite-widgets.md) | `ScrollBox`, `EditableText`, `ComboBox`, `TopMost` overlay layer. |
| 8 | `slate-ui-declarative-layer` | [08-declarative.md](08-declarative.md) | `New<W>().Text("x")[{children}]` DSL via `WidgetBuilder<W>` + `UZ_SLATE_PROP` macro. |
| 9 | `slate-ui-window-focus-dnd` | [09-window-focus-dnd.md](09-window-focus-dnd.md) | `Window` + modal, `FocusManager` (tab cycle), `Tooltip`, `DragDrop`. |
| 10 | `slate-ui-vfx-editor-rebuild` | [10-vfx-editor.md](10-vfx-editor.md) | Final: Niagara-style VFX Editor (4-row + 5-cell) on top of the Slate framework. |

## Reading order

If you are picking up this work cold:

1. Read [00-decisions.md](00-decisions.md) end-to-end — every other phase
   assumes these decisions.
2. Skim each subsequent phase doc top-down. Each one builds on the previous.
3. Phase 10 is the integration target. Every previous phase's API is exercised
   there.

## Source layout

| Path | Purpose |
|---|---|
| `runtime/engine/ui/slate_core.h` | `SP<T>`, `WP<T>`, `Vec2`, `Rect`, `Color`, `Geometry`. |
| `runtime/engine/ui/slate_renderer.{h,cpp}` | `SlateRenderer`, `DrawElement`. |
| `runtime/engine/ui/slate_widget.{h,cpp}` | `Widget`, `Attribute<T>`, `Tick`/`Measure`/`Arrange`/`Paint`. |
| `runtime/engine/ui/slate_layout.{h,cpp}` | `HBox`, `VBox`, `Overlay`, `Spacer`. |
| `runtime/engine/ui/slate_input.{h,cpp}` | `PointerEvent`, `KeyEvent`, `EventReply`, `InputRouter`. |
| `runtime/engine/ui/slate_invalidation.{h,cpp}` | `InvalidationFlag`, cached measure + paint. |
| `runtime/engine/ui/slate_brush.{h,cpp}`, `slate_style.{h,cpp}` | Style + Brush + claude.dark. |
| `runtime/engine/ui/slate_scrollbox.{h,cpp}`, `slate_editable_text.{h,cpp}`, `slate_combobox.h` | Composite widgets. |
| `runtime/engine/ui/slate_declarative.h` | DSL (header-only). |
| `runtime/engine/ui/slate_window.{h,cpp}`, `slate_focus.{h,cpp}`, `slate_tooltip.{h,cpp}`, `slate_dragdrop.{h,cpp}` | Window + focus + tooltip + drag-drop. |
| `runtime/engine/vfx/editor/vfx_editor_slate.{h,cpp}` | The VFX editor rebuild. |
| `runtime/demos/slate_ui_step{1..9}/` | Per-step visualization demo. |

## Visual verification policy

Every step ships a demo under `runtime/demos/slate_ui_stepN/`. The step is
not considered complete until at least one screenshot is captured and
visually inspected. See `.claude/rules/visual-verification.md` for the
project-wide policy.
