# UzEngine VSCode Extension — Reference

## Overview

The UzEngine VSCode extension manages the engine process lifecycle and displays the live JPEG stream in a webview panel.

**Location:** `vscode-uzengine/`

**Entry point:** `src/extension.ts`

---

## Commands

### uzengine.openStreamViewer

Opens the stream viewer and starts the server if not running.

**Activation:** Command palette or keybinding

**Flow:**
1. Check if server is already running
2. If not, spawn `UzEngine.exe` (headless mode)
3. Poll port 9002 until reachable (5 second timeout)
4. Create/show the webview panel
5. Panel connects to WebSocket and displays stream

**Keyboard shortcut:** (configurable in `keybindings.json`)

### uzengine.startServer

Starts the server without opening the panel.

**Use case:** Prepare engine for external tools or pre-warm before opening panel

**If already running:** No-op

### uzengine.stopServer

Kills the `UzEngine.exe` process.

**Effect:** Webview disconnects from WebSocket; shows "Disconnected" status.

**Manual restart:** Run `uzengine.openStreamViewer` again

---

## Configuration

### uzengine.serverUrl

WebSocket URL of the running engine.

**Default:** `ws://localhost:9002`

**Example configuration (settings.json):**
```json
{
  "uzengine.serverUrl": "ws://127.0.0.1:9002"
}
```

**Use case:** Custom hostname or port (if running engine elsewhere)

### uzengine.serverExecutable

Absolute path to `UzEngine.exe`. Auto-discovered if not set.

**Default:** Auto-search in:
1. `<workspace>/build/Release/UzEngine.exe`
2. `<workspace>/build/Debug/UzEngine.exe`
3. `<workspace>/build/UzEngine.exe`

**Example configuration (settings.json):**
```json
{
  "uzengine.serverExecutable": "C:\\custom\\path\\UzEngine.exe"
}
```

**Use case:** Custom build location or pre-built binary

---

## Architecture

### Server Manager (src/server-manager.ts)

Manages the `UzEngine.exe` child process lifecycle.

```typescript
export class ServerManager implements vscode.Disposable {
    private _process: cp.ChildProcess | undefined;
    private _outputChannel: vscode.OutputChannel;
    private _statusBar: vscode.StatusBarItem;

    async start(): Promise<boolean>;
    stop(): void;
    dispose(): void;
}
```

**Responsibilities:**
- Resolve executable path (setting → auto-search)
- Spawn process with environment: `UZENGINE_HEADLESS=1`
- Capture stdout/stderr → output channel
- Poll port 9002 until reachable
- Handle process exit/error
- Display status in status bar

**Port polling (TCP):**
```typescript
private _waitForPort(port: number, timeoutMs: number): Promise<boolean> {
    // Attempt TCP connect every 100ms
    // Return true if port reachable before timeout
    // Used to detect when engine is ready
}
```

**Status bar:**
- **Running:** `$(debug-stop) UzEngine: Release/UzEngine.exe` (clickable, toggles stop)
- **Stopped:** `$(play) UzEngine: stopped`

### Stream Panel (src/stream-panel.ts)

Manages the singleton webview panel displaying the JPEG stream.

```typescript
export class StreamPanel {
    private static currentPanel: StreamPanel | undefined;
    private _panel: vscode.WebviewPanel;

    public static createOrShow(extensionUri: vscode.Uri): void;
    public dispose(): void;
}
```

**Singleton pattern:**
- Only one panel at a time
- Re-invoking the command reveals existing panel (no duplicate)

**Webview lifecycle:**
- `retainContextWhenHidden: true` — WebSocket stays alive across tab switches
- `onDidDispose` event → cleanup
- `onDidChangeViewState` → resume message (forward compatibility)

**Content Security Policy (CSP):**
```html
<meta http-equiv="Content-Security-Policy"
      content="default-src 'none';
               connect-src ws://localhost:*;
               img-src blob: data: vscode-webview:;
               script-src 'nonce-...';">
```

- No inline scripts without nonce
- No external resources
- WebSocket only to localhost
- img src allows data: URLs (used for JPEG base64 data URLs)

---

## Webview Script

The webview HTML includes an inlined JavaScript script that manages WebSocket connection and JPEG display.

### Connection Management

