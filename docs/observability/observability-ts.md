# Spec — Observability: TypeScript Extension, N-API Bridge & MCP Server

> **Goal**: End-to-end observability across all three TypeScript layers — VSCode extension, N-API bridge, and MCP server. Every error surfaces in VSCode's UI (Output panel, Problems panel), every build failure shows as a diagnostic, every layer's logs are correlated, and native crashes are detectable from the TS side.

> **Companion**: [observability-cpp.md](observability-cpp.md) covers the C++ runtime (Tracy, spdlog, crash reporter).

---

## The Three Layers

```
┌─ MCP Server (Node.js, stdio/SSE) ───────────────────────────────┐
│  AI tool invocations, scene queries, screenshot capture          │
│  Logs: stderr only (stdout = JSON-RPC transport)                 │
└──────────────────────────────────────────────────────────────────┘
        ↕ JSON-RPC (stdio) or HTTP (SSE)
┌─ VSCode Extension Host (Node.js) ───────────────────────────────┐
│  Extension activation, commands, webview panels, lifecycle       │
│  Logs: LogOutputChannel → Output panel                           │
│  Diagnostics: DiagnosticCollection → Problems panel              │
│  N-API calls: require('.node') → sync/async C++ functions        │
└──────────────────────────────────────────────────────────────────┘
        ↕ N-API (in-process, same V8 isolate)
┌─ C++ Runtime (.node addon) ─────────────────────────────────────┐
│  DX12 rendering, scene management, physics, audio                │
│  Logs: spdlog → NAPISink → extension host callback               │
│  Crashes: SEH → uzengine-crashreport.exe (separate process)     │
└──────────────────────────────────────────────────────────────────┘
```

---

## How Other VSCode Extensions Solve This

### rust-analyzer

**Logging**: Two `OutputChannel`s — "Rust Analyzer Client" (extension logs) and "rust-analyzer LSP Trace" (protocol traffic). Log level controlled via `Developer: Set Log Level` command per-channel. `"rust-analyzer.trace.server": "verbose"` enables full LSP trace.

**Build diagnostics**: Parses `cargo check` JSON output (`--message-format=json`), maps errors to `DiagnosticCollection` → Problems panel with red squiggles in editor. Compiler suggestions become quick-fix code actions.

**Native crash handling**: rust-analyzer is a separate process (LSP), not in-process N-API. If it crashes, the extension detects the process exit and offers to restart. Crash logs go to the output channel.

### C/C++ Extension (ms-vscode.cpptools)

**Build diagnostics**: Provides built-in problem matchers (`$gcc`, `$msCompile`) for tasks.json. Parses MSVC `cl.exe` and GCC/Clang output into diagnostics.

**Native debugging**: `cppvsdbg` (MSVC) and `cppdbg` (GDB/LLDB) debug adapter types. Compound launch configs for simultaneous JS + native debugging.

### ESLint

**Logging**: `LogOutputChannel` (newer versions). Structured log entries with severity. User controls verbosity via `Developer: Set Log Level`.

**Diagnostics**: Full lifecycle management — create diagnostics on lint, clear on file close/fix, update on edit.

---

## The Problem

UzEngine is unique: a **native C++ runtime loaded in-process** via N-API, plus an MCP server for AI agents. This creates observability challenges no other extension faces:

1. **Native crashes kill the extension host** — no try/catch saves you. Electron restarts the host in ~1s, but we need diagnostics from before the crash.
2. **Three logging streams** — C++ (spdlog), Extension (LogOutputChannel), MCP (stderr) — must be correlated for debugging.
3. **Build failures** — cmake-js compiling the .node addon produces MSVC/Clang errors that need to appear as VSCode diagnostics.
4. **Webview panels** have isolated contexts — their errors are invisible unless explicitly forwarded.
5. **MCP stdio transport** — stdout is JSON-RPC. A single `console.log()` corrupts the transport. All MCP logging MUST go to stderr.

---

## UzEngine Approach

### 1. Extension Logging: `LogOutputChannel`

**Decision**: `vscode.window.createOutputChannel('UzEngine', { log: true })` — the native VSCode logging API.

**Why not winston/pino**: `LogOutputChannel` (added VSCode 1.74) provides `trace()`, `debug()`, `info()`, `warn()`, `error()` with built-in level filtering via `Developer: Set Log Level`. Zero dependencies, native Output panel integration, respects user preferences. Winston/pino add bundle size and complexity for features we don't need in the extension host.

