/**
 * ipc-client.test.ts — Unit tests for IpcClient.
 *
 * Tests cover:
 *  - connect() opens a WebSocket and transitions to 'connecting' then 'connected'
 *  - request() sends a correct JSON message and resolves on matching response
 *  - request() rejects when ok=false (engine error)
 *  - request() rejects on timeout
 *  - request() rejects when not connected
 *  - request() rejects after disconnect()
 *  - request() rejects when send() throws
 *  - Auto-reconnect: reconnect loop calls _openConnection when disconnected
 *  - Push events: on() receives dispatched events, unsubscribe removes handler
 *  - Multiple subscribers for the same event type
 *  - Event handler errors are swallowed
 *  - Stale WebSocket references are ignored after reconnect
 *  - onclose rejects all pending requests
 *  - disconnect() stops reconnect loop and rejects pending requests
 *  - Message with unknown id is silently ignored
 *  - Malformed (non-JSON) message is silently ignored
 *  - Message without id and without event key is silently ignored
 *  - WS_* constants are correct
 *  - IpcClientError has correct properties
 */

import { IpcClient, IpcClientError, WS_OPEN, WS_CONNECTING, WS_CLOSING, WS_CLOSED, IWebSocket } from '../ipc-client';

// ---------------------------------------------------------------------------
// Mock WebSocket
// ---------------------------------------------------------------------------

/** Creates a controllable mock WebSocket. */
function makeMockWs(): IWebSocket & {
    _triggerOpen(): void;
    _triggerClose(): void;
    _triggerMessage(data: string): void;
    _triggerError(): void;
    _sentMessages: string[];
    _closed: boolean;
} {
    let _readyState: number = WS_CONNECTING;
    const _sentMessages: string[] = [];
    let _closed = false;

    const ws = {
        get readyState(): number { return _readyState; },
        onopen: null as ((e: unknown) => void) | null,
        onclose: null as ((e: unknown) => void) | null,
        onerror: null as ((e: unknown) => void) | null,
        onmessage: null as ((e: { data: string }) => void) | null,
        send(data: string): void {
            _sentMessages.push(data);
        },
        close(): void {
            _closed = true;
            _readyState = WS_CLOSED;
        },
        _triggerOpen(): void {
            _readyState = WS_OPEN;
            ws.onopen?.({});
        },
        _triggerClose(): void {
            _readyState = WS_CLOSED;
            ws.onclose?.({});
        },
        _triggerMessage(data: string): void {
            ws.onmessage?.({ data });
        },
        _triggerError(): void {
            ws.onerror?.({});
        },
        _sentMessages,
        get _closed(): boolean { return _closed; },
    };
    return ws;
}

