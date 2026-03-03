## 1. Color Property Widget

- [ ] 1.1 Add `color` to `PropertyType` union in `src/inspector-panel.ts`
- [ ] 1.2 Add `PropertyDescriptor` with `type: 'color'` to `_buildProperties()` (e.g. material albedo)
- [ ] 1.3 Render `<input type="color">` in `_renderWidget()` for `color` type
- [ ] 1.4 Convert hex string (0..255) ↔ float [0..1] in `onPropertyChange` webview script
- [ ] 1.5 Send converted `{ r, g, b }` float values via `scene.setProperty` IPC on change

## 2. Resource Path Property Widget

- [ ] 2.1 Add `resource` to `PropertyType` union in `src/inspector-panel.ts`
- [ ] 2.2 Render `resource` widget: read-only text field + "Browse..." button in `_renderWidget()`
- [ ] 2.3 Add `vscode.commands.registerCommand('uzengine.browseResource', ...)` in `extension.ts`
- [ ] 2.4 Command opens `vscode.window.showOpenDialog` filtered to `.uzmat`, `.hlsl`, `.uzscene`, etc.
- [ ] 2.5 Selected path is posted back to the webview via `panel.webview.postMessage`
- [ ] 2.6 Webview receives path update and sends `scene.setProperty` IPC

## 3. Testing

- [ ] 3.1 Add test cases for `color` widget rendering in `__tests__/inspector-panel.test.ts`
- [ ] 3.2 Test hex-to-float conversion edge cases (black #000000, white #ffffff, mid values)
- [ ] 3.3 Test `resource` widget HTML output
- [ ] 3.4 Test `uzengine.browseResource` command (mock `showOpenDialog` returning undefined and a path)
- [ ] 3.5 Test webview message `resourcePathSelected` updates webview state
- [ ] 3.6 Verify ≥95% line + branch coverage on all modified files

## 4. Documentation

- [ ] 4.1 Add `color` and `resource` property types to `docs/vscode-extension.md` Inspector section