```typescript
// extension/src/log.ts
import * as vscode from 'vscode';

// Single shared channel for the extension
export const log = vscode.window.createOutputChannel('UzEngine', { log: true });

// Usage — severity-aware, user-filterable
log.info('Extension activated');
log.debug('Addon loaded from {}', addonPath);
log.warn('GPU adapter not found, falling back to WARP');
log.error('Failed to init DX12 device: {}', error.message);
log.trace('N-API call: renderFrame() returned {} bytes', frame.length);
```

**Multiple channels for high-traffic separation**:

```typescript
export const log      = vscode.window.createOutputChannel('UzEngine', { log: true });
export const logNAPI  = vscode.window.createOutputChannel('UzEngine N-API', { log: true });
export const logMCP   = vscode.window.createOutputChannel('UzEngine MCP', { log: true });
export const logBuild = vscode.window.createOutputChannel('UzEngine Build', { log: true });
```

User controls each channel's level independently via `Developer: Set Log Level → UzEngine N-API → Trace`.

### 2. C++ → Extension Log Bridge (NAPISink)

The C++ runtime logs via `UZ_LOG(Category, Level, ...)` which routes through spdlog. The `NAPISink` (custom spdlog sink) forwards logs to a JS callback registered from the extension:

```
C++ UZ_LOG(LogRHI, Info, "Device: {}", name)
  → spdlog → NAPISink → ThreadSafeFunction callback
  → Extension Host: logNAPI.info('[LogRHI] Device: RTX 4090')
  → Output panel: "UzEngine N-API" channel
```

```typescript
// extension/src/napi-bridge.ts
import { logNAPI } from './log';

export function initAddonLogging(addon: UzEngineAddon): void {
    addon.setLogCallback((level: number, category: string, message: string) => {
        const formatted = `[${category}] ${message}`;
        switch (level) {
            case 0: logNAPI.trace(formatted); break;
            case 1: logNAPI.debug(formatted); break;
            case 2: logNAPI.info(formatted);  break;
            case 3: logNAPI.warn(formatted);  break;
            case 4: logNAPI.error(formatted); break;
            case 5: logNAPI.error(`FATAL: ${formatted}`); break;
        }
    });
}
```

**C++ side** (in NAPISink):

```cpp
// runtime/core/logging/sinks/napi_sink.h
class NAPISink : public spdlog::sinks::base_sink<std::mutex> {
    Napi::ThreadSafeFunction m_tsfn;  // thread-safe call into JS

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        // Format message, call JS callback on main thread
        auto callback = [level = msg.level, cat = category_, msg = formatted](
            Napi::Env env, Napi::Function jsCallback) {
            jsCallback.Call({
                Napi::Number::New(env, static_cast<int>(level)),
                Napi::String::New(env, cat),
                Napi::String::New(env, msg)
            });
        };
        m_tsfn.NonBlockingCall(callback);
    }
};
```

**Key**: `Napi::ThreadSafeFunction` — the C++ render thread can call the JS callback safely. `NonBlockingCall` ensures the render thread never blocks waiting for JS.

### 3. Build Diagnostics: Problems Panel Integration

When cmake-js builds the .node addon, compiler errors must appear in VSCode's Problems panel.

**Two approaches (we use both)**:

#### 3a. tasks.json Problem Matchers (for manual builds)

```jsonc
// .vscode/tasks.json
{
    "label": "UzEngine: Build Runtime",
    "type": "shell",
    "command": "cmake-js build --runtime=electron --runtime-version=39.6.0",
    "problemMatcher": [
        {
            "owner": "uzengine-msvc",
            "pattern": {
                "regexp": "^(.+?)\\((\\d+)\\):\\s+(error|warning)\\s+(C\\d+):\\s+(.+)$",
                "file": 1, "line": 2, "severity": 3, "code": 4, "message": 5
            }
        },
        {
            "owner": "uzengine-clang",
            "pattern": {
                "regexp": "^(.+?):(\\d+):(\\d+):\\s+(error|warning|note):\\s+(.+)$",
                "file": 1, "line": 2, "column": 3, "severity": 4, "message": 5
            }
        }
    ]
}
```

#### 3b. Programmatic DiagnosticCollection (for auto-build on save)

