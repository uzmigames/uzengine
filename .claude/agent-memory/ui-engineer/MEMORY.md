# UI Engineer — Persistent Memory

## Project: UzEngine
uz_ui_html.dll = stable C ABI HTML+CSS renderer. No JavaScript. Phase 2.

## Key Paths
- `docs/specs/ui-system.md` — complete spec
- `docs/modules/ui/README.md` — module-level overview
- `docs/specs/console-commands.md` — in-game console spec

## Architecture Rules
- uz_ui_html.dll is a SEPARATE DLL with stable C ABI (never break signatures)
- UIPass sort_key = 800 (after tonemapping, before TAA)
- Modal documents: consume input (return true from ui_deliver_input)
- HUD documents: passthrough (don't consume input)
- Data binding: CSS custom properties via ui_set_property(doc, name, value)

## Z-Order Layering
- 0–499: World-space UI
- 500–699: HUD overlays
- 700–899: Pop-ups, tooltips
- 900+: Modal menus, full-screen overlays

## CUIRoot Component
- type=UIRoot in .uzscene
- Properties: ui_document, ui_layer, ui_auto_show, ui_modal
