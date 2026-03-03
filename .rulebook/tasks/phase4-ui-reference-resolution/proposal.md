# Proposal: phase2-ui-reference-resolution

## Why
Game UI is currently authored at fixed pixel sizes with no scaling system. On a 4K (3840x2160) display every UI element appears at one quarter of its intended visual size because there is no mapping from design coordinates to physical pixels. On a 1280x720 display, elements sized for 1920x1080 overflow their containers. Shipping a game without a reference resolution system means the UI is only correct on the exact resolution the designer used, which is unacceptable for a commercial product.

HiDPI (device pixel ratio > 1) screens add a second axis of complexity: a 2560x1440 display at 150% DPI scaling exposes a logical resolution of 1707x960 to the browser, meaning CSS pixel values represent different physical pixels depending on the OS display scaling setting. Without explicit DPR handling, text and icons appear blurry on HiDPI screens because the UI surface is rendered at logical resolution and upscaled by the compositor rather than being rendered at full native resolution.

Unity's Canvas Scaler and Unreal's DPI Scaling are the reference implementations. Both solve the same problem with a scale-and-anchor model: author at a single reference resolution, compute a scale factor at runtime, and anchor HUD elements to viewport edges so they remain at the correct screen position regardless of aspect ratio. UzEngine's HTML-based UI can implement this more cleanly than either by expressing the scale as a single CSS `transform: scale()` on the root element, letting the browser handle all child layout math without any per-element CSS override.

## What Changes
- Add `ui.reference_width` (default 1920) and `ui.reference_height` (default 1080) keys to `.uzproj`; these define the design coordinate space for all UI CSS pixel values
- Add `ui.scale_mode` key to `.uzproj` with values: `match-width` (scale by width ratio), `match-height` (scale by height ratio), `expand` (use the larger ratio to fill), `shrink` (use the smaller ratio to letterbox/pillarbox), and `stretch` (non-uniform x/y scale)
- Compute scale factors in `UIPass::UpdateScaling()` each frame: `scale_x = viewport_width / reference_width`, `scale_y = viewport_height / reference_height`; apply the selected mode formula to produce a final `scale` value
- Apply the scale as `transform: scale(${scale})` and `transform-origin: top left` on the UI root `<div>` element, so all child CSS pixel values scale uniformly without modification
- Apply device pixel ratio compensation: render the UI WebView surface at `floor(viewport_width * dpr)` x `floor(viewport_height * dpr)` physical pixels and set `zoom: ${1/dpr}` on the root element to counteract the browser's automatic DPI scaling, producing crisp rendering at native resolution on HiDPI screens
- Expose the computed scale factor as CSS custom property `--uz-ui-scale` and as Lua value `ctx.ui.scale_factor` so dynamic layouts (e.g., procedurally positioned elements) can account for scaling in Lua code
- Add anchor point support via `data-anchor="top-left|top-right|bottom-left|bottom-right|center"` attribute: elements with an anchor are positioned in a separate unscaled overlay layer, keeping HUD corners pixel-perfect at any resolution
- Add `data-anchor-offset-x` and `data-anchor-offset-y` attributes to specify the distance from the anchor point in reference-resolution pixels, scaled automatically
- Expose `ctx.ui:set_reference_resolution(w, h)` and `ctx.ui:set_scale_mode(mode)` Lua API for runtime overrides such as cinematic letterbox mode
- Add a debug overlay (`ui.debug_reference_rect 1`) that draws a semi-transparent rectangle showing the reference resolution bounds within the actual viewport, so designers can see the design canvas boundaries at any resolution
- Write tests in `tests/ui/reference_resolution_test.cpp` covering all five scale modes with known viewport/reference combinations and verifying the computed scale value and anchor offset calculations

## Impact
- Affected specs: `docs/specs/ui-system.md`
- Affected code: `src/ui/UIPass.cpp`, `src/ui/UIPass.h`, `src/ui/uz_ui_html.dll`, `src/engine/ProjectConfig.cpp`, `src/engine/ProjectConfig.h`
- Breaking change: NO — existing projects that do not set `ui.reference_width` in their `.uzproj` get a scale factor of 1.0 (no change in behaviour); DPR compensation is opt-in via `ui.hidpi_enabled 1`
- User benefit: UI designed at 1920x1080 automatically looks correct at any resolution from 720p to 4K without any per-resolution CSS overrides; HiDPI screens get crisp native-resolution rendering; HUD elements pinned to screen corners remain anchored correctly on any aspect ratio from 4:3 to 21:9
