# Proposal: phase3-ui-accessibility

## Why
UzEngine's HTML game UI has no screen reader support, no high-contrast mode, no visible focus ring for keyboard and gamepad navigation, and no reduced-motion option. Many platforms (PC storefronts, console certification) require accessibility compliance. Players with visual impairments, motor disabilities, or vestibular disorders cannot use the UI at all. Games built on UzEngine will fail platform certification reviews and exclude a significant portion of their potential audience without these features.

## What Changes
- ARIA role attributes: `uz_ui_html.dll` parses `role`, `aria-label`, `aria-live`, `aria-disabled` on HTML elements and exposes them to the Windows UI Automation (UIA) tree so screen readers (Narrator, NVDA) can read them
- Text-to-speech IPC bridge: `ctx.ui:speak(text, interrupt)` sends text to Windows SAPI via a named pipe; interrupt=true cancels current utterance; speech rate and volume configurable in game settings
- High contrast theme: activated by `ctx.ui:set_high_contrast(true)` or by detecting `prefers-contrast: more` system preference; applies a CSS class `.uz-high-contrast` globally that reduces all colors to black, white, and yellow with minimum 4.5:1 contrast ratio
- Visible focus ring: all focusable elements (`button`, `input`, `[tabindex]`, `<uz-*>`) receive a 2px yellow outline when focused via keyboard or gamepad; ring is suppressed for mouse interaction to avoid visual clutter (`:focus-visible` semantics)
- Tab order management: `tabindex` attribute respected; `ctx.ui:focus_next()` / `ctx.ui:focus_prev()` called by gamepad D-pad navigation; `ctx.ui:focus(element_id)` programmatic focus
- `prefers-reduced-motion` media query support in HTML/CSS: animations and transitions are disabled automatically when the system accessibility setting is enabled or `ctx.ui:set_reduced_motion(true)` is called
- `ctx.settings` integration: TTS rate, TTS volume, high contrast, and reduced motion exposed as standard game settings entries

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/input-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/localization.md`
- Affected code: `src/ui/accessibility.cpp`, `src/ui/accessibility.h`, `src/ui/ui_pass.cpp`, `src/ui/focus_manager.cpp`, `src/lua/lua_ui_api.cpp`
- Breaking change: NO
- User benefit: Players with visual and motor impairments gain usable screen reader and keyboard/gamepad navigation support; games pass platform accessibility certification requirements
