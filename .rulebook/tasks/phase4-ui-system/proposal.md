# Proposal: phase2-ui-system

## Why

Every shipped game requires a UI layer for HUDs, menus, pause screens, dialogue boxes, and inventory. UzEngine currently has no UI system. Game developers on top of UzEngine are forced to either skip UI entirely, hand-roll a rendering layer in C++, or bolt on Dear ImGui — which is a debug tool, not a shipping UI framework. This makes UzEngine unsuitable for any production game project regardless of how strong the rendering and scripting systems are.

The design choice to use HTML and CSS as the authoring language is deliberate. HTML and CSS are the most widely understood declarative layout languages in existence. Game developers, artists, and UI designers already know them. A system that renders an HTML subset over the DX12 frame removes the need to learn a proprietary UI format, keeps UI files as plain text that can be version-controlled and AI-generated, and makes the data binding story trivial via CSS custom properties (`--health: 0.73`).

Placing `uz_ui_html.dll` in a separate DLL rather than linking it into the engine proper means the layout and rendering cost is isolated, the dependency on `msdfgen` and Media Foundation does not pollute the engine binary, and the UI system can be replaced or upgraded independently without rebuilding the engine.

## What Changes

- Create `uz_ui_html.dll` in `src/modules/uz_ui_html/` — standalone DLL exposing a C ABI (`ui_doc_t *ui_doc_load(const char *path)`, `ui_doc_render(doc, cmd_list)`, `ui_doc_set_property(doc, key, value)`, `ui_doc_set_event_cb(doc, cb)`, `ui_doc_free(doc)`); engine links against this via `GetProcAddress` at runtime
- Implement HTML subset parser in `src/modules/uz_ui_html/html_parser.c` — supports `<div>`, `<p>`, `<span>`, `<button>`, `<input>`, `<progress>`, `<img>`, `<video>`, `<canvas>`; no JavaScript; produces a DOM tree in the frame arena
- Implement CSS box model and Flexbox layout engine in `src/modules/uz_ui_html/css_layout.c` — parses inline and file-linked `.css`, resolves CSS custom properties (`--var`), computes Flexbox layout passes; transitions handled via per-frame lerp on changed properties
- Implement `UIPass` in `src/engine/renderer/ui_pass.c` — runs after the main scene pass; iterates all `UIRoot` nodes sorted by layer key; calls `ui_doc_render` with the current DX12 command list; uses a dedicated descriptor heap and SRV for UI textures
- Add `UIRoot` scene node type in `src/engine/scene/nodes/node_ui_root.c` — `.uzscene` format: `[node.type=UIRoot] doc=hud/health.html layer=200`; loads the HTML doc via `uz_ui_html.dll` on scene load, frees on node destroy
- Implement data binding in `src/modules/uz_ui_html/data_binding.c` — `ui_doc_set_property(doc, "health", "0.73")` updates the CSS custom property `--health` in the document; layout is re-evaluated on the next frame; no polling required
- Implement event callbacks — DOM events (`click`, `change`, `hover`, `focus`) fire a `UiEvent` struct to the registered callback; `src/engine/scripting/lua_ui.c` bridges this to Lua as `ctx.ui:on_event("btn_start", function(e) end)`
- Add `ctx.ui` Lua API in `src/engine/scripting/lua_ui.c`: `ctx.ui:load(path)`, `ctx.ui:show(doc)`, `ctx.ui:hide(doc)`, `ctx.ui:set_property(doc, key, value)`, `ctx.ui:on_event(doc, selector, fn)`, `ctx.ui:unload(doc)`
- Integrate `msdfgen` for font SDF rasterization in `src/modules/uz_ui_html/font_atlas.c` — fonts are baked to SDF atlases at Cook time and loaded as `RhiTexture` handles; runtime glyph lookup is a single atlas UV lookup
- Add video element support via Windows Media Foundation in `src/modules/uz_ui_html/video_element.c` — `<video src="cutscene.mp4">` decodes H.264/H.265 via hardware (`IMFSourceReader`), uploads each frame to a `RhiTexture`, samples it in the UI shader
- Implement hot-reload in `src/modules/uz_ui_html/hot_reload.c` — watches `.html` and `.css` files via `ReadDirectoryChangesW`; re-parses and re-lays out the affected document within one frame; wired into the existing asset hot-reload bus
- Add layer sort keys as constants in `src/engine/ui/ui_layers.h`: `UZ_UI_LAYER_WORLD=50`, `UZ_UI_LAYER_HUD=200`, `UZ_UI_LAYER_MENU=600`, `UZ_UI_LAYER_SYSTEM=950`

## Impact

- Affected specs: `docs/specs/ui-system.md`
- Affected code: `src/modules/uz_ui_html/`, `src/engine/renderer/ui_pass.c`, `src/engine/scene/nodes/node_ui_root.c`, `src/engine/scripting/lua_ui.c`, `src/engine/ui/ui_layers.h`
- Breaking change: NO
- User benefit: Game developers author HUDs and menus in HTML and CSS, bind game data via CSS custom properties, and control UI from Lua without writing any C++; the system ships as a separate DLL so it does not inflate the engine binary for projects that do not use it
