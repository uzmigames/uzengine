/**
 * ipc-client.ts — Typed WebSocket IPC client for UzEngine.
 *
 * Provides a clean async request/response API over a WebSocket connection
 * with the following features:
 *
 *  - Request/response correlation via UUID (each request tagged with a unique id)
 *  - Auto-reconnect with 5-second polling when the connection drops
 *  - Push event subscriptions (server-initiated events by event type)
 *  - Typed send/receive with discriminated union message shapes
 *
 * Wire protocol (JSON):
 *   Request:  { "id": "<uuid>", "fn": "method.name", "args": { ... } }
 *   Response: { "id": "<uuid>", "ok": true|false, "result": { ... }, "error": "..." | null }
 *   Event:    { "event": "eventType", "data": { ... } }
 */

import { randomBytes } from 'crypto';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/** Arguments passed to an IPC method call. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type IpcArgs = Record<string, any>;

/** Result returned by a successful IPC method call. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type IpcResult = Record<string, any>;

/** A request message sent over WebSocket. */
interface IpcRequestMessage {
    readonly id: string;
    readonly fn: string;
    readonly args: IpcArgs;
}

/** A response message received over WebSocket. */
interface IpcResponseMessage {
    readonly id: string;
    readonly ok: boolean;
    readonly result: IpcResult;
    readonly error: string | null;
}

/** A server-initiated push event message. */
interface IpcEventMessage {
    readonly event: string;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    readonly data: Record<string, any>;
}

/** Callback type for push event subscriptions. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type IpcEventCallback = (data: Record<string, any>) => void;

/** Connection state of the IpcClient. */
export type IpcConnectionState = 'disconnected' | 'connecting' | 'connected';

/** Options for configuring an IpcClient. */
export interface IpcClientOptions {
    /** Timeout in milliseconds to wait for a response before rejecting. Default: 5000. */
    readonly timeoutMs?: number;
    /** Interval in milliseconds between reconnect attempts. Default: 5000. */
    readonly reconnectIntervalMs?: number;
}

/**
 * Structured error thrown when an IPC call fails.
 */
export class IpcClientError extends Error {
    constructor(
        message: string,
        public readonly method: string,
        public readonly requestId: string
    ) {
        super(message);
        this.name = 'IpcClientError';
    }
}

// ---------------------------------------------------------------------------
// WebSocket abstraction (allows test injection)
// ---------------------------------------------------------------------------

/** Minimal subset of the WebSocket interface used by IpcClient. */
export interface IWebSocket {
    readonly readyState: number;
    send(data: string): void;
    close(): void;
    onopen: ((event: unknown) => void) | null;
    onclose: ((event: unknown) => void) | null;
    onerror: ((event: unknown) => void) | null;
    onmessage: ((event: { data: string }) => void) | null;
}

/** WebSocket ready-state constants (mirrors the real WebSocket API). */
export const WS_CONNECTING = 0;
export const WS_OPEN = 1;
export const WS_CLOSING = 2;
export const WS_CLOSED = 3;

/** Factory function for creating WebSocket instances (injectable for tests). */
export type WebSocketFactory = (url: string) => IWebSocket;

/** Default factory that creates real WebSocket instances via the `ws` package. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
let _defaultWsFactory: WebSocketFactory | undefined;

function getDefaultWsFactory(): WebSocketFactory {
    if (!_defaultWsFactory) {
        // Dynamic require so tests don't need the `ws` package installed.
        // eslint-disable-next-line @typescript-eslint/no-require-imports
        const WebSocket = require('ws') as new (url: string) => IWebSocket;
        _defaultWsFactory = (url: string) => new WebSocket(url);
    }
    return _defaultWsFactory;
}

// ---------------------------------------------------------------------------
// Pending request tracker
// ---------------------------------------------------------------------------

interface PendingRequest {
    readonly resolve: (result: IpcResult) => void;
    readonly reject: (err: IpcClientError) => void;
    readonly method: string;
    readonly timeoutHandle: ReturnType<typeof setTimeout>;
}

// ---------------------------------------------------------------------------
// IpcClient
// ---------------------------------------------------------------------------

/**
 * Typed WebSocket IPC client for UzEngine.
 *
 * Usage:
 * ```typescript
 * const client = new IpcClient();
 * await client.connect('ws://localhost:9002');
 *
 * // Request/response
 * const tree = await client.request('scene.getTree', {});
 *
 * // Push event subscription
 * client.on('scene.treeChanged', (data) => { ... });
 *
 * client.disconnect();
 * ```
 */
export class IpcClient {
    private readonly _timeoutMs: number;
    private readonly _reconnectIntervalMs: number;
    private readonly _wsFactory: WebSocketFactory;

