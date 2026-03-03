# Proposal: phase3-accessibility-framework

## Why
The engine exposes no accessibility options — there are no colorblind modes, subtitle support, reduced motion settings, or high contrast UI modes. Major platforms including PS5 and Xbox require games to meet accessibility guidelines, and both Sony and Microsoft run certification checks for these features. Several high-profile games have faced public backlash and negative press coverage for shipping without basic accessibility support. Without a framework-level solution every game developer would have to implement these features from scratch, with no consistency guarantee.

## What Changes
- Add `AccessibilitySettings` singleton with fields: `colorblind_mode` (enum: None, Deuteranopia, Protanopia, Tritanopia), `reduced_motion` (bool), `large_text_scale` (float 1.0–2.0), `high_contrast_ui` (bool), `subtitles_enabled` (bool)
- Colorblind simulation: post-process pass applies a 3x3 LUT matrix (Daltonize algorithm) to the final frame based on selected mode; LUT matrices stored as engine constants; zero overhead when mode is None
- Subtitle system: `SubtitleManager::show(text, duration_ms, speaker_name)` renders a styled overlay via the UI system; game code and Lua scripts call this on dialogue events; font size respects `large_text_scale`
- Reduced motion flag: queried by screen shake system (disables shake when true), motion blur pass (disables when true), and any particle-heavy camera effects; game Lua scripts can query `ctx.accessibility.reduced_motion`
- Large text mode: `large_text_scale` multiplier applied to all `ctx.ui` text elements globally; UI layout system reflowed to prevent clipping at 2x scale
- High contrast UI: alternate CSS class injected into uz_ui_html.dll when enabled; provides black/white palette overrides for game UI elements
- Settings persisted to `%APPDATA%/UzEngine/accessibility.ini`; loaded before first frame; not tied to save slots
- Lua read API: `ctx.accessibility.colorblind_mode`, `ctx.accessibility.subtitles_enabled`, etc. (read-only from Lua; write from settings screen only)

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/screen-space-effects.md`, `docs/specs/localization.md`
- Affected code: `src/accessibility/accessibility_settings.h` (new), `src/accessibility/accessibility_settings.cpp` (new), `src/renderer/post_process.cpp`, `src/ui/subtitle_manager.cpp` (new), `src/scripting/lua_accessibility.cpp` (new)
- Breaking change: NO
- User benefit: Games built on UzEngine meet PS5/Xbox accessibility certification requirements out of the box. Players with colorblindness, motion sensitivity, or low vision get a first-class experience without requiring per-game custom work from developers.
