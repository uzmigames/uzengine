## 1. Project Configuration
- [ ] 1.1 Add `ui.reference_width` (int, default 1920) and `ui.reference_height` (int, default 1080) keys to the `.uzproj` parser in `src/engine/ProjectConfig.cpp`
- [ ] 1.2 Add `ui.scale_mode` key to the `.uzproj` parser with string values `match-width`, `match-height`, `expand`, `shrink`, `stretch`; default to `match-width`
- [ ] 1.3 Add `ui.hidpi_enabled` bool key to the `.uzproj` parser (default `false`)
- [ ] 1.4 Expose the parsed values as `ProjectConfig::ui_reference_width`, `ui_reference_height`, `ui_scale_mode`, and `ui_hidpi_enabled` fields in `src/engine/ProjectConfig.h`

## 2. Scale Factor Computation in UIPass
- [ ] 2.1 Implement `UIPass::UpdateScaling()` in `src/ui/UIPass.cpp` called once per frame when viewport size changes
- [ ] 2.2 Compute `scale_x = (float)viewport_width / reference_width` and `scale_y = (float)viewport_height / reference_height` in `UpdateScaling()`
- [ ] 2.3 Implement `match-width` mode: `scale = scale_x`
- [ ] 2.4 Implement `match-height` mode: `scale = scale_y`
- [ ] 2.5 Implement `expand` mode: `scale = max(scale_x, scale_y)`
- [ ] 2.6 Implement `shrink` mode: `scale = min(scale_x, scale_y)`
- [ ] 2.7 Implement `stretch` mode: `scale_x` and `scale_y` applied independently (non-uniform)

## 3. CSS Transform Application
- [ ] 3.1 After computing the final scale value, inject `transform: scale(${scale}); transform-origin: top left;` on the UI root `<div>` element via the injected runtime script
- [ ] 3.2 For `stretch` mode, inject `transform: scale(${scale_x}, ${scale_y})` on the root element

## 4. HiDPI Device Pixel Ratio Compensation
- [ ] 4.1 Read the device pixel ratio (DPR) from the OS via `GetDpiForWindow` / `GetDpiForMonitor` in `src/ui/UIPass.cpp`
- [ ] 4.2 When `ui.hidpi_enabled = true`, set the WebView surface size to `floor(viewport_width * dpr)` x `floor(viewport_height * dpr)` physical pixels
- [ ] 4.3 Set `zoom: ${1/dpr}` on the root element to counteract the browser's automatic DPI upscaling, producing crisp native-resolution rendering

## 5. CSS Custom Property and Lua API
- [ ] 5.1 After computing the scale, set CSS custom property `--uz-ui-scale` on `document.documentElement` via the injected script
- [ ] 5.2 Implement `ctx.ui.scale_factor` Lua read-only property in `src/lua/lua_ui_api.cpp` returning the current computed scale value
- [ ] 5.3 Implement `ctx.ui:set_reference_resolution(w, h)` Lua API in `src/lua/lua_ui_api.cpp` that overrides `reference_width`/`reference_height` at runtime and triggers `UpdateScaling()`
- [ ] 5.4 Implement `ctx.ui:set_scale_mode(mode)` Lua API in `src/lua/lua_ui_api.cpp` that overrides `ui_scale_mode` at runtime and triggers `UpdateScaling()`

## 6. Anchor Point Support
- [ ] 6.1 Implement detection of `data-anchor` attribute values (`top-left`, `top-right`, `bottom-left`, `bottom-right`, `center`) in the injected runtime script
- [ ] 6.2 Move anchored elements to a separate unscaled overlay `<div>` layer that is not subject to the root scale transform
- [ ] 6.3 Compute the CSS `left`/`top`/`right`/`bottom` position for each anchored element using `data-anchor-offset-x` and `data-anchor-offset-y` values scaled by the current scale factor
- [ ] 6.4 Re-apply anchor positions whenever `UpdateScaling()` fires a viewport change

## 7. Debug Overlay
- [ ] 7.1 Register CVar `ui.debug_reference_rect` (int, default 0) in the CVar system
- [ ] 7.2 When `ui.debug_reference_rect = 1`, inject a semi-transparent CSS overlay rectangle on the UI root showing the reference resolution bounds within the actual viewport using a colored border `<div>` with fixed position and `pointer-events: none`

## 8. Testing
- [ ] 8.1 Create `tests/ui/reference_resolution_test.cpp` with Google Test suite `ReferenceResolutionTest`
- [ ] 8.2 Write test `MatchWidthScale`: call `UpdateScaling()` with viewport `2560x1440`, reference `1920x1080`, mode `match-width`; assert `scale == 2560.0f/1920.0f`
- [ ] 8.3 Write test `MatchHeightScale`: same inputs, mode `match-height`; assert `scale == 1440.0f/1080.0f`
- [ ] 8.4 Write test `ExpandScale`: same inputs, mode `expand`; assert `scale == max(2560.0f/1920.0f, 1440.0f/1080.0f)`
- [ ] 8.5 Write test `ShrinkScale`: same inputs, mode `shrink`; assert `scale == min(2560.0f/1920.0f, 1440.0f/1080.0f)`
- [ ] 8.6 Write test `StretchScaleNonUniform`: mode `stretch`; assert `scale_x != scale_y` when aspect ratios differ
- [ ] 8.7 Write test `NoScaleWhenViewportMatchesReference`: viewport `1920x1080`, reference `1920x1080`, mode `match-width`; assert `scale == 1.0f`
- [ ] 8.8 Write test `AnchorOffsetScaledCorrectly`: set `data-anchor-offset-x=10`, scale factor 2.0; assert computed CSS `left` value is `20px`
- [ ] 8.9 Write test `ProjectConfigDefaultsApplied`: parse a `.uzproj` with no `[ui]` section; assert `reference_width == 1920`, `reference_height == 1080`, `scale_mode == "match-width"`, `hidpi_enabled == false`
- [ ] 8.10 Write test `HidpiSurfaceSizedAtPhysicalPixels`: `hidpi_enabled = true`, DPR = 2.0, viewport 1920x1080; assert WebView surface is 3840x2160
- [ ] 8.11 Verify C++ line coverage >= 95% and branch coverage >= 95% for `UIPass.cpp` scaling paths and `ProjectConfig.cpp` parsing paths

## 9. Documentation
- [ ] 9.1 Update `docs/specs/ui-system.md` with the reference resolution system, all five scale modes, DPR compensation, anchor point attributes, and the `--uz-ui-scale` CSS custom property
