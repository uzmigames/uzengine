/**
 * ws.ts — Minimal Jest stub for the `ws` (WebSocket) npm package.
 *
 * `ws` is not installed in devDependencies because the extension's IpcClient
 * uses a dynamic `require('ws')` that is only reached when no wsFactory is
 * injected (i.e., in production, not in tests that inject a mock factory).
 *
 * This stub exists solely to allow Jest to resolve `require('ws')` inside
 * `getDefaultWsFactory()` when tests exercise that code path via
 * `moduleNameMapper`.  It is exported with `export =` so that
 * `const WebSocket = require('ws')` returns the constructor directly,
 * matching how the real `ws` package exports its default export.
 */

class WebSocket {
    public url: string;
    public readyState: number = 0; // WS_CONNECTING
    public onopen: ((e: unknown) => void) | null = null;
    public onclose: ((e: unknown) => void) | null = null;
    public onerror: ((e: unknown) => void) | null = null;
    public onmessage: ((e: { data: string }) => void) | null = null;

    constructor(url: string) {
        this.url = url;
    }

    send(_data: string): void { /* stub */ }
    close(): void { this.readyState = 3; /* WS_CLOSED */ }
}

// Use `export =` so that `require('ws')` returns the constructor itself,
// matching the real `ws` package's CommonJS export style.
export = WebSocket;
