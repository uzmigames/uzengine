# Proposal: phase2-ui-hot-reload-stable

## Why
The current UI hot-reload mechanism reloads the entire iframe when any HTML or CSS file changes, producing a visible white flash and resetting all UI state (animations, scroll positions, open menus). This interrupts the designer's flow on every iteration and makes it impossible to test animated states that require specific conditions to reach. Professional UI tooling requires that CSS-only changes apply instantly and invisibly, without discarding runtime state.

The white-flash problem is not cosmetic — it breaks the design feedback loop. When a designer is tuning an animation that requires the player to enter a specific game state (e.g., low-health HUD pulse), a full iframe reload forces them to re-enter that state on every CSS tweak. This can add minutes to each iteration and causes designers to batch changes rather than exploring freely. Vite HMR and Chrome DevTools demonstrate that sub-100ms, state-preserving CSS updates are achievable; UzEngine should match that experience for game UI authors.

A secondary concern is reliability: the current watcher has race conditions when multiple files are saved in rapid succession (e.g., a formatter saving `.html` and `.css` simultaneously), causing double-reloads or missed updates. The new implementation must debounce and coalesce file events into a single patch operation, and must queue patches if a previous patch is still in flight to avoid ordering violations.

## What Changes
- Replace the full iframe `src` reload with a diff-based patch strategy: on file save, the VSCode extension reads the changed file and sends its new contents over IPC using `ui.patch_css` or `ui.patch_html_fragment` instead of triggering `ui.reload`
- For CSS-only changes: locate the corresponding `<style>` or `<link>` element by filename in the document, replace its content/href in-place; for CSS custom property changes extract the property-value pairs and call `document.documentElement.style.setProperty` directly for zero-flash updates with no style recalculation on unchanged rules
- For HTML structural changes: integrate the `morphdom` library (MIT, 2KB gzipped) into `uz_ui_html.dll`'s injected runtime script; call `morphdom(existingEl, newHtmlString)` to diff and patch the DOM tree in place, preserving existing element identities, event listeners attached via Lua `ctx.ui:on`, and CSS transition states
- Add a patch validity check: if the morphdom diff detects `<script>` block changes or root element tag changes (cases that cannot be safely patched in-place), log a warning and fall back to a full iframe reload with a notification in the VSCode status bar explaining why
- Add debouncing in `vscode-ext/src/fileWatcher.ts`: coalesce all file-change events within a 50ms window into a single patch operation; if files of both types (HTML and CSS) change together, apply CSS patch first then HTML patch in sequence to avoid a double repaint
- Add a patch queue in `src/ipc/IpcServer.cpp` so that if a patch arrives while a previous patch is being applied, it is queued and applied in order rather than dropped
- Expose `ui.hot_reload_mode` CVar with values `off`, `css-only`, `full` for environments where full patch support is unavailable (e.g., production builds where uz_ui_html.dll is stripped)
- Add a hot-reload status indicator in the VSCode status bar showing last patch type (`CSS`, `HTML`, `FULL`), patch latency in ms, and an error state with the failure reason if a patch fails
- Add IPC handler `ui.get_hot_reload_stats` returning `{patches_applied, css_patches, html_patches, full_reloads, last_patch_ms}` for the Memory/Debug panel
- Write integration tests in `vscode-ext/src/test/fileWatcher.test.ts` covering debounce timing, coalescing of simultaneous saves, and fallback trigger conditions

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/editor-panels.md`
- Affected code: `src/ui/uz_ui_html.dll`, `src/ui/HotReloadPatcher.cpp`, `src/ipc/IpcServer.cpp`, `vscode-ext/src/fileWatcher.ts`, `vscode-ext/src/ipcClient.ts`, `vscode-ext/src/statusBar.ts`
- Breaking change: NO — the `ui.reload` IPC command remains available and is used as the fallback
- User benefit: CSS and layout edits appear in the running game UI within one frame of saving the file with no visible flash or state reset; animation states are preserved across tweaks; the iteration loop for UI design is reduced from a multi-second interruption to a seamless sub-100ms update