```typescript
// extension/src/build-diagnostics.ts
import * as vscode from 'vscode';

const buildDiags = vscode.languages.createDiagnosticCollection('uzengine-build');

// Regex patterns for compiler output parsing
const MSVC_PATTERN  = /^(.+?)\((\d+)\):\s+(error|warning)\s+(C\d+):\s+(.+)$/;
const CLANG_PATTERN = /^(.+?):(\d+):(\d+):\s+(error|warning|note):\s+(.+)$/;
const ZIG_PATTERN   = /^(.+?):(\d+):(\d+):\s+error:\s+(.+)$/;

export function parseBuildOutput(output: string): void {
    const diagMap = new Map<string, vscode.Diagnostic[]>();

    for (const line of output.split('\n')) {
        let match: RegExpMatchArray | null;
        let file: string, lineNum: number, col: number, severity: string, message: string, code: string | undefined;

        if ((match = line.match(MSVC_PATTERN))) {
            [, file, lineNum as any, severity, code, message] = match;
            col = 0;
        } else if ((match = line.match(CLANG_PATTERN))) {
            [, file, lineNum as any, col as any, severity, message] = match;
        } else if ((match = line.match(ZIG_PATTERN))) {
            [, file, lineNum as any, col as any, message] = match;
            severity = 'error';
        } else continue;

        const diag = new vscode.Diagnostic(
            new vscode.Range(Number(lineNum) - 1, Number(col), Number(lineNum) - 1, 999),
            message,
            severity === 'error' ? vscode.DiagnosticSeverity.Error
                : severity === 'warning' ? vscode.DiagnosticSeverity.Warning
                : vscode.DiagnosticSeverity.Information
        );
        if (code) diag.code = code;
        diag.source = 'uzengine-build';

        const uri = vscode.Uri.file(file);
        const existing = diagMap.get(uri.toString()) || [];
        existing.push(diag);
        diagMap.set(uri.toString(), existing);
    }

    buildDiags.clear();
    for (const [uriStr, diags] of diagMap) {
        buildDiags.set(vscode.Uri.parse(uriStr), diags);
    }
}
```

**Supported compilers**: MSVC `cl.exe`, Clang/GCC, Zig `cc`. Each has a distinct output format — the regex patterns handle all three.

### 4. Native Crash Detection from TS

A .node addon crash kills the extension host. Electron restarts it in ~1s. We need to:

1. **Detect the crash happened** (extension re-activates after restart)
2. **Find crash data** (written by `uzengine-crashreport.exe` to `Saved/Crashes/`)
3. **Surface it to the user** (notification + show crash report)

```typescript
// extension/src/crash-detector.ts
import * as vscode from 'vscode';
import * as fs from 'fs';
import * as path from 'path';

const CRASH_DIR = path.join(/* project root */, 'Saved', 'Crashes');

export async function checkForUnprocessedCrashes(): Promise<void> {
    if (!fs.existsSync(CRASH_DIR)) return;

    const entries = fs.readdirSync(CRASH_DIR, { withFileTypes: true })
        .filter(e => e.isDirectory())
        .sort((a, b) => b.name.localeCompare(a.name)); // newest first

    for (const entry of entries) {
        const crashDir = path.join(CRASH_DIR, entry.name);
        const reportFile = path.join(crashDir, 'crash.json');
        const processedMarker = path.join(crashDir, '.processed');

        if (!fs.existsSync(reportFile) || fs.existsSync(processedMarker)) continue;

        const report = JSON.parse(fs.readFileSync(reportFile, 'utf-8'));
        const action = await vscode.window.showErrorMessage(
            `UzEngine crashed: ${report.exception_message || 'Unknown error'}`,
            'Show Report', 'Open Minidump', 'Dismiss'
        );

        if (action === 'Show Report') {
            const doc = await vscode.workspace.openTextDocument(
                path.join(crashDir, 'crash.txt')
            );
            await vscode.window.showTextDocument(doc);
        } else if (action === 'Open Minidump') {
            // Open .dmp in VS — user can debug offline
            vscode.env.openExternal(
                vscode.Uri.file(path.join(crashDir, 'crash.dmp'))
            );
        }

        // Mark as processed so we don't show again
        fs.writeFileSync(processedMarker, new Date().toISOString());
    }
}
```

**Called during `activate()`** — on every extension start, check for unprocessed crash dirs.

**N-API error boundary** — for non-fatal C++ errors that throw via `Napi::Error`:

