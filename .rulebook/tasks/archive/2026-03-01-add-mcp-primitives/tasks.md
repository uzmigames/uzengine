## Prerequisite
- add-engine-ipc must be complete and working ✅

## 1. MCP Server Setup

- [x] 1.1 Create `tools/mcp-uzengine/package.json` — name: @uzmigames/uzengine-mcp, bin: uzengine-mcp
- [x] 1.2 Create `tools/mcp-uzengine/index.js` — MCP server with stdio transport, 5 tools
- [x] 1.3 Create `tools/mcp-uzengine/ipc-client.js` — reads/writes build/Release/preview/*.json

## 2. Implement the 5 Primitive Tools

- [x] 2.1 `uz_call(function, args?, timeout_ms?)` — writes command.json, polls response.json by ID (4s default)
- [x] 2.2 `uz_screenshot(wait_ms?)` — calls engine.screenshot, waits for PNG, returns base64 inline image
- [x] 2.3 `uz_status()` — reads status.json directly, returns alive:false if not found
- [x] 2.4 `uz_read_file(path)` — delegates to uz_call("file.read", {path})
- [x] 2.5 `uz_write_file(path, content)` — delegates to uz_call("file.write", {path, content})

## 3. Configuration

- [x] 3.1 Added "uzengine" entry to `.mcp.json` at project root (node tools/mcp-uzengine/index.js)
- [x] 3.2 Added `/tools/mcp-uzengine/node_modules/` to `.gitignore`
- [x] 3.3 Created `docs/ai-interaction.md` with full tool API, Skills model, and example workflows

## 4. Acceptance Criteria

- [x] `.mcp.json` has uzengine server entry — auto-discovered by Claude Code on project open
- [x] `uz_status()` returns `{alive:false}` gracefully when engine not running
- [x] `uz_call("engine.ping", {})` returns `{ok:true}` when engine is running
- [x] `uz_screenshot()` returns inline base64 PNG image Claude can see
- [x] `uz_call("scene.setRotation", {"y": 3.0})` dispatches to engine IPC
- [x] `uz_write_file` delegates to engine file.write handler
- [x] All 5 tools documented in docs/ai-interaction.md with examples

<!-- Server startup smoke test: OK. npm install: 0 vulnerabilities. -->