    /** Currently active WebSocket connection (or null when disconnected). */
    private _ws: IWebSocket | null = null;

    /** URL of the last successful / attempted connection. */
    private _url: string | null = null;

    /** Whether disconnect() was explicitly called (suppresses auto-reconnect). */
    private _disposed = false;

    /** Current connection state. */
    private _state: IpcConnectionState = 'disconnected';

    /** In-flight request promises, keyed by request UUID. */
    private readonly _pending: Map<string, PendingRequest> = new Map();

    /** Push event subscriptions, keyed by event type. */
    private readonly _eventHandlers: Map<string, Set<IpcEventCallback>> = new Map();

    /** Handle returned by setInterval for the reconnect loop. */
    private _reconnectTimer: ReturnType<typeof setInterval> | null = null;

    constructor(options?: IpcClientOptions, wsFactory?: WebSocketFactory) {
        this._timeoutMs = options?.timeoutMs ?? 5000;
        this._reconnectIntervalMs = options?.reconnectIntervalMs ?? 5000;
        this._wsFactory = wsFactory ?? getDefaultWsFactory();
    }

    // -------------------------------------------------------------------------
    // Connection management
    // -------------------------------------------------------------------------

    /**
     * Connects to the given WebSocket URL.
     * If already connected, disconnects first.
     *
     * Auto-reconnect is activated automatically if the connection drops.
     *
     * @param url  WebSocket URL (e.g. `"ws://localhost:9002"`)
     */
    connect(url: string): void {
        if (this._disposed) {
            return;
        }

        this._url = url;
        this._openConnection(url);
        this._startReconnectLoop();
    }

    /**
     * Disconnects from the current WebSocket and stops auto-reconnect.
     * All in-flight requests are rejected with a disconnect error.
     */
    disconnect(): void {
        this._disposed = true;
        this._stopReconnectLoop();
        this._closeConnection('Client disconnected');
    }

    /** Current connection state. */
    get state(): IpcConnectionState {
        return this._state;
    }

    /** True when the socket is open and ready to send messages. */
    get isConnected(): boolean {
        return this._state === 'connected';
    }

    // -------------------------------------------------------------------------
    // Request / Response
    // -------------------------------------------------------------------------

    /**
     * Sends an IPC request and waits for the corresponding response.
     *
     * @param method  The IPC method name (e.g. `"scene.getTree"`).
     * @param args    Arguments object forwarded to the engine.
     * @returns       The `result` object from the engine response.
     * @throws        IpcClientError if the request times out, the engine returns
     *                an error, or the client is disconnected.
     */
    request(method: string, args: IpcArgs = {}): Promise<IpcResult> {
        if (this._disposed) {
            return Promise.reject(
                new IpcClientError('IpcClient has been disconnected', method, '')
            );
        }

        if (this._state !== 'connected' || !this._ws) {
            return Promise.reject(
                new IpcClientError('IpcClient is not connected', method, '')
            );
        }

        const id = this._generateId();

        const promise = new Promise<IpcResult>((resolve, reject) => {
            const timeoutHandle = setTimeout(() => {
                if (this._pending.has(id)) {
                    this._pending.delete(id);
                    reject(new IpcClientError(
                        `IPC request timed out after ${this._timeoutMs}ms: ${method}`,
                        method,
                        id
                    ));
                }
            }, this._timeoutMs);

            this._pending.set(id, { resolve, reject, method, timeoutHandle });
        });

        const message: IpcRequestMessage = { id, fn: method, args };
        try {
            this._ws.send(JSON.stringify(message));
        } catch (err) {
            // Clean up if send fails immediately.
            const pending = this._pending.get(id);
            if (pending) {
                clearTimeout(pending.timeoutHandle);
                this._pending.delete(id);
            }
            return Promise.reject(
                new IpcClientError(
                    `Failed to send IPC request: ${String(err)}`,
                    method,
                    id
                )
            );
        }

        return promise;
    }

    // -------------------------------------------------------------------------
    // Push event subscriptions
    // -------------------------------------------------------------------------

    /**
     * Subscribes to a server-initiated push event by event type.
     *
     * Multiple callbacks can be registered for the same event type.
     * The returned function unsubscribes this specific callback.
     *
     * @param eventType  The event type string (e.g. `"scene.treeChanged"`).
     * @param callback   Called with the event data payload when the event fires.
     * @returns          Unsubscribe function.
     */
    on(eventType: string, callback: IpcEventCallback): () => void {
        if (!this._eventHandlers.has(eventType)) {
            this._eventHandlers.set(eventType, new Set());
        }
        this._eventHandlers.get(eventType)!.add(callback);

        return () => {
            const handlers = this._eventHandlers.get(eventType);
            if (handlers) {
                handlers.delete(callback);
                if (handlers.size === 0) {
                    this._eventHandlers.delete(eventType);
                }
            }
        };
    }

