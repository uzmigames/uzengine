# ui-icon-atlas

## ADDED Requirements

### Requirement: Icon atlas loaded at overlay init

`TextRenderer::LoadIconAtlas` SHALL succeed when called with a valid
RGBA8 PNG path. On success, `HasIconAtlas()` returns true and every
subsequent `AddSprite` call samples from the loaded texture.

#### Scenario: LoadIconAtlas succeeds for the shipped atlas

Given a fresh `TextRenderer` initialized and pointed at
`runtime/assets/editor/icons_atlas.png`
When `LoadIconAtlas` is called
Then the method returns `true` and `HasIconAtlas()` is `true`.

### Requirement: AddSprite emits a textured quad

`TextRenderer::AddSprite(icon, x, y, w, h, tint)` SHALL append a
quad whose UV rectangle matches `VfxGetIconUv(icon)` and whose vertex
color equals the tint parameter.

#### Scenario: Play icon sprite has correct UV

Given the shipped icon atlas
When `AddSprite(VfxUiIcon::Play, 0, 0, 24, 24, white)` runs
Then the captured vertex buffer contains a 4-vertex quad whose UV
rect equals `VfxGetIconUv(VfxUiIcon::Play)` within 1/1024 tolerance.

### Requirement: Stable icon enum identity

Every `VfxUiIcon` value SHALL have a stable numeric identity that
does not change across builds — downstream code may cast the enum
to `uint16_t` for serialization.

#### Scenario: Play is always enum value 1

Given any build of the engine
When `static_cast<uint16_t>(VfxUiIcon::Play)` is evaluated
Then the value is exactly 1.

### Requirement: AddText path unchanged

`AddText` SHALL continue to render the ASCII bitmap font without
behavioral changes.

#### Scenario: AddText emits the legacy font SRV quad

Given a pre-existing `AddText` call
When it is re-built against the phase 13.1 `TextRenderer`
Then the emitted quad's source-flag bit equals 0 (font atlas) and
the text renders identically to phase 12.
