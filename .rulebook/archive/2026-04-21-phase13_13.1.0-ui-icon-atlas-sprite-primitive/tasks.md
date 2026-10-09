## 1. Implementation

- [x] 1.1 Icon atlas is procedurally generated at runtime (256x256 RGBA8, 8x8 grid of 32x32 cells, 24x24 drawn region with 4-px padding) — avoids committing a binary PNG to the repo; see `runtime/engine/vfx/editor/vfx_icon_atlas.cpp::VfxGenerateIconAtlas`
- [x] 1.2 Created `runtime/engine/vfx/editor/vfx_icon_atlas.{h,cpp}` with `VfxUiIcon` enum (33 stable values) + `VfxIconUv` rect table + `VfxGetIconUv(VfxUiIcon)` accessor
- [x] 1.3 Added `TextRenderer::LoadIconAtlas(cmdList, upload)` that generates the RGBA8 atlas, creates a GPU texture + SRV, uploads via the existing `UploadContext::UploadTexture`, and transitions to `ShaderResource`
- [x] 1.4 Added `TextRenderer::AddSprite(VfxUiIcon, x, y, w, h, tint)` primitive with its own vertex batch drawn between the rect and text batches
- [x] 1.5 Added dedicated `overlay_sprite_ps.hlsl` pixel shader (bindless + SM5 paths) that samples the RGBA atlas and multiplies by the vertex tint; new `OverlaySpritePipeline` PSO created by `TextRenderer::InitPipelines`
- [x] 1.6 Wired `VfxEditorOverlay::Init` to call `m_textRenderer.LoadIconAtlas(cmdList, upload)` after font init (non-fatal on failure)
- [x] 1.7 Migrated `vfx_timeline_scrubber_widget.cpp` transport buttons to sprite glyphs: Play/Pause swap on clock state, JumpStart, JumpEnd, StepBack, StepFwd, Restart, Loop (accent tint when active), plus ChevronDown on the SpeedBtn dropdown trigger
- [x] 1.8 Headless screenshot `.screenshots/phase13_icon_atlas.png` confirms sprite transport glyphs render: `|<  <<  ||  >>  >|  O  1.0x v  ∞` instead of ASCII labels

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
