# Output Panel — Logging and Console Integration

> **Status**: Planned (Phase 2–3)
> **Source**: `extension/editor/output.ts`, `engine/core/logging.h`, `engine/editor/ipc_logging.h`
> **References**: Logging System (§13), IPC/MCP (§41), VSCode Output Channel API, Unity Console

## Purpose

The Output panel displays runtime logs from the UzEngine in a searchable, filterable output window. Rather than a custom panel, it leverages VSCode's native Output Channel API, which provides excellent performance, native search, problem matcher integration, and clickable file:line links. Log messages from C++ (via UZ_LOG) are forwarded to the extension via IPC, then written to the Output Channel. Users can filter by category and level, search for text, and quickly navigate to source code.

## Architecture

```
Logging Data Flow
    │
    ├─ C++ Runtime (uz_engine.lib)
    │  │
    │  └─ UZ_LOG(Category, Level, Format, ...)
    │     │
    │     └─ Spdlog → category sinks
    │        │
    │        └─ IPC bridge forwards logs
    │           │
    │           └─ On IPC::LogMessage() call
    │
    ├─ N-API Extension Layer
    │  │
    │  └─ Receives log messages via IPC
    │     │
    │     └─ Writes to VSCode OutputChannel
    │
    └─ VSCode Output Panel
       │
       ├─ Display (scrollable, searchable)
       ├─ Filter by category/level
       ├─ Problem matcher (error highlighting)
       └─ Clickable file:line links
```

## Logging System (C++)

The runtime uses spdlog for structured logging. The extension subscribes to a log sink and forwards messages via IPC.

```cpp
/// Log category declaration (used in .h files).
UZ_DECLARE_LOG_CATEGORY(Engine);
UZ_DECLARE_LOG_CATEGORY(Rendering);
UZ_DECLARE_LOG_CATEGORY(Physics);
UZ_DECLARE_LOG_CATEGORY(Audio);
UZ_DECLARE_LOG_CATEGORY(Scripting);
UZ_DECLARE_LOG_CATEGORY(Asset);

/// Log category definition (used once in .cpp file).
UZ_DEFINE_LOG_CATEGORY(Engine);
UZ_DEFINE_LOG_CATEGORY(Rendering);

/// Log level enumeration.
enum class LogLevel : uint8_t {
    Trace = 0,      // Verbose diagnostic info
    Debug = 1,      // Debug info during development
    Info = 2,       // General information
    Warning = 3,    // Warning (should investigate)
    Error = 4,      // Error (something failed)
    Fatal = 5,      // Fatal error (engine crash)
};

/// Structured log message.
struct LogMessage {
    std::string category;           // "Rendering", "Physics", etc.
    LogLevel level;
    std::string text;               // Formatted message
    std::string file;               // Source file (__FILE__ if available)
    int32_t line;                   // Line number (__LINE__)
    int64_t timestamp;              // Unix milliseconds
    std::thread::id thread_id;      // Which thread logged
};

/// Public logging API (used everywhere).
#define UZ_LOG(category, level, fmt, ...) \
    ::uz::GetLogger().Log<category>(level, __FILE__, __LINE__, fmt, __VA_ARGS__)

// Examples:
UZ_LOG(Engine, Info, "Initializing engine version {}", "0.2.1");
UZ_LOG(Rendering, Warning, "GPU memory high: {:.1f}% used", gpu_usage);
UZ_LOG(Physics, Error, "Failed to load physics scene: {}", error_msg);

/// Logger interface (singleton, internal use).
class Logger {
public:
    /// Log a message to the category-specific sink.
    template <typename Category>
    void Log(LogLevel level, const char* file, int line, std::string_view format, auto&&... args) {
        const auto& sink = GetSinkForCategory(Category::Name);
        const auto msg = fmt::format(format, std::forward<decltype(args)>(args)...);
        LogMessage log_msg{
            .category = std::string(Category::Name),
            .level = level,
            .text = msg,
            .file = file,
            .line = line,
            .timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count(),
            .thread_id = std::this_thread::get_id(),
        };
        sink->Log(log_msg);
    }

    /// Register IPC sink (called by editor during init).
    void RegisterIPCSink(std::shared_ptr<IPCLoggingSink> sink);
};

/// Sink that forwards logs via IPC to extension.
class IPCLoggingSink : public LogSink {
public:
    void Log(const LogMessage& msg) override {
        // Serialize and send via IPC::SendAsync
        ipc_->SendAsync("editor.on_log", {
            { "category", msg.category },
            { "level", static_cast<int>(msg.level) },
            { "text", msg.text },
            { "file", msg.file },
            { "line", msg.line },
            { "timestamp", msg.timestamp },
        });
    }
};
```