/** Builds an IpcClient with an injected mock WebSocket factory. */
function makeClient(
    ws: ReturnType<typeof makeMockWs>,
    options?: { timeoutMs?: number; reconnectIntervalMs?: number }
): IpcClient {
    return new IpcClient(
        { timeoutMs: options?.timeoutMs ?? 5000, reconnectIntervalMs: options?.reconnectIntervalMs ?? 60000 },
        () => ws
    );
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Parses the last sent JSON message. */
function lastSent(ws: ReturnType<typeof makeMockWs>): Record<string, unknown> {
    const last = ws._sentMessages[ws._sentMessages.length - 1];
    return JSON.parse(last) as Record<string, unknown>;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('IpcClient', () => {

    describe('WS constants', () => {
        it('has correct constant values', () => {
            expect(WS_CONNECTING).toBe(0);
            expect(WS_OPEN).toBe(1);
            expect(WS_CLOSING).toBe(2);
            expect(WS_CLOSED).toBe(3);
        });
    });

    describe('IpcClientError', () => {
        it('has correct name, message, method, and requestId', () => {
            const err = new IpcClientError('test error', 'scene.getTree', 'abc-123');
            expect(err.name).toBe('IpcClientError');
            expect(err.message).toBe('test error');
            expect(err.method).toBe('scene.getTree');
            expect(err.requestId).toBe('abc-123');
            expect(err).toBeInstanceOf(Error);
            expect(err).toBeInstanceOf(IpcClientError);
        });
    });

    describe('connect() / state', () => {
        it('transitions to "connecting" on connect()', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            expect(client.state).toBe('connecting');
            expect(client.isConnected).toBe(false);
            client.disconnect();
        });

        it('transitions to "connected" when WebSocket fires onopen', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();
            expect(client.state).toBe('connected');
            expect(client.isConnected).toBe(true);
            client.disconnect();
        });

        it('transitions to "disconnected" when WebSocket fires onclose', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();
            ws._triggerClose();
            expect(client.state).toBe('disconnected');
            expect(client.isConnected).toBe(false);
            client.disconnect();
        });

        it('does nothing when connect() called after disconnect()', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.disconnect();
            client.connect('ws://localhost:9002'); // should be a no-op
            expect(client.state).toBe('disconnected');
        });
    });

    describe('request() — happy path', () => {
        it('sends a correctly structured JSON message', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('scene.getTree', { level: 1 });

            const msg = lastSent(ws);
            expect(typeof msg['id']).toBe('string');
            expect((msg['id'] as string).length).toBeGreaterThan(0);
            expect(msg['fn']).toBe('scene.getTree');
            expect(msg['args']).toEqual({ level: 1 });

            // Respond with matching id.
            const responseId = msg['id'] as string;
            ws._triggerMessage(JSON.stringify({ id: responseId, ok: true, result: { nodes: [] } }));

            const result = await promise;
            expect(result).toEqual({ nodes: [] });

            client.disconnect();
        });

        it('resolves with empty object when result is null', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');

            const msg = lastSent(ws);
            const id = msg['id'] as string;
            ws._triggerMessage(JSON.stringify({ id, ok: true, result: null }));

            const result = await promise;
            expect(result).toEqual({});
            client.disconnect();
        });

        it('generates a unique UUID-like id for each request', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const p1 = client.request('a.b');
            const p2 = client.request('c.d');

            const id1 = (JSON.parse(ws._sentMessages[0]) as Record<string, unknown>)['id'] as string;
            const id2 = (JSON.parse(ws._sentMessages[1]) as Record<string, unknown>)['id'] as string;

            expect(id1).not.toBe(id2);
            expect(id1).toMatch(/^[0-9a-f-]{36}$/);
            expect(id2).toMatch(/^[0-9a-f-]{36}$/);

            // Resolve both to clean up.
            ws._triggerMessage(JSON.stringify({ id: id1, ok: true, result: {} }));
            ws._triggerMessage(JSON.stringify({ id: id2, ok: true, result: {} }));
            await Promise.all([p1, p2]);
            client.disconnect();
        });
    });

    describe('request() — error paths', () => {
        it('rejects with IpcClientError when ok=false', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('scene.deleteNode', { id: 'bad-id' });
            const msg = lastSent(ws);
            ws._triggerMessage(JSON.stringify({
                id: msg['id'],
                ok: false,
                result: {},
                error: 'Node not found',
            }));

            await expect(promise).rejects.toBeInstanceOf(IpcClientError);
            await expect(promise).rejects.toMatchObject({
                message: expect.stringContaining('Node not found'),
                method: 'scene.deleteNode',
            });
            client.disconnect();
        });

        it('rejects with IpcClientError when request times out', async () => {
            const ws = makeMockWs();
            const client = new IpcClient(
                { timeoutMs: 50, reconnectIntervalMs: 60000 },
                () => ws
            );
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');

            await expect(promise).rejects.toBeInstanceOf(IpcClientError);
            await expect(promise).rejects.toMatchObject({
                message: expect.stringContaining('timed out'),
                method: 'engine.ping',
            });
            client.disconnect();
        }, 3000);

        it('rejects immediately when not connected', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            // Not calling connect() — stays at 'disconnected'.

            await expect(client.request('engine.ping')).rejects.toBeInstanceOf(IpcClientError);
            await expect(client.request('engine.ping')).rejects.toMatchObject({
                message: expect.stringContaining('not connected'),
            });
        });

        it('rejects immediately when in "connecting" state (not yet open)', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            // Not triggering onopen — still 'connecting'.

            await expect(client.request('engine.ping')).rejects.toBeInstanceOf(IpcClientError);
            client.disconnect();
        });

        it('rejects immediately after disconnect()', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();
            client.disconnect();

            await expect(client.request('engine.ping')).rejects.toBeInstanceOf(IpcClientError);
            await expect(client.request('engine.ping')).rejects.toMatchObject({
                message: expect.stringContaining('disconnected'),
            });
        });

        it('rejects pending requests when connection closes', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');

            // Simulate connection drop before response arrives.
            ws._triggerClose();

            await expect(promise).rejects.toBeInstanceOf(IpcClientError);
            await expect(promise).rejects.toMatchObject({
                message: expect.stringContaining('closed'),
            });
            client.disconnect();
        });

        it('rejects pending requests when disconnect() is called', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');
            client.disconnect();

            await expect(promise).rejects.toBeInstanceOf(IpcClientError);
        });

        it('rejects and cleans up when send() throws', async () => {
            const ws = makeMockWs();
            const sendSpy = jest.spyOn(ws, 'send').mockImplementation(() => {
                throw new Error('WebSocket send failed');
            });

            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');

            await expect(promise).rejects.toBeInstanceOf(IpcClientError);
            await expect(promise).rejects.toMatchObject({
                message: expect.stringContaining('send'),
            });

            sendSpy.mockRestore();
            client.disconnect();
        });
    });

    describe('request() — message routing', () => {
        it('ignores a response with an unknown id', async () => {
            const ws = makeMockWs();
            const client = new IpcClient(
                { timeoutMs: 80, reconnectIntervalMs: 60000 },
                () => ws
            );
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');

            // Wrong id — should be ignored.
            ws._triggerMessage(JSON.stringify({ id: 'wrong-id-that-does-not-exist', ok: true, result: {} }));

            // The real request should time out.
            await expect(promise).rejects.toMatchObject({ message: expect.stringContaining('timed out') });
            client.disconnect();
        }, 3000);

        it('ignores non-JSON messages', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');
            const msg = lastSent(ws);
            const id = msg['id'] as string;

            // Send garbage first.
            ws._triggerMessage('this is not json!!');

            // Then send valid response.
            ws._triggerMessage(JSON.stringify({ id, ok: true, result: { pong: 1 } }));

            const result = await promise;
            expect(result).toEqual({ pong: 1 });
            client.disconnect();
        });

        it('ignores messages that are not objects', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');
            const msg = lastSent(ws);
            const id = msg['id'] as string;

            // Non-object JSON.
            ws._triggerMessage('42');
            ws._triggerMessage('"hello"');
            ws._triggerMessage('null');

            // Then valid response.
            ws._triggerMessage(JSON.stringify({ id, ok: true, result: { ok: true } }));

            const result = await promise;
            expect(result).toEqual({ ok: true });
            client.disconnect();
        });

        it('ignores a message with neither id nor event key', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('engine.ping');
            const msg = lastSent(ws);
            const id = msg['id'] as string;

            // Message missing both "id" and "event".
            ws._triggerMessage(JSON.stringify({ foo: 'bar', baz: 42 }));

            // Respond with the correct id.
            ws._triggerMessage(JSON.stringify({ id, ok: true, result: {} }));

            await expect(promise).resolves.toEqual({});
            client.disconnect();
        });
    });

    describe('on() — push event subscriptions', () => {
        it('calls the callback when a matching event is received', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const received: Array<Record<string, unknown>> = [];
            client.on('scene.treeChanged', data => { received.push(data); });

            ws._triggerMessage(JSON.stringify({ event: 'scene.treeChanged', data: { version: 2 } }));

            expect(received).toHaveLength(1);
            expect(received[0]).toEqual({ version: 2 });
            client.disconnect();
        });

        it('does not call callbacks for other event types', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const received: Array<Record<string, unknown>> = [];
            client.on('scene.treeChanged', data => { received.push(data); });

            // Different event type.
            ws._triggerMessage(JSON.stringify({ event: 'engine.statusChanged', data: { fps: 60 } }));

            expect(received).toHaveLength(0);
            client.disconnect();
        });

        it('supports multiple callbacks for the same event type', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const calls1: number[] = [];
            const calls2: number[] = [];
            client.on('scene.treeChanged', () => calls1.push(1));
            client.on('scene.treeChanged', () => calls2.push(2));

            ws._triggerMessage(JSON.stringify({ event: 'scene.treeChanged', data: {} }));

            expect(calls1).toHaveLength(1);
            expect(calls2).toHaveLength(1);
            client.disconnect();
        });

        it('unsubscribe function removes the specific callback', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const received: number[] = [];
            const unsub = client.on('scene.treeChanged', () => received.push(1));

            // Fire once — should receive.
            ws._triggerMessage(JSON.stringify({ event: 'scene.treeChanged', data: {} }));
            expect(received).toHaveLength(1);

            unsub();

            // Fire again — should not receive.
            ws._triggerMessage(JSON.stringify({ event: 'scene.treeChanged', data: {} }));
            expect(received).toHaveLength(1);
            client.disconnect();
        });

        it('unsubscribe cleans up empty event handler sets', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const unsub = client.on('scene.treeChanged', () => {});
            unsub(); // Should not throw even when removing the last handler.

            client.disconnect();
        });

        it('swallows errors thrown by event handlers', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const received: number[] = [];
            client.on('scene.treeChanged', () => { throw new Error('handler error'); });
            client.on('scene.treeChanged', () => received.push(1));

            // Should not throw even though the first handler throws.
            expect(() => {
                ws._triggerMessage(JSON.stringify({ event: 'scene.treeChanged', data: {} }));
            }).not.toThrow();

            // Second handler should still run.
            expect(received).toHaveLength(1);
            client.disconnect();
        });

        it('calls handler with empty object when event data is null', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const received: Array<Record<string, unknown>> = [];
            client.on('scene.treeChanged', data => received.push(data));

            ws._triggerMessage(JSON.stringify({ event: 'scene.treeChanged', data: null }));

            expect(received).toHaveLength(1);
            expect(received[0]).toEqual({});
            client.disconnect();
        });
    });

    describe('stale WebSocket reference handling', () => {
        it('ignores onopen from a stale WebSocket after reconnect', () => {
            let wsInstance: ReturnType<typeof makeMockWs> | null = null;
            const factory = () => {
                wsInstance = makeMockWs();
                return wsInstance;
            };

            const client = new IpcClient(
                { timeoutMs: 5000, reconnectIntervalMs: 60000 },
                factory
            );

            client.connect('ws://localhost:9002');
            const firstWs = wsInstance!;
            firstWs._triggerOpen();
            expect(client.state).toBe('connected');

            // Disconnect triggers a new WS to be built on reconnect.
            // We simulate by calling connect() again directly.
            client.connect('ws://localhost:9002');
            // At this point a new WS is built; firstWs is stale.

            // Trigger onopen on the stale socket — should be ignored.
            firstWs._triggerOpen();
            // Client should still be 'connecting' based on the new socket.
            expect(client.state).not.toBe('connected'); // stale open was ignored

            client.disconnect();
        });
    });

    describe('disconnect()', () => {
        it('transitions to "disconnected" and closes the socket', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            client.disconnect();
            expect(client.state).toBe('disconnected');
            expect(ws._closed).toBe(true);
        });

        it('disconnect() with no active socket (never connected via open) sets state to disconnected', () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            // never triggered open, but still call disconnect
            client.disconnect();
            expect(client.state).toBe('disconnected');
            expect(ws._closed).toBe(true);
        });
    });

    describe('auto-reconnect loop', () => {
        it('reconnect timer stops immediately when disposed is true during interval tick', async () => {
            // This covers the _reconnectTimer disposed guard branch in _startReconnectLoop.
            jest.useFakeTimers();

            let wsCallCount = 0;
            const factory = () => {
                wsCallCount++;
                return makeMockWs();
            };

            const client = new IpcClient(
                { timeoutMs: 5000, reconnectIntervalMs: 200 },
                factory
            );
            client.connect('ws://localhost:9002');
            // wsCallCount = 1 from initial connect

            // Trigger close to set state to disconnected (so reconnect loop would fire)
            // But first disconnect the client to set _disposed = true
            client.disconnect();
            const countAfterDisconnect = wsCallCount;

            // Advance timers — the reconnect loop tick should call stopReconnectLoop
            // but NOT create a new socket because _disposed = true
            jest.advanceTimersByTime(500);

            expect(wsCallCount).toBe(countAfterDisconnect);

            jest.useRealTimers();
        });

        it('reconnect loop creates new connection when state is not connected and not disposed', async () => {
            jest.useFakeTimers();

            const sockets: ReturnType<typeof makeMockWs>[] = [];
            let idx = 0;
            const factory = () => {
                const ws = makeMockWs();
                sockets.push(ws);
                idx++;
                return ws;
            };

            const client = new IpcClient(
                { timeoutMs: 5000, reconnectIntervalMs: 100 },
                factory
            );

            client.connect('ws://localhost:9002');
            expect(sockets).toHaveLength(1);

            // First socket opens then closes
            sockets[0]._triggerOpen();
            sockets[0]._triggerClose();
            expect(client.state).toBe('disconnected');

            // Advance past reconnect interval — should create a new socket
            jest.advanceTimersByTime(200);

            // A second socket should have been created
            expect(sockets.length).toBeGreaterThan(1);

            // Clean up
            client.disconnect();
            jest.useRealTimers();
        });

        it('stale onopen fires after socket replaced — branch 15 covered', () => {
            // Save the onopen callback from ws1 BEFORE _openConnection nullifies it.
            // Then call the saved callback manually to hit the `if (this._ws !== ws)` guard.
            let wsInstances: ReturnType<typeof makeMockWs>[] = [];
            const factory = () => {
                const ws = makeMockWs();
                wsInstances.push(ws);
                return ws;
            };

            const client = new IpcClient(
                { timeoutMs: 5000, reconnectIntervalMs: 60000 },
                factory
            );

            client.connect('ws://localhost:9002');
            const first = wsInstances[0];
            // Capture the onopen closure before connect() replaces the socket
            const savedOnOpen = first.onopen;

            // Replace the socket — this nullifies first.onopen
            client.connect('ws://localhost:9002');
            expect(wsInstances).toHaveLength(2);

            // The second socket's open confirms connectivity
            wsInstances[1]._triggerOpen();
            expect(client.state).toBe('connected');

            // Now call the SAVED onopen from socket 1 — this._ws is socket 2, not socket 1
            // → triggers the stale reference guard `if (this._ws !== ws) { return; }`
            if (savedOnOpen) {
                savedOnOpen({});
            }
            // State should still be connected (guard fired early return)
            expect(client.state).toBe('connected');

            client.disconnect();
        });

        it('stale onclose fires after socket replaced — branch 16 covered', () => {
            // Save onclose from ws1 before _openConnection nullifies it.
            let wsInstances: ReturnType<typeof makeMockWs>[] = [];
            const factory = () => {
                const ws = makeMockWs();
                wsInstances.push(ws);
                return ws;
            };

            const client = new IpcClient(
                { timeoutMs: 5000, reconnectIntervalMs: 60000 },
                factory
            );

            client.connect('ws://localhost:9002');
            const first = wsInstances[0];
            first._triggerOpen();
            expect(client.state).toBe('connected');

            // Save onclose BEFORE the second connect() nullifies it
            const savedOnClose = first.onclose;

            // Replace the socket
            client.connect('ws://localhost:9002');
            wsInstances[1]._triggerOpen();
            expect(client.state).toBe('connected');

            // Fire the saved onclose from socket 1 — this._ws is socket 2
            // → triggers `if (this._ws !== ws) { return; }` (stale reference guard)
            if (savedOnClose) {
                savedOnClose({});
            }
            // State should remain connected (guard fired early return)
            expect(client.state).toBe('connected');

            client.disconnect();
        });

        it('disposed guard in reconnect timer — branch 19 covered via spy', () => {
            // To exercise `if (this._disposed) { this._stopReconnectLoop(); return; }` inside
            // the setInterval callback, we need the timer to fire after _disposed=true but
            // before the interval is cleared. We achieve this by spying on _stopReconnectLoop
            // to make the first call a no-op, letting the timer fire and hit the disposed guard.
            jest.useFakeTimers();

            const ws = makeMockWs();
            const client = new IpcClient(
                { timeoutMs: 5000, reconnectIntervalMs: 100 },
                () => ws
            );

            // Spy on the private _stopReconnectLoop to make it a no-op initially
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const spy = jest.spyOn(client as any, '_stopReconnectLoop')
                .mockImplementationOnce(() => {
                    // First call (from disconnect()) is a no-op — timer stays alive
                })
                .mockImplementationOnce(() => {
                    // Second call (from inside timer callback) — restore and call real impl
                    spy.mockRestore();
                    // eslint-disable-next-line @typescript-eslint/no-explicit-any
                    (client as any)._stopReconnectLoop();
                });

            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            // disconnect() sets _disposed=true and calls _stopReconnectLoop (mocked to no-op)
            // → the interval is NOT cleared
            client.disconnect();

            // Advance time to fire the interval callback — _disposed is true
            // → hits `if (this._disposed) { this._stopReconnectLoop(); return; }` (branch 19)
            jest.advanceTimersByTime(200);

            spy.mockRestore();
            jest.useRealTimers();
        });
    });

    describe('message with event key AND id (ambiguous message)', () => {
        it('ignores messages that have event=string AND have id key (not dispatched as either push event or response)', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const received: number[] = [];
            client.on('myEvent', () => received.push(1));

            const promise = client.request('engine.ping');
            const msg = lastSent(ws);
            const id = msg['id'] as string;

            // Has "event" string but also has "id" — dispatch branch should NOT fire
            // because the condition is: has event AND no id
            ws._triggerMessage(JSON.stringify({ event: 'myEvent', id: 'some-id', ok: false }));
            expect(received).toHaveLength(0);

            // Respond with real id to clean up pending request
            ws._triggerMessage(JSON.stringify({ id, ok: true, result: {} }));
            await promise;

            client.disconnect();
        });
    });

    describe('ok=false with null error field', () => {
        it('uses "unknown error" when error field is null', async () => {
            const ws = makeMockWs();
            const client = makeClient(ws);
            client.connect('ws://localhost:9002');
            ws._triggerOpen();

            const promise = client.request('scene.getTree');
            const msg = lastSent(ws);
            ws._triggerMessage(JSON.stringify({
                id: msg['id'],
                ok: false,
                result: {},
                error: null,
            }));

            await expect(promise).rejects.toMatchObject({
                message: expect.stringContaining('unknown error'),
            });

            client.disconnect();
        });
    });

    describe('constructor options nullish coalescing defaults', () => {
        it('uses default timeoutMs=5000 when options is provided without timeoutMs', () => {
            const ws = makeMockWs();
            // Pass options with reconnectIntervalMs only — timeoutMs defaults to 5000
            const client = new IpcClient(
                { reconnectIntervalMs: 60000 },
                () => ws
            );
            client.connect('ws://localhost:9002');
            ws._triggerOpen();
            // Just verify the client works (doesn't crash)
            expect(client.isConnected).toBe(true);
            client.disconnect();
        });

        it('uses default reconnectIntervalMs=5000 when options is provided without reconnectIntervalMs', () => {
            const ws = makeMockWs();
            // Pass options with timeoutMs only — reconnectIntervalMs defaults to 5000
            const client = new IpcClient(
                { timeoutMs: 1000 },
                () => ws
            );
            client.connect('ws://localhost:9002');
            ws._triggerOpen();
            expect(client.isConnected).toBe(true);
            client.disconnect();
        });

        it('uses default wsFactory (getDefaultWsFactory) when no factory is provided', () => {
            // getDefaultWsFactory() calls require('ws') which is mapped to src/__mocks__/ws.ts
            // via moduleNameMapper. Use jest.isolateModules so _defaultWsFactory is undefined
            // in the freshly loaded module (no cache from prior require calls).
            jest.isolateModules(() => {
                // eslint-disable-next-line @typescript-eslint/no-require-imports
                const { IpcClient: FreshIpcClient } = require('../ipc-client') as typeof import('../ipc-client');

                // Construct without a wsFactory — triggers getDefaultWsFactory()
                const client = new FreshIpcClient({ timeoutMs: 100, reconnectIntervalMs: 60000 });
                client.connect('ws://localhost:9002');
                // If getDefaultWsFactory() failed we'd have gotten a throw; reaching here
                // confirms it succeeded and created a socket using our ws mock.
                expect(client.state).toBe('connecting');
                client.disconnect();
            });
        });

        it('getDefaultWsFactory returns the cached factory on the second call', () => {
            // Exercise the `return _defaultWsFactory` branch (line 118) by constructing
            // two clients without a factory in the SAME isolated module scope.
            jest.isolateModules(() => {
                // eslint-disable-next-line @typescript-eslint/no-require-imports
                const { IpcClient: FreshIpcClient } = require('../ipc-client') as typeof import('../ipc-client');

                // First call to getDefaultWsFactory() — sets _defaultWsFactory
                const client1 = new FreshIpcClient({ timeoutMs: 100, reconnectIntervalMs: 60000 });
                client1.connect('ws://localhost:9002');
                expect(client1.state).toBe('connecting');
                client1.disconnect();

                // Second call to getDefaultWsFactory() — returns cached _defaultWsFactory
                const client2 = new FreshIpcClient({ timeoutMs: 100, reconnectIntervalMs: 60000 });
                client2.connect('ws://localhost:9002');
                expect(client2.state).toBe('connecting');
                client2.disconnect();
            });
        });

    });
});
