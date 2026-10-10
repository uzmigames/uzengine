# editor-visual-repaint-v2

## ADDED Requirements

### Requirement: Every card uses rounded corners

Every widget that renders a "card" surface (module card, emitter
row, parameter row, picker tile, context menu panel, dropdown panel,
timeline speed pill, status-bar chip) SHALL use
`TextRenderer::AddRoundedRect` with a non-zero `cornerRadius`. Raw
`AddRect` is retained only for axis-aligned dividers + full-panel
backgrounds.

#### Scenario: Module card emitted with rounded corners

Given the stack panel bound to a populated emitter
When `Render` runs
Then the captured primitive list shows every `ModuleCard` rect with
primitive tag = 3 (rounded rect) and `cornerRadius >= 2.0`.

### Requirement: Transport uses sprite icons

The timeline scrubber's transport buttons (Play / Pause / StepBack /
StepForward / JumpStart / JumpEnd / Restart / Loop) SHALL render
via `TextRenderer::AddSprite(VfxUiIcon::...)` — no ASCII glyph or
Unicode codepoint is used for those specific buttons.

#### Scenario: Play button is a sprite

Given the timeline rendered in a frame where the clock is paused
When the frame's vertex buffer is inspected
Then the Play button's quad has primitive tag = 2 (sprite) and the
UV rect equals `VfxGetIconUv(VfxUiIcon::Play)`.

### Requirement: Crisp text via MSDF

Every text call in the editor that renders > 16 px OR < 12 px SHALL
use the MSDF path. Text at the native bake size MAY stay bitmap.

#### Scenario: 24-px header uses MSDF

Given a phase-12 header label migrated to MSDF in phase 13.5
When it renders at 24 px height
Then the emitted quad has primitive tag = 4 (MSDF text) and the
MSDF sampler produces an AA edge of 1-2 pixels.

### Requirement: Screenshots pass visual regression

Headless screenshots at 1280x720, 1920x1080, 2560x1440 SHALL all
show the Niagara-parity editor rendering without aliasing artifacts
or missing glyph fallbacks.

#### Scenario: 2560x1440 screenshot is crisp

Given a fresh demo built with 13.1-13.4 + 13.5
When `vfx_viewport_app.exe --screenshot-stdout --screenshot-frame 5
--frames 8 --window-size 2560x1440` runs
Then the decoded PNG shows the stack panel + transport + center
panel + system overview rendering with crisp text + real icon
sprites + rounded panel chrome.
