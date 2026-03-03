## 1. IPC Patch Commands
- [ ] 1.1 Add IPC handler `ui.patch_css` in `src/ipc/IpcServer.cpp` accepting `{filename: string, content: string}`; locate the matching `<style>` or `<link>` element by filename and replace its content/href in-place without reloading the iframe
- [ ] 1.2 Add IPC handler `ui.patch_html_fragment` in `src/ipc/IpcServer.cpp` accepting `{content: string}`; call `morphdom(existingEl, newHtmlString)` on the live DOM using the injected runtime script
- [ ] 1.3 Retain the existing `ui.reload` IPC command unchanged as the fallback path; ensure it is still invoked when patch validity checks fail
- [ ] 1.4 For CSS custom property changes in `ui.patch_css`: extract `--var: value` pairs and call `document.documentElement.style.setProperty` directly for zero-flash updates

## 2. morphdom Integration
- [ ] 2.1 Add `morphdom` (MIT, ~2KB gzipped) to `src/ui/uz_ui_html.dll`'s injected runtime JavaScript bundle; include it as a vendored file at `src/ui/vendor/morphdom.min.js`
- [ ] 2.2 Expose `window.__uz_patch_html(newHtmlString)` in the injected runtime script that calls `morphdom(document.body, newHtmlString, { onBeforeElUpdated: preserveInputState })`
- [ ] 2.3 Ensure morphdom preserves Lua `ctx.ui:on` event listener bindings by using the `onBeforeElUpdated` callback to skip updating elements that have `data-uz-preserve` attribute or active CSS transitions

## 3. Patch Validity Check and Fallback
- [ ] 3.1 Implement `HotReloadPatcher::ValidatePatch(const std::string& new_html)` in `src/ui/HotReloadPatcher.cpp` that inspects the new HTML for `<script>` block changes or root element tag changes
- [ ] 3.2 If `ValidatePatch` returns false, log a warning with the reason and call `ui.reload` instead of `ui.patch_html_fragment`
- [ ] 3.3 Emit a VSCode status bar notification explaining why a full reload was triggered (via IPC `ui.hot_reload_fallback_reason` event)

## 4. Debouncing and Coalescing in VSCode Extension
- [ ] 4.1 Rewrite `vscode-uzengine/src/fileWatcher.ts` to debounce file-change events within a configurable window (default 50ms) using a `setTimeout`-based coalescing approach
- [ ] 4.2 When both HTML and CSS files change within the debounce window, apply CSS patch first then HTML patch in sequence to avoid double-repaint
- [ ] 4.3 Expose the debounce window as a configurable setting `uzengine.hotReload.debounceMs` in `package.json` `contributes.configuration`

## 5. Patch Queue in IpcServer
- [ ] 5.1 Add a `std::queue<HotReloadPatch>` patch queue in `src/ipc/IpcServer.cpp` guarded by a mutex
- [ ] 5.2 If a patch arrives while a previous patch is in flight (applying), enqueue it rather than dropping it
- [ ] 5.3 Drain the queue in FIFO order once the current patch completes, calling each queued patch's IPC handler in sequence

## 6. CVar and Mode Control
- [ ] 6.1 Register `ui.hot_reload_mode` CVar with valid string values `off`, `css-only`, `full` (default `full`)
- [ ] 6.2 When `ui.hot_reload_mode = css-only`, the `ui.patch_html_fragment` handler rejects HTML patches and sends a fallback `ui.reload` instead
- [ ] 6.3 When `ui.hot_reload_mode = off`, all patch and reload IPC commands are no-ops and log a debug message

## 7. Status Bar Indicator
- [ ] 7.1 Add `vscode-uzengine/src/statusBar.ts` with a `HotReloadStatusBar` class that creates a `vscode.StatusBarItem` in the left status bar group
- [ ] 7.2 Update the status bar item text to show last patch type (`CSS`, `HTML`, `FULL`), patch latency in ms, and an error icon with reason on failure
- [ ] 7.3 Wire the status bar to update on each IPC `ui.hot_reload_stats` push event received from the engine

## 8. Stats IPC Handler
- [ ] 8.1 Add IPC handler `ui.get_hot_reload_stats` in `src/ipc/IpcServer.cpp` returning JSON `{patches_applied, css_patches, html_patches, full_reloads, last_patch_ms}`
- [ ] 8.2 Maintain the stats counters as atomic integers in a `HotReloadStats` struct in `src/ui/HotReloadPatcher.h`

## 9. Testing
- [ ] 9.1 Create `vscode-uzengine/src/__tests__/fileWatcher.test.ts` with Jest test suite `FileWatcherTests`
- [ ] 9.2 Write test `DebounceCoalescesRapidSaves`: fire 5 file-change events within 10ms; assert the watcher fires exactly one IPC call after the 50ms debounce window
- [ ] 9.3 Write test `CssAndHtmlCoalescedOrder`: fire one CSS and one HTML change event within the debounce window; assert `ui.patch_css` is called before `ui.patch_html_fragment`
- [ ] 9.4 Write test `FallbackTriggeredForScriptChanges`: simulate a `ui.patch_html_fragment` where `ValidatePatch` returns false (new HTML contains `<script>`); assert `ui.reload` is called and a fallback reason notification is sent
- [ ] 9.5 Write test `PatchQueueDrainsInOrder`: send two HTML patches while first is in-flight; assert both patches are applied in FIFO order
- [ ] 9.6 Write test `HotReloadModeOffBlocksPatches`: set `ui.hot_reload_mode = off`; fire a CSS change; assert zero IPC patch calls are made
- [ ] 9.7 Write test `CssOnlyModeBlocksHtmlPatch`: set `ui.hot_reload_mode = css-only`; fire an HTML change; assert `ui.reload` is called instead of `ui.patch_html_fragment`
- [ ] 9.8 Write test `StatsCounterIncrementsOnPatch`: call `ui.patch_css` three times; call `ui.get_hot_reload_stats`; assert `css_patches == 3` and `patches_applied == 3`
- [ ] 9.9 Verify Jest line coverage >= 95% and branch coverage >= 95% for `fileWatcher.ts` and `statusBar.ts`

## 10. Documentation
- [ ] 10.1 Update `docs/specs/ui-system.md` with the diff-based patch strategy, morphdom integration, debounce behavior, and `ui.hot_reload_mode` CVar values
- [ ] 10.2 Update `docs/specs/editor-panels.md` with the status bar indicator behavior and `ui.get_hot_reload_stats` IPC response schema