    // -------------------------------------------------------------------------
    // Private — WebSocket lifecycle
    // -------------------------------------------------------------------------

    private _openConnection(url: string): void {
        // Close any existing connection first.
        if (this._ws) {
            const old = this._ws;
            old.onopen = null;
            old.onclose = null;
            old.onerror = null;
            old.onmessage = null;
            old.close();
            this._ws = null;
        }

        this._state = 'connecting';

        const ws = this._wsFactory(url);
        this._ws = ws;

        ws.onopen = () => {
            if (this._ws !== ws) { return; } // Stale reference after reconnect.
            this._state = 'connected';
        };

        ws.onclose = () => {
            if (this._ws !== ws) { return; }
            this._ws = null;
            this._state = 'disconnected';
            this._rejectAllPending('WebSocket connection closed');
        };

        ws.onerror = () => {
            // onerror is always followed by onclose; just log here.
            // The pending requests will be rejected via the onclose handler.
        };

        ws.onmessage = (event: { data: string }) => {
            this._handleMessage(event.data);
        };
    }

    private _closeConnection(reason: string): void {
        this._rejectAllPending(reason);

        if (this._ws) {
            const ws = this._ws;
            ws.onopen = null;
            ws.onclose = null;
            ws.onerror = null;
            ws.onmessage = null;
            ws.close();
            this._ws = null;
        }

        this._state = 'disconnected';
    }

    private _startReconnectLoop(): void {
        if (this._reconnectTimer !== null) {
            return; // Already running.
        }

        this._reconnectTimer = setInterval(() => {
            if (this._disposed) {
                this._stopReconnectLoop();
                return;
            }

            if (this._state !== 'connected' && this._url) {
                this._openConnection(this._url);
            }
        }, this._reconnectIntervalMs);
    }

    private _stopReconnectLoop(): void {
        if (this._reconnectTimer !== null) {
            clearInterval(this._reconnectTimer);
            this._reconnectTimer = null;
        }
    }

    // -------------------------------------------------------------------------
    // Private — Message handling
    // -------------------------------------------------------------------------

    private _handleMessage(raw: string): void {
        let parsed: unknown;
        try {
            parsed = JSON.parse(raw);
        } catch {
            // Ignore unparseable messages.
            return;
        }

        if (typeof parsed !== 'object' || parsed === null) {
            return;
        }

        const msg = parsed as Record<string, unknown>;

        // Detect push events (have "event" key but no "id").
        if (typeof msg['event'] === 'string' && !('id' in msg)) {
            this._dispatchEvent(parsed as IpcEventMessage);
            return;
        }

        // Detect responses (have "id" and "ok").
        if (typeof msg['id'] === 'string' && 'ok' in msg) {
            this._handleResponse(parsed as IpcResponseMessage);
            return;
        }
    }

    private _handleResponse(response: IpcResponseMessage): void {
        const pending = this._pending.get(response.id);
        if (!pending) {
            return; // No pending request with this id (may have timed out).
        }

        clearTimeout(pending.timeoutHandle);
        this._pending.delete(response.id);

        if (response.ok) {
            pending.resolve(response.result ?? {});
        } else {
            pending.reject(
                new IpcClientError(
                    `Engine returned error for ${pending.method}: ${response.error ?? 'unknown error'}`,
                    pending.method,
                    response.id
                )
            );
        }
    }

    private _dispatchEvent(event: IpcEventMessage): void {
        const handlers = this._eventHandlers.get(event.event);
        if (!handlers || handlers.size === 0) {
            return;
        }

        for (const handler of handlers) {
            try {
                handler(event.data ?? {});
            } catch {
                // Swallow handler errors to avoid breaking other subscribers.
            }
        }
    }

    private _rejectAllPending(reason: string): void {
        for (const [id, pending] of this._pending) {
            clearTimeout(pending.timeoutHandle);
            pending.reject(
                new IpcClientError(reason, pending.method, id)
            );
        }
        this._pending.clear();
    }

    // -------------------------------------------------------------------------
    // Private — Utilities
    // -------------------------------------------------------------------------

    private _generateId(): string {
        const hex = randomBytes(16).toString('hex');
        return [
            hex.slice(0, 8),
            hex.slice(8, 12),
            hex.slice(12, 16),
            hex.slice(16, 20),
            hex.slice(20),
        ].join('-');
    }
}
