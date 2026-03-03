# TypeScript VSCode Extension Agent Memory

## Project Structure
- Extension: `/workspaces/UzEngine/vscode-uzengine/`
- Source: `src/` — TypeScript extension files
- Tests: `src/__tests__/*.test.ts`
- VSCode mock: `src/__mocks__/vscode.ts`
- ws stub: `src/__mocks__/ws.ts` (export = WebSocket style for CommonJS compat)
- Coverage config in `package.json` jest config

## Test Infrastructure

### vscode mock (`src/__mocks__/vscode.ts`)
- `window.createOutputChannel`, `createStatusBarItem`, `showErrorMessage` — all jest.fn()
- `workspace.__setConfig(section, key, value)` / `workspace.__clearConfig()` — config test helpers
- `workspace.workspaceFolders` — mutable array, cast as `any` to override
- `makeMockContext()` — returns mock ExtensionContext with subscriptions[]
- `makeMockWebviewPanel()` — full mock panel with _simulate* helpers:
  - `_simulateMessage(msg)`, `_simulateDispose()`, `_simulateVisibilityChange(visible)`
  - `onDidReceiveMessage(cb, _thisArg?, disposables?)` — pushes to `listeners.message[]` AND provided `disposables[]`
  - `onDidDispose(cb, _thisArg?, disposables?)` — pushes to `listeners.dispose[]` AND provided `disposables[]`
- `__setMockPanelFactory()` / `__resetMockPanelFactory()` — control panel creation

### moduleNameMapper (package.json)
```json
"^vscode$": "<rootDir>/src/__mocks__/vscode.ts",
"^ws$": "<rootDir>/src/__mocks__/ws.ts"
```
ws.ts uses `export = WebSocket` (not `export default`) so `require('ws')` returns the constructor directly.

### Type cast patterns for mocks
- `jest.fn<any>().mockResolvedValue(...)` — when TypeScript infers `never` for mockResolvedValue
- `jest.Mock<(...args: any[]) => any>` causes `never` because the fn type is not PromiseLike — use `jest.Mock<any>` instead
- `(vscodeWindow.showErrorMessage as unknown as jest.Mock<() => Promise<any>>).mockResolvedValue(...)`

### module mocking
- `jest.mock('child_process')` + `(cp.spawn as jest.Mock).mockReturnValue(mockProc)`
- `jest.mock('fs', () => ({ existsSync: jest.fn().mockReturnValue(false) }))` — full fs mock
- `jest.mock('net')` + `(net.Socket as unknown as jest.Mock).mockImplementation(...)`
- `jest.mock('../viewport-manager', () => ({ ViewportManager: { getInstance: jest.fn() } }))`
- `jest.isolateModules(() => { const { X } = require('...') as typeof import('...'); })` — fresh module scope

### Private method testing
- Cast to `any` to call private methods: `(inspector as any)._renderWidget({...}, 'node-id')`

## Coverage Patterns

### Stale WebSocket Reference Branches
To cover `if (this._ws !== ws) { return; }` in `IpcClient`:
1. Connect socket 1, **save its callback reference** (before connect() nullifies it)
2. Connect socket 2, open socket 2
3. Call **saved** callback from socket 1 — triggers stale-reference guard

### Disposed Timer Guard (Inside setInterval)
Use `jest.spyOn(client as any, '_stopReconnectLoop').mockImplementationOnce(() => {})` to keep timer alive after `disconnect()`, then `jest.advanceTimersByTime(...)` to fire it.

### getDefaultWsFactory Coverage (ipc-client.ts lines 111-118)
Requires `ws` moduleNameMapper + `jest.isolateModules` to get fresh module with uncached `_defaultWsFactory`.

### validateInput Callback Coverage
```typescript
let capturedValidator: ((v: string) => string | undefined) | undefined;
mockWindow.showInputBox.mockImplementation((opts: { validateInput?: ... }) => {
    capturedValidator = opts.validateInput;
    return Promise.resolve('ValidName');
});
await provider.renameNode(item);
expect(capturedValidator!('')).toBe('Node name cannot be empty');
```

### Error Branch Coverage (instanceof Error ternary)
Test by rejecting IPC calls with plain strings: `ipc.request.mockRejectedValue('plain string error')`

## Coverage Status (current)
- `console-panel.ts`: 100%/100%/100%/100% ✅
- `inspector-panel.ts`: 100%/100%/100%/100% ✅ (InspectorPanel + InspectorViewProvider)
- `play-controls.ts`: 100%/100%/100%/100% ✅
- `scene-tree-provider.ts`: 98.11%/97.77%/100%/98.92% ✅
- `ipc-client.ts`: 100%/100%/96.29%/100% ✅
- `server-manager.ts`: 100%/100%/100%/100% ✅
- `stream-panel.ts`: 100%/100%/100%/100% ✅
- Global: 98.66% stmts / 95.76% branch / 97.24% funcs / 98.96% lines ✅
- Total: 641 tests across 16 suites (all passing)

## WebviewView Mock Pattern (inline — no shared mock exists)
For WebviewViewProvider tests, create an inline mock (not in vscode.ts):
```typescript
function makeMockWebviewView() {
    const messageHandlers: Array<(msg: unknown) => void> = [];
    const webview = {
        options: {} as { enableScripts?: boolean },
        html: '',
        postMessage: jest.fn().mockResolvedValue(true),
        onDidReceiveMessage: jest.fn((cb: (msg: unknown) => void) => {
            messageHandlers.push(cb);
            return { dispose: jest.fn() };
        }),
        cspSource: '',
    };
    return {
        webview,
        _triggerMessage(msg: unknown) { messageHandlers.forEach(h => h(msg)); },
    };
}
```
Cast to `view as unknown as import('vscode').WebviewView` when calling resolveWebviewView.

## vscode mock — window stubs added
- `window.registerWebviewViewProvider` — added stub `jest.fn(() => ({ dispose: jest.fn() }))`
  Extension.ts uses this for InspectorViewProvider and ConsolePanel.

## jest.fn() type parameter restriction
- `jest.fn<() => Promise<T>>()` causes TS2743 in test files with this project's ts-jest config.
- Use `jest.fn().mockResolvedValue(...)` without type parameters instead.
- The vscode mock file itself uses the 1-arg form — it works there due to its own tsconfig context.

## Key Source Files
- `src/ipc-client.ts` — WebSocket IPC client, auto-reconnect, request/response correlation
- `src/scene-tree-provider.ts` — VSCode TreeDataProvider for scene nodes
- `src/inspector-panel.ts` — InspectorPanel (WebviewPanel) + InspectorViewProvider (WebviewViewProvider)
- `src/console-panel.ts` — ConsolePanel (WebviewViewProvider, LogLevel/LogEntry, MAX_ENTRIES=500)
- `src/play-controls.ts` — Status bar play/pause/stop controls
- `src/server-manager.ts` — ServerManager (process spawn, port wait, status bar)
- `src/stream-panel.ts` — StreamPanel (singleton webview panel via ViewportManager)