```javascript
var ws = new WebSocket(SERVER_URL);
ws.binaryType = 'arraybuffer';

ws.addEventListener('open', () => {
    setStatus('Connected', 'connected');
    reconnectDelay = 1000;  // reset backoff
});

ws.addEventListener('error', (e) => {
    console.error('[UzEngine] WebSocket error:', e);
    setStatus('Reconnecting…', 'disconnected');
});

ws.addEventListener('close', () => {
    setStatus('Reconnecting…', 'disconnected');
    scheduleReconnect();
});
```

**Reconnection strategy:**
- Initial delay: 1 second
- Double on each retry: 2s, 4s, 8s, 16s
- Cap at 16 seconds

### JPEG Display Pipeline

```javascript
ws.addEventListener('message', function(e) {
    if (!(e.data instanceof ArrayBuffer)) {
        console.warn('[UzEngine] Received non-binary message');
        return;
    }

    // 1. Convert ArrayBuffer to Uint8Array
    var bytes = new Uint8Array(e.data);

    // 2. Convert bytes to binary string (for btoa)
    var binary = '';
    for (var i = 0; i < bytes.byteLength; i++) {
        binary += String.fromCharCode(bytes[i]);
    }

    // 3. Base64 encode
    var base64 = btoa(binary);

    // 4. Create data URL
    var dataUrl = 'data:image/jpeg;base64,' + base64;

    // 5. Display
    document.getElementById('stream').src = dataUrl;

    // 6. Update FPS counter
    frameCount++;
    if (now - lastFpsTick >= 1000) {
        fpsEl.textContent = frameCount + ' fps';
        frameCount = 0;
        lastFpsTick = now;
    }
});
```

**Why base64 data URLs?**
- VSCode webview CSP restricts blob: URLs (MSE would require blob)
- data: URLs are allowed via CSP `img-src data:`
- Browser automatically decodes base64 → JPEG → displays
- No temporary files, no resource restrictions

### UI Components

| Element | Purpose |
|---------|---------|
| `#toolbar` | Top bar with status, FPS, server URL |
| `#status` | Connection state (Connected/Disconnected/Connecting) |
| `#fps` | Frames per second counter (client-side) |
| `#stream-container` | Flex container centering the image |
| `#stream` | `<img>` element displaying JPEG frames |
| `#diag` | Diagnostic message (hidden after first frame) |

**Styling:**
- Dark theme (matching VSCode default)
- Monospace font (Consolas)
- Color-coded status (green for connected, red for disconnected, yellow for connecting)
- `object-fit: contain` — maintains aspect ratio

---

## Extension Lifecycle

### Activation

```typescript
export function activate(context: vscode.ExtensionContext): void {
    serverManager = new ServerManager();
    context.subscriptions.push(serverManager);

    const openCommand = vscode.commands.registerCommand(
        'uzengine.openStreamViewer',
        async () => {
            const started = await serverManager!.start();
            if (started) {
                StreamPanel.createOrShow(context.extensionUri);
            }
        }
    );
    // ... register other commands
    context.subscriptions.push(openCommand, startCommand, stopCommand);
}
```

**Triggered when:**
- First command is run
- Extension dependency is activated

**Setup:**
- Create ServerManager singleton
- Register all commands
- Register disposables

### Deactivation

```typescript
export function deactivate(): void {
    // ServerManager is disposed automatically via context.subscriptions
}
```

VSCode automatically calls `dispose()` on all registered subscriptions, killing the process.

---

## Extension Settings (package.json)

```json
{
  "contributes": {
    "commands": [
      {
        "command": "uzengine.openStreamViewer",
        "title": "UzEngine: Open Stream Viewer"
      },
      {
        "command": "uzengine.startServer",
        "title": "UzEngine: Start Server"
      },
      {
        "command": "uzengine.stopServer",
        "title": "UzEngine: Stop Server"
      }
    ],
    "configuration": [
      {
        "title": "UzEngine",
        "properties": {
          "uzengine.serverUrl": {
            "type": "string",
            "default": "ws://localhost:9002",
            "description": "WebSocket URL of the UzEngine server"
          },
          "uzengine.serverExecutable": {
            "type": "string",
            "default": "",
            "description": "Absolute path to UzEngine.exe (auto-discovered if empty)"
          }
        }
      }
    ]
  }
}
```