```typescript
// extension/src/napi-bridge.ts
export function safeCall<T>(fn: () => T, fallback: T, context: string): T {
    try {
        return fn();
    } catch (err) {
        log.error(`N-API error in ${context}: ${err}`);
        // Surface as notification for critical errors
        if (context === 'init' || context === 'createViewport') {
            vscode.window.showErrorMessage(`UzEngine: ${context} failed — ${err}`);
        }
        return fallback;
    }
}

// Usage
const frame = safeCall(
    () => addon.renderFrame(instanceCount),
    null,
    'renderFrame'
);
```

### 5. MCP Server Logging

**Cardinal rule**: stdout is JSON-RPC. All logging goes to **stderr**.

```typescript
// mcp/src/logger.ts

// NEVER use console.log() in MCP server — it corrupts stdio transport
// console.error() goes to stderr — safe

import pino from 'pino';

export const mcpLog = pino({
    transport: {
        target: 'pino/file',
        options: { destination: 2 } // fd 2 = stderr
    },
    level: process.env.UZ_MCP_LOG_LEVEL || 'info'
});

// Usage
mcpLog.info({ tool: 'create_entity', params: { name: 'player' } }, 'Tool invoked');
mcpLog.error({ err }, 'Tool execution failed');
```

**MCP protocol notifications** — for logs that should appear in the AI client (Claude Desktop, etc.):

```typescript
// mcp/src/server.ts
import { McpServer } from '@modelcontextprotocol/sdk';

server.sendLoggingMessage({
    level: 'info',
    logger: 'uzengine-mcp',
    data: 'Scene loaded: forest_clearing.uzscene'
});
```

**MCP ↔ Extension log bridge**: When MCP runs as a child process of the extension (not standalone), pipe its stderr to the `logMCP` output channel:

```typescript
// extension/src/mcp-bridge.ts
import { spawn } from 'child_process';
import { logMCP } from './log';

const mcpProcess = spawn('node', ['mcp/dist/server.js'], { stdio: ['pipe', 'pipe', 'pipe'] });

mcpProcess.stderr.on('data', (chunk: Buffer) => {
    for (const line of chunk.toString().split('\n').filter(Boolean)) {
        try {
            const entry = JSON.parse(line); // pino JSON
            const msg = `[${entry.logger || 'mcp'}] ${entry.msg}`;
            switch (entry.level) {
                case 10: case 20: logMCP.debug(msg); break;
                case 30: logMCP.info(msg);  break;
                case 40: logMCP.warn(msg);  break;
                case 50: case 60: logMCP.error(msg); break;
                default: logMCP.info(msg);
            }
        } catch {
            logMCP.info(line); // non-JSON line, log as-is
        }
    }
});
```

### 6. End-to-End Log Correlation

Every operation that crosses layer boundaries gets a **correlation ID**:

```
[EXT:op-a7f3] User command: openScene("level1.uzscene")
[NAPI:op-a7f3] addon.loadScene("level1.uzscene") called
[RT:op-a7f3] [LogScene] Loading scene: level1.uzscene
[RT:op-a7f3] [LogRHI] Creating pipeline state for material: stone_floor
[RT:op-a7f3] [LogScene] Scene loaded: 142 entities, 38 materials
[NAPI:op-a7f3] loadScene completed in 487ms
[EXT:op-a7f3] Scene ready, updating hierarchy panel
[MCP:op-a7f3] Notifying AI: scene_loaded { entities: 142 }
```

```typescript
// extension/src/correlation.ts
let nextId = 0;

export function correlationId(): string {
    return `op-${(nextId++).toString(36).padStart(4, '0')}`;
}

// Usage in a command handler
const opId = correlationId();
log.info(`[${opId}] User command: openScene("${scenePath}")`);
logNAPI.info(`[${opId}] addon.loadScene("${scenePath}") called`);
const result = addon.loadScene(scenePath, opId); // opId passed to C++ → appears in UZ_LOG
logNAPI.info(`[${opId}] loadScene completed in ${result.elapsed_ms}ms`);
```

C++ side receives the correlation ID and includes it in all `UZ_LOG` calls for that operation:

```cpp
// Called from N-API with correlation ID
void LoadScene(const std::string& path, const std::string& opId) {
    UZ_LOG(LogScene, Info, "[{}] Loading scene: {}", opId, path);
    // ... all subsequent logs in this call chain include opId
}
```

### 7. Webview Error Forwarding

Webview panels (inspector, hierarchy, asset browser) run in isolated browser contexts. Their errors are invisible unless forwarded:

```typescript
// Inside webview HTML/JS
window.onerror = (msg, source, line, col, error) => {
    vscode.postMessage({
        type: 'webview-error',
        data: { message: msg, source, line, col, stack: error?.stack }
    });
};

window.onunhandledrejection = (event) => {
    vscode.postMessage({
        type: 'webview-error',
        data: { message: event.reason?.message || String(event.reason), stack: event.reason?.stack }
    });
};
```

```typescript
// Extension side — receive and log
panel.webview.onDidReceiveMessage((msg) => {
    if (msg.type === 'webview-error') {
        log.error(`[Webview:${panelId}] ${msg.data.message}`);
        if (msg.data.stack) log.debug(`[Webview:${panelId}] ${msg.data.stack}`);
    }
});
```

### 8. Debug Launch Configurations

**Compound config** for simultaneous TS + native debugging:

```jsonc
// .vscode/launch.json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Run Extension",
            "type": "extensionHost",
            "request": "launch",
            "args": ["--extensionDevelopmentPath=${workspaceFolder}/extension"],
            "outFiles": ["${workspaceFolder}/extension/dist/**/*.js"],
            "sourceMaps": true
        },
        {
            "name": "Attach Native Debugger",
            "type": "cppvsdbg",
            "request": "attach",
            "processId": "${command:pickProcess}",
            // Filter to extension host process
            "symbolSearchPath": "${workspaceFolder}/build/Release",
            "sourceFileMap": {
                "/build": "${workspaceFolder}"
            }
        },
        {
            "name": "Debug MCP Server",
            "type": "node",
            "request": "launch",
            "program": "${workspaceFolder}/mcp/dist/server.js",
            "args": ["--stdio"],
            "sourceMaps": true,
            "outFiles": ["${workspaceFolder}/mcp/dist/**/*.js"]
        }
    ],
    "compounds": [
        {
            "name": "Extension + Native",
            "configurations": ["Run Extension", "Attach Native Debugger"],
            "stopAll": true
        }
    ]
}
```

**Workflow**:
1. Launch "Extension + Native" compound
2. Extension host starts → TS breakpoints work immediately
3. When addon loads, pick the extension host PID for native attach
4. Now both TS and C++ breakpoints work simultaneously

**Important**: Build the .node addon with debug symbols (`cmake-js build --debug` or `CMAKE_BUILD_TYPE=RelWithDebInfo`).

### 9. Error Telemetry