## Output Panel (TypeScript)

```typescript
/// Output panel wrapper around VSCode OutputChannel.
export class OutputPanel {
    private outputChannel: vscode.OutputChannel;
    private logBuffer: LogMessage[] = [];
    private categoryFilters: Map<string, boolean> = new Map();
    private levelFilter: LogLevel = LogLevel.Trace;
    private searchText: string = '';

    constructor(private context: vscode.ExtensionContext, private ipc: IPC) {
        this.outputChannel = vscode.window.createOutputChannel('UzEngine', { log: true });
        this.setupUI();
        this.attachEventListeners();
    }

    private setupUI() {
        // Create webview panel for filter controls
        this.filterPanel = vscode.window.createWebviewPanel(
            'uz-log-filters',
            'UzEngine Filters',
            vscode.ViewColumn.Two,
            { enableScripts: true }
        );

        this.filterPanel.webview.html = `
            <!DOCTYPE html>
            <html>
            <head>
                <style>
                    body { background: #252526; color: #cccccc; font-family: Segoe UI, sans-serif; }
                    .filter-panel { padding: 12px; }
                    .filter-group { margin: 12px 0; }
                    .filter-group label { display: block; margin: 4px 0; font-size: 12px; }
                    input[type="checkbox"] { margin-right: 6px; cursor: pointer; }
                    select { padding: 6px; background: #3e3e42; border: 1px solid #444; color: #cccccc; border-radius: 2px; }
                    .search-box { width: 100%; padding: 6px; background: #3e3e42; border: 1px solid #444; color: #cccccc; margin: 8px 0; }
                </style>
            </head>
            <body>
                <div class="filter-panel">
                    <div class="filter-group">
                        <label>Log Level:</label>
                        <select id="level-filter" onchange="vscode.postMessage({command: 'setLevel', value: this.value})">
                            <option value="0">Trace</option>
                            <option value="1">Debug</option>
                            <option value="2" selected>Info</option>
                            <option value="3">Warning</option>
                            <option value="4">Error</option>
                            <option value="5">Fatal</option>
                        </select>
                    </div>

                    <div class="filter-group">
                        <label>Categories:</label>
                        <label><input type="checkbox" id="engine-filter" checked onchange="vscode.postMessage({command: 'setCategory', category: 'Engine', value: this.checked})"> Engine</label>
                        <label><input type="checkbox" id="rendering-filter" checked onchange="vscode.postMessage({command: 'setCategory', category: 'Rendering', value: this.checked})"> Rendering</label>
                        <label><input type="checkbox" id="physics-filter" checked onchange="vscode.postMessage({command: 'setCategory', category: 'Physics', value: this.checked})"> Physics</label>
                        <label><input type="checkbox" id="audio-filter" checked onchange="vscode.postMessage({command: 'setCategory', category: 'Audio', value: this.checked})"> Audio</label>
                        <label><input type="checkbox" id="scripting-filter" checked onchange="vscode.postMessage({command: 'setCategory', category: 'Scripting', value: this.checked})"> Scripting</label>
                        <label><input type="checkbox" id="asset-filter" checked onchange="vscode.postMessage({command: 'setCategory', category: 'Asset', value: this.checked})"> Asset</label>
                    </div>

                    <div class="filter-group">
                        <input type="text" class="search-box" placeholder="Search logs..." onkeyup="vscode.postMessage({command: 'search', text: this.value})">
                    </div>
                </div>
            </body>
            </html>
        `;

        this.filterPanel.webview.onDidReceiveMessage((msg) => {
            if (msg.command === 'setLevel') {
                this.levelFilter = parseInt(msg.value);
                this.refreshOutput();
            } else if (msg.command === 'setCategory') {
                this.categoryFilters.set(msg.category, msg.value);
                this.refreshOutput();
            } else if (msg.command === 'search') {
                this.searchText = msg.text;
                this.refreshOutput();
            }
        });

        // Initialize category filters (all enabled by default)
        this.categoryFilters.set('Engine', true);
        this.categoryFilters.set('Rendering', true);
        this.categoryFilters.set('Physics', true);
        this.categoryFilters.set('Audio', true);
        this.categoryFilters.set('Scripting', true);
        this.categoryFilters.set('Asset', true);
    }

    private attachEventListeners() {
        // Subscribe to log messages from IPC
        this.ipc.on('editor.on_log', (msg: LogMessage) => {
            this.logBuffer.push(msg);

            // Apply filters
            if (this.passesFilters(msg)) {
                this.outputChannel.appendLine(this.formatLogMessage(msg));
            }
        });

        // Register VSCode command for "Open in Editor" (from problem matcher)
        vscode.commands.registerCommand('uz-engine.openLogSource', async (file: string, line: number) => {
            const uri = vscode.Uri.file(file);
            const editor = await vscode.window.showTextDocument(uri);
            const lineNum = Math.max(0, line - 1);
            editor.selection = new vscode.Selection(lineNum, 0, lineNum, 0);
            editor.revealRange(new vscode.Range(lineNum, 0, lineNum, 0));
        });
    }

    private passesFilters(msg: LogMessage): boolean {
        // Check category filter
        if (!this.categoryFilters.get(msg.category)) {
            return false;
        }

        // Check level filter
        if (msg.level < this.levelFilter) {
            return false;
        }

        // Check search text
        if (this.searchText && !msg.text.toLowerCase().includes(this.searchText.toLowerCase())) {
            return false;
        }

        return true;
    }

    private formatLogMessage(msg: LogMessage): string {
        const timestamp = new Date(msg.timestamp).toLocaleTimeString();
        const levelName = ['TRACE', 'DEBUG', 'INFO', 'WARN', 'ERROR', 'FATAL'][msg.level];
        const color = this.getLevelColor(msg.level);

        // Format: [HH:MM:SS] [LEVEL] [Category] Message (file:line)
        let line = `[${timestamp}] [${levelName}] [${msg.category}] ${msg.text}`;
        if (msg.file && msg.line > 0) {
            line += ` (${msg.file}:${msg.line})`;
        }

        return line;
    }

    private getLevelColor(level: LogLevel): string {
        // VSCode log color codes
        switch (level) {
            case LogLevel.Trace:
            case LogLevel.Debug:
                return '\x1b[90m';  // Gray
            case LogLevel.Info:
                return '\x1b[37m';  // White
            case LogLevel.Warning:
                return '\x1b[33m';  // Yellow
            case LogLevel.Error:
            case LogLevel.Fatal:
                return '\x1b[31m';  // Red
        }
    }

    private refreshOutput() {
        this.outputChannel.clear();
        for (const msg of this.logBuffer) {
            if (this.passesFilters(msg)) {
                this.outputChannel.appendLine(this.formatLogMessage(msg));
            }
        }
    }

    show() {
        this.outputChannel.show();
    }

    clear() {
        this.outputChannel.clear();
        this.logBuffer = [];
    }
}
```

## IPC Logging Handler (C++)

In the runtime, register the IPC logging sink during initialization:

```cpp
/// Called during engine startup (in editor mode).
void EditorSystem::InitializeLogging() {
    auto ipc_sink = std::make_shared<IPCLoggingSink>(ipc_);
    Logger::Get().RegisterIPCSink(ipc_sink);

    UZ_LOG(Engine, Info, "Editor logging initialized");
}

/// Example: some system logs during runtime
void RenderingSystem::Init() {
    UZ_LOG(Rendering, Info, "Initializing D3D12 backend");
    UZ_LOG(Rendering, Info, "GPU: {}", adapter_name);
    UZ_LOG(Rendering, Info, "VRAM: {:.1f} GB", vram_mb / 1024.0f);
}

void PhysicsSystem::SimulateFrame() {
    if (bodies_simulated > 1000) {
        UZ_LOG(Physics, Warning, "High body count: {} (consider LOD)", bodies_simulated);
    }

    if (performance_issue_detected) {
        UZ_LOG(Physics, Warning, "Physics frame took {:.2f}ms (budget: 8ms)", frame_time_ms);
    }
}
```

## Problem Matcher Integration

VSCode's problem matcher can parse error logs and add them to the Problems panel:

```json
// In .vscode/tasks.json or extension package.json
{
    "problemMatcher": {
        "name": "uz-engine",
        "pattern": {
            "regexp": "^\\[.*\\]\\s+\\[(ERROR|FATAL)\\]\\s+\\[(.*)\\]\\s+(.*)\\s+\\((.+):(\\d+)\\)$",
            "file": 4,
            "location": 5,
            "message": 3,
            "severity": 1
        }
    }
}
```

This enables:
- Errors appear in VSCode's Problems panel
- Click error → open file at line
- Integrated into "Go to Next Problem" (F8)
- Error gutter markers in editor

## Log Level Color Codes

| Level | Color | Use Case |
|-------|-------|----------|
| **TRACE** | Gray | Very verbose diagnostic info (disabled by default) |
| **DEBUG** | Gray | Debug information during development |
| **INFO** | White | General information, asset loads, system init |
| **WARNING** | Yellow | Something unusual (missing asset, high perf cost, deprecated API) |
| **ERROR** | Red | Something failed (compilation error, I/O failure, invalid state) |
| **FATAL** | Red (bold) | Engine crash imminent (corrupted state, assertion failure) |

## Log Category Guidelines

Developers creating new systems should declare and use categories:

```cpp
// In myfeature/myfeature.h
UZ_DECLARE_LOG_CATEGORY(MyFeature);

// In myfeature/myfeature.cpp
UZ_DEFINE_LOG_CATEGORY(MyFeature);

void MyFeatureSystem::Load() {
    UZ_LOG(MyFeature, Info, "Loading configuration...");
    // ... work ...
    UZ_LOG(MyFeature, Info, "Load complete");
}
```

Categories are used to:
1. Filter logs by subsystem
2. Quickly identify where a message came from
3. Enable/disable debug logging per system
4. Track performance bottlenecks

## Comparison

| Feature | UE5 Output Log | Godot Output | Unity Console | **UzEngine** |
|---|---|---|---|---|
| **Native integration** | Custom panel | Custom panel | Custom panel | **VSCode OutputChannel (native)** |
| **Log categories** | ✓ (Log macro with category) | ✗ | ✗ | **✓ Full categorization** |
| **Color coding** | ✓ (Warning=yellow, Error=red) | ✓ By level | ✓ By level | **✓ By level + consistent** |
| **Clickable file:line** | ✓ Opens editor | ✓ Opens editor | ✓ Opens editor | **✓ VSCode native** |
| **Problem matcher** | ✗ (separate panel) | ✗ | ✗ | **✓ VSCode Problems panel** |
| **Search/filter** | ✓ Search box | ✓ Search box | ✓ Search box | **✓ VSCode search + filter UI** |
| **Thread indication** | ✗ | ✗ | ✗ | **✓ Thread ID in metadata** |
| **Timestamp** | ✗ | ✗ | ✗ | **✓ Millisecond precision** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Basic logging output (Info/Warning/Error), category labels, timestamp, file:line links, VSCode output channel integration |
| **Phase 3** | Level filter UI (Trace/Debug/Info/Warning/Error/Fatal), category checkboxes, search, problem matcher for errors, color coding, thread ID display |
| **Phase 4** | Structured logging (JSON export), log aggregation (multi-session), remote logging (cloud), performance profiler integration, custom log sinks |

