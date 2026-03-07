## 1. Color Property Widget

- [x] 1.1 Add `color` to `PropertyType` union in `src/inspector-panel.ts`
- [x] 1.2 Add `PropertyDescriptor` with `type: 'color'` to `_buildProperties()` (e.g. material albedo)
- [x] 1.3 Render `<input type="color">` in `_renderWidget()` for `color` type
- [x] 1.4 Convert hex string (0..255) ↔ float [0..1] in `onPropertyChange` webview script
- [x] 1.5 Send converted `{ r, g, b }` float values via `scene.setProperty` IPC on change

## 2. Resource Path Property Widget

- [x] 2.1 Add `resource` to `PropertyType` union in `src/inspector-panel.ts`
- [x] 2.2 Render `resource` widget: read-only text field + "Browse..." button in `_renderWidget()`
- [x] 2.3 Add `vscode.window.showOpenDialog` in `_handleResourceBrowse()` (both InspectorPanel and InspectorViewProvider)
- [x] 2.4 Dialog opens filtered to `.uzmat`, `.hlsl`, `.uzscene`, `.png`, `.jpg`, `.tga`, `.obj`, `.fbx`
- [x] 2.5 Selected path is posted back to the webview via `panel.webview.postMessage`
- [x] 2.6 Webview receives path update and sends `scene.setProperty` IPC

## 3. Testing

- [x] 3.1 Add test cases for `color` widget rendering in `__tests__/inspector-panel.test.ts`
- [x] 3.2 Test hex-to-float conversion edge cases (black #000000, white #ffffff, mid values, clamp)
- [x] 3.3 Test `resource` widget HTML output
- [x] 3.4 Test resource browse (mock `showOpenDialog` returning undefined, empty array, and a path)
- [x] 3.5 Test webview message `resourcePathSelected` updates webview state
- [x] 3.6 Verify ≥95% line + branch coverage on all modified files — **achieved 100%**

## 4. Documentation

- [x] 4.1 Updated `inspector-panel.ts` header comment with `color` and `resource` property types