---

## Debugging the Extension

### Launch in Debug Mode

1. Open `vscode-uzengine/` in VSCode
2. Press **F5** (or Run → Start Debugging)
3. A new VSCode window opens with the extension loaded

**VSCode debug configurations** (`.vscode/launch.json`):
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Extension",
      "type": "extensionHost",
      "request": "launch",
      "args": ["--extensionDevelopmentPath=${workspaceFolder}"],
      "preLaunchTask": "npm: compile"
    }
  ]
}
```

### Debug Console

Debug window output channel:
- `Extensions: UzEngine` output channel in debug VSCode window
- Shows `console.log()` output from the extension

### Breakpoints

1. Set breakpoints in `src/extension.ts`, `src/server-manager.ts`, `src/stream-panel.ts`
2. Run the extension (F5)
3. VSCode debugger attaches and breaks at breakpoints

### Reloading

After editing source:
- **Automatic:** esbuild watch recompiles (if using watch mode)
- **Manual:** **Ctrl+Shift+F5** in debug window to reload extension

---

## Output Channel

The extension creates an output channel named "UzEngine Server" showing:
- Process startup messages
- stdout/stderr from `UzEngine.exe`
- Process exit status

**View:** VSCode → Output panel → select "UzEngine Server"

**Example log:**
```
[UzEngine] Starting: C:\path\to\build\Release\UzEngine.exe
[UzEngine/WS] WebSocket server listening on port 9002
[UzEngine] Server exited — code: 0 signal: null
```

---

## Common Issues

### Extension doesn't activate

**Check:**
1. VSCode extension discovery: Extensions panel → should show "UzEngine"
2. Activation events in `package.json`

**Cause:** Syntax error in `package.json` or `extension.ts`

**Fix:**
```bash
cd vscode-uzengine
npm run compile  # Check for TypeScript errors
```

### "UzEngine.exe not found"

**Cause:** Executable not in expected location.

**Fix:**
1. Build the C++ app: `cmake --build build --config Release`
2. Or set `uzengine.serverExecutable` in VSCode settings

### WebSocket connection fails

**Cause:** Engine not running or listening on wrong port.

**Check:**
1. Is UzEngine.exe running? (Check Task Manager)
2. Is port 9002 open? (`netstat -ano | findstr :9002`)
3. Check UzEngine server output channel

**Fix:**
- Manually start engine: `.\build\Release\UzEngine.exe` (with `UZENGINE_WINDOWED=1`)
- Check firewall (localhost should not require firewall rule)

### Webview blank / showing "Disconnected"

**Possible causes:**
1. Engine crashed after process.spawn()
2. Engine failed to start WebSocket server
3. Firewall blocking localhost connection

**Debug:**
1. Check "UzEngine Server" output channel
2. Check Windows Event Viewer for process crashes
3. Try `uzengine.stopServer` then `uzengine.openStreamViewer`

---

## Testing the Extension

### Manual Testing Checklist

- [ ] Start VSCode with extension loaded (F5 in `vscode-uzengine/`)
- [ ] Run `UzEngine: Open Stream Viewer` command
- [ ] Verify WebSocket "Connecting..." status appears
- [ ] Verify "Connected" status and FPS counter appear
- [ ] See live JPEG stream updating
- [ ] Run `UzEngine: Stop Server` → webview shows "Disconnected"
- [ ] Run `UzEngine: Start Server` → reconnects automatically
- [ ] Close webview panel → process still running
- [ ] Close VSCode → process killed

### Extension Build Test

```bash
cd vscode-uzengine
npm run compile
npm run test  # (if tests exist)
npm run package  # produces .vsix file
```

### Package Installation

```bash
# Install the .vsix into your VSCode
code --install-extension ./uzengine-0.1.0.vsix

# Uninstall
code --uninstall-extension uzmigames.uzengine
```

---

## Further Reading

- [VSCode Extension API](https://code.visualstudio.com/api)
- [Webview API](https://code.visualstudio.com/api/extension-guides/webview)
- [Content Security Policy](https://code.visualstudio.com/api/extension-guides/webview#content-security-policy)
- [TypeScript Handbook](https://www.typescriptlang.org/docs/)