**Decision**: `@vscode/extension-telemetry` for opt-in production telemetry. NO Sentry in the extension (global state pollution, captures other extensions' errors).

```typescript
// extension/src/telemetry.ts
import TelemetryReporter from '@vscode/extension-telemetry';

let reporter: TelemetryReporter;

export function initTelemetry(context: vscode.ExtensionContext): void {
    // Application Insights connection string
    reporter = new TelemetryReporter(AI_CONNECTION_STRING);
    context.subscriptions.push(reporter);
}

// Feature usage
reporter.sendTelemetryEvent('command/openScene', { sceneType: 'level' });

// Error (auto-respects telemetry opt-out)
reporter.sendTelemetryErrorEvent('napi/crash', {
    exception_message: report.exception_message,
    gpu_adapter: report.system_info.adapter
});

// Exception with stack
reporter.sendTelemetryException(error, { context: 'renderFrame' });
```

**package.json** — declare telemetry so VSCode surfaces it in settings:

```jsonc
{
    "telemetry": {
        "key": "uzengine-telemetry"
    }
}
```

**Sentry for webview panels only** (isolated context, safe):

```typescript
// Inside webview — @sentry/browser is safe here
import * as Sentry from '@sentry/browser';
Sentry.init({ dsn: '...', environment: 'development' });
```

### 10. Extension Host Crash Safety

`process.on('uncaughtException')` and `process.on('unhandledRejection')` fire in the extension host BUT:
- An unhandled rejection **crashes ALL extensions**, not just ours
- Multiple extensions registering listeners causes memory leak warnings

**Our approach**: Never rely on these. Use proper try/catch everywhere. Register a **last-resort** handler for telemetry only:

```typescript
// extension/src/activate.ts
export function activate(context: vscode.ExtensionContext) {
    // Last-resort handler — log before death, don't try to recover
    process.on('uncaughtException', (err) => {
        log.error(`Uncaught exception (extension host will crash): ${err.stack}`);
        reporter?.sendTelemetryException(err, { fatal: 'true' });
    });

    process.on('unhandledRejection', (reason) => {
        log.error(`Unhandled rejection: ${reason}`);
        // Do NOT rethrow — let the host handle it
    });
}
```

---

## Build Configuration Matrix

| Build Type | Extension Logging | N-API Log Bridge | Build Diagnostics | Crash Detection | Telemetry | MCP Logging |
|-----------|------------------|-----------------|-------------------|-----------------|-----------|-------------|
| **Development** | All levels (trace+) | Full (all C++ logs forwarded) | Full (MSVC+Clang+Zig parsers) | Full (show dialog) | Disabled | pino debug to stderr |
| **Production** | Info+ | Warn+ only (reduce noise) | Full | Full (offer upload) | Opt-in (`@vscode/extension-telemetry`) | pino info to stderr |
| **CI/Test** | Warn+ | Error+ only | Full (exit code on error) | Headless (auto-report) | Disabled | pino warn to stderr |

---

## File Structure

```
extension/src/
├── log.ts                     # LogOutputChannel instances (log, logNAPI, logMCP, logBuild)
├── correlation.ts             # Correlation ID generator for cross-layer tracing
├── napi-bridge.ts             # N-API call wrappers with error boundary + log callback init
├── crash-detector.ts          # Check Saved/Crashes/ on activate, surface unprocessed reports
├── build-diagnostics.ts       # Parse MSVC/Clang/Zig output → DiagnosticCollection → Problems panel
├── telemetry.ts               # @vscode/extension-telemetry wrapper
└── webview/
    └── error-forwarder.ts     # Webview error → postMessage → extension log

mcp/src/
├── logger.ts                  # pino to stderr (NEVER stdout)
└── server.ts                  # MCP server with notifications/message for AI client logs

.vscode/
├── launch.json                # Compound: Extension + Native debugger
└── tasks.json                 # Build task with MSVC/Clang/Zig problem matchers
```

---

## Deliverables

| Task | Acceptance Criteria |
|------|-------------------|
| `LogOutputChannel` setup | 4 channels visible in Output panel (UzEngine, N-API, MCP, Build) |
| N-API log bridge (NAPISink) | C++ `UZ_LOG` messages appear in "UzEngine N-API" output channel |
| Build diagnostics | cmake-js MSVC errors appear in Problems panel with correct file:line |
| Clang/Zig build diagnostics | Clang and Zig compiler errors also parsed correctly |
| Crash detection | After native crash + restart, notification offers to show crash report |
| Correlation IDs | Cross-layer operation logs share the same `op-XXXX` ID |
| Webview error forwarding | Webview JS errors appear in extension log |
| MCP stderr logging | MCP logs appear in "UzEngine MCP" channel, stdout stays clean |
| MCP notifications | AI client sees `notifications/message` for important events |
| Compound debug config | TS + C++ breakpoints work simultaneously |
| Problem matchers | tasks.json build task parses MSVC + Clang + Zig errors |
| Telemetry opt-in | `@vscode/extension-telemetry` reports errors when user opts in |
| Extension host safety | Unhandled exceptions logged before crash, no recovery attempt |

---

## Dependencies

- **NAPISink requires**: [observability-cpp.md](observability-cpp.md) spdlog + NAPISink implementation
- **Crash detection requires**: [observability-cpp.md](observability-cpp.md) crash reporter writing to `Saved/Crashes/`
- **Build diagnostics requires**: cmake-js build pipeline functional
- **MCP logging requires**: 3.0 (MCP Integration) — MCP server exists
- **Telemetry requires**: published extension (connection string from Azure App Insights)

---

## Third-Party Libraries

| Library | Layer | Purpose | License |
|---------|-------|---------|---------|
| **pino** | MCP Server | Structured JSON logging to stderr | MIT |
| **@vscode/extension-telemetry** | Extension | Opt-in error/usage telemetry (Azure App Insights) | MIT |
| **@sentry/browser** | Webview only | Error capture in isolated webview contexts | MIT |

**NOT using**: winston (unnecessary weight), @sentry/node in extension (global state pollution, captures other extensions' errors — [getsentry/sentry-javascript#3564](https://github.com/getsentry/sentry-javascript/issues/3564)), pino in extension host (LogOutputChannel is better).

---

*Reference: [observability-cpp.md](observability-cpp.md) | [ARCHITECTURE.md](../ARCHITECTURE.md) | [2.0-napi-foundation.md](../specs/2.0-napi-foundation.md) | [3.0-mcp-integration.md](../specs/3.0-mcp-integration.md)*
