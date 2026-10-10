## 1. Implementation

- [x] 1.1 Added a new `OverlayRrectVertex` struct (96 bytes) that carries all per-quad parameters inline — chosen over a separate primitive-type tag byte because rrect uses its own pipeline + its own vertex buffer (cleaner dispatch than a megashader with a switch)
- [x] 1.2 Replicated per-quad params across all 6 vertices of each quad (simpler than a structured buffer indexed by `vertexID/4`; for typical editor frames the replication bandwidth is negligible) — see `AddRoundedRect` in `text_renderer.cpp`
- [x] 1.3 Created `runtime/shaders/editor/ui_rrect.hlsli` with `sdRoundedBox(p, b, r)` + `AaFillFromSdf`, `AaBorderFromSdf`, `ShadowFalloff` helpers (Inigo Quilez reference); function body duplicated verbatim into `overlay_rrect_ps.hlsl` because DXC's default include handler cannot resolve `.hlsli` for source-embedded shaders
- [x] 1.4 New dedicated pipeline + shaders: `overlay_rrect_vs.hlsl` + `overlay_rrect_ps.hlsl` — separates SDF concerns from the rect/sprite/text paths instead of a megashader; bindless (SM6.6) + SM5 IA paths both covered
- [x] 1.5 Added `TextRenderer::AddRoundedRect(x, y, w, h, cornerRadius, fillColor, borderThickness=0, borderColor=transparent, shadowOffset={0,0}, shadowBlur=0, shadowColor=transparent)` — every SDF feature opt-in via defaults
- [x] 1.6 Degenerate fast-path implemented: when `cornerRadius<=0 && borderThickness<=0 && shadowBlur<=0`, `AddRoundedRect` forwards to `AddRect` — byte-for-byte identical vertex stream (verified by `DegenerateRrectByteIdenticalToAddRect` test)
- [x] 1.7 Migrated `vfx_module_picker_dialog.cpp` panel chrome to two `AddRoundedRect` calls (8-px corners, 1-px border, 10-px shadow blur at `(0,4)`) replacing four phase-12 offset rects
- [x] 1.8 Regression screenshot `.screenshots/phase13_3_rrect.png` confirms the editor still renders cleanly with the rrect batch / pipeline / vertex buffer added; `AddRect` path untouched (no rrect batch entries for phase-12 widgets)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
