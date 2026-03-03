/**
 * console-panel.test.ts — Unit tests for ConsolePanel.
 *
 * Coverage targets (≥95% line + branch):
 *  - Constructor initialises empty state
 *  - log() with each level: 'info', 'warning', 'error'
 *  - log() before resolveWebviewView — buffered, no postMessage called
 *  - log() after resolveWebviewView — postMessage called with correct payload
 *  - log() eviction: when _entries.length >= MAX_ENTRIES, oldest entry is shifted
 *  - clear() before resolveWebviewView — entries cleared, no postMessage
 *  - clear() after resolveWebviewView — entries cleared, postMessage type='clear'
 *  - entries getter returns a readonly snapshot
 *  - resolveWebviewView() sets enableScripts=true
 *  - resolveWebviewView() sets html containing '<!DOCTYPE html>'
 *  - resolveWebviewView() replays all buffered entries via postMessage
 *  - dispose() sets _disposed = true, clears _entries
 *  - After dispose(), log() is a no-op
 *  - After dispose(), clear() is a no-op
 *  - viewType static property
 *  - MAX_ENTRIES static property
 */

import { ConsolePanel, LogEntry } from '../console-panel';

// ---------------------------------------------------------------------------
// Inline WebviewView mock (no existing mock for WebviewView)
// ---------------------------------------------------------------------------

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
        _triggerMessage(msg: unknown) {
            messageHandlers.forEach(h => h(msg));
        },
    };
}

// Minimal stubs for WebviewViewResolveContext and CancellationToken
const stubContext = {} as import('vscode').WebviewViewResolveContext;
const stubToken = {} as import('vscode').CancellationToken;

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
});

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('ConsolePanel', () => {

    // -------------------------------------------------------------------------
    // Static properties
    // -------------------------------------------------------------------------

    describe('static properties', () => {
        it('viewType is uzengine.console', () => {
            expect(ConsolePanel.viewType).toBe('uzengine.console');
        });

        it('MAX_ENTRIES is 500', () => {
            expect(ConsolePanel.MAX_ENTRIES).toBe(500);
        });
    });

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------

    describe('constructor', () => {
        it('initialises with empty entries', () => {
            const panel = new ConsolePanel();
            expect(panel.entries).toHaveLength(0);
        });

        it('is not disposed after construction', () => {
            const panel = new ConsolePanel();
            // Calling log() should work (not a no-op) before resolveWebviewView.
            panel.log('info', 'hello');
            expect(panel.entries).toHaveLength(1);
        });
    });

    // -------------------------------------------------------------------------
    // log() — before resolveWebviewView (buffered)
    // -------------------------------------------------------------------------

    describe('log() before resolveWebviewView', () => {
        it('appends an info entry to the buffer', () => {
            const panel = new ConsolePanel();
            panel.log('info', 'test info');

            expect(panel.entries).toHaveLength(1);
            expect(panel.entries[0].level).toBe('info');
            expect(panel.entries[0].message).toBe('test info');
            expect(panel.entries[0].timestamp).toBeInstanceOf(Date);
        });

        it('appends a warning entry to the buffer', () => {
            const panel = new ConsolePanel();
            panel.log('warning', 'test warning');

            expect(panel.entries).toHaveLength(1);
            expect(panel.entries[0].level).toBe('warning');
            expect(panel.entries[0].message).toBe('test warning');
        });

        it('appends an error entry to the buffer', () => {
            const panel = new ConsolePanel();
            panel.log('error', 'test error');

            expect(panel.entries).toHaveLength(1);
            expect(panel.entries[0].level).toBe('error');
            expect(panel.entries[0].message).toBe('test error');
        });

        it('does not call postMessage when view is not set', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.log('info', 'before view');

            // postMessage should NOT have been called because view is not yet set.
            expect(view.webview.postMessage).not.toHaveBeenCalled();
        });

        it('accumulates multiple buffered entries in order', () => {
            const panel = new ConsolePanel();
            panel.log('info', 'first');
            panel.log('warning', 'second');
            panel.log('error', 'third');

            expect(panel.entries).toHaveLength(3);
            expect(panel.entries[0].message).toBe('first');
            expect(panel.entries[1].message).toBe('second');
            expect(panel.entries[2].message).toBe('third');
        });
    });

    // -------------------------------------------------------------------------
    // log() — after resolveWebviewView (live posting)
    // -------------------------------------------------------------------------

    describe('log() after resolveWebviewView', () => {
        it('posts a log message to the webview with correct type and level', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('info', 'live info message');

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    type: 'log',
                    level: 'info',
                    message: 'live info message',
                })
            );
        });

        it('posts a warning message with correct level', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('warning', 'live warning');

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({ type: 'log', level: 'warning', message: 'live warning' })
            );
        });

        it('posts an error message with correct level', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('error', 'live error');

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({ type: 'log', level: 'error', message: 'live error' })
            );
        });

        it('includes a timestamp string in the posted message', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('info', 'timestamped');

            const call = (view.webview.postMessage as jest.Mock).mock.calls[0][0] as {
                timestamp: unknown;
            };
            expect(typeof call.timestamp).toBe('string');
            expect((call.timestamp as string).length).toBeGreaterThan(0);
        });

        it('appends the new entry to _entries after posting', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('info', 'entry after view');
            expect(panel.entries).toHaveLength(1);
        });
    });

    // -------------------------------------------------------------------------
    // log() — MAX_ENTRIES eviction
    // -------------------------------------------------------------------------

    describe('log() MAX_ENTRIES eviction', () => {
        it('shifts the oldest entry when capacity is reached', () => {
            const panel = new ConsolePanel();

            // Fill to exactly MAX_ENTRIES.
            for (let i = 0; i < ConsolePanel.MAX_ENTRIES; i++) {
                panel.log('info', `msg-${i}`);
            }

            expect(panel.entries).toHaveLength(ConsolePanel.MAX_ENTRIES);
            expect(panel.entries[0].message).toBe('msg-0');

            // Adding one more should shift the oldest.
            panel.log('info', 'overflow');

            expect(panel.entries).toHaveLength(ConsolePanel.MAX_ENTRIES);
            expect(panel.entries[0].message).toBe('msg-1');
            expect(panel.entries[ConsolePanel.MAX_ENTRIES - 1].message).toBe('overflow');
        });

        it('keeps count at MAX_ENTRIES after multiple overflow insertions', () => {
            const panel = new ConsolePanel();

            for (let i = 0; i < ConsolePanel.MAX_ENTRIES + 10; i++) {
                panel.log('info', `msg-${i}`);
            }

            expect(panel.entries).toHaveLength(ConsolePanel.MAX_ENTRIES);
        });

        it('evicts when view is attached and posts the new entry', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            // Fill to MAX_ENTRIES first (no view yet — buffered).
            for (let i = 0; i < ConsolePanel.MAX_ENTRIES; i++) {
                panel.log('info', `buf-${i}`);
            }

            // Attach the view — replays all 500 buffered entries.
            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            const callsBefore = (view.webview.postMessage as jest.Mock).mock.calls.length;
            expect(callsBefore).toBe(ConsolePanel.MAX_ENTRIES);

            // Now push one more — should evict oldest and post only the new entry.
            panel.log('error', 'overflow-entry');

            const callsAfter = (view.webview.postMessage as jest.Mock).mock.calls.length;
            expect(callsAfter).toBe(ConsolePanel.MAX_ENTRIES + 1);

            const lastCall = (view.webview.postMessage as jest.Mock).mock.calls[callsAfter - 1][0] as {
                level: string;
                message: string;
            };
            expect(lastCall.level).toBe('error');
            expect(lastCall.message).toBe('overflow-entry');
        });
    });

    // -------------------------------------------------------------------------
    // clear()
    // -------------------------------------------------------------------------

    describe('clear()', () => {
        it('empties the entries array before resolveWebviewView', () => {
            const panel = new ConsolePanel();
            panel.log('info', 'a');
            panel.log('info', 'b');

            panel.clear();

            expect(panel.entries).toHaveLength(0);
        });

        it('does not call postMessage when view is not set', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.log('info', 'a');
            panel.clear();

            expect(view.webview.postMessage).not.toHaveBeenCalled();
        });

        it('empties the entries array after resolveWebviewView', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('info', 'x');
            panel.clear();

            expect(panel.entries).toHaveLength(0);
        });

        it('posts a clear message after resolveWebviewView', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('info', 'x');
            // Reset call count so we only track the clear call.
            (view.webview.postMessage as jest.Mock).mockClear();

            panel.clear();

            expect(view.webview.postMessage).toHaveBeenCalledWith({ type: 'clear' });
        });
    });

    // -------------------------------------------------------------------------
    // entries getter
    // -------------------------------------------------------------------------

    describe('entries getter', () => {
        it('returns the current snapshot of entries', () => {
            const panel = new ConsolePanel();
            panel.log('info', 'alpha');
            panel.log('error', 'beta');

            const snap = panel.entries;
            expect(snap).toHaveLength(2);
            expect(snap[0].message).toBe('alpha');
            expect(snap[1].message).toBe('beta');
        });

        it('returns an empty array when no entries have been logged', () => {
            const panel = new ConsolePanel();
            expect(panel.entries).toHaveLength(0);
        });

        it('reflects the cleared state after clear()', () => {
            const panel = new ConsolePanel();
            panel.log('info', 'a');
            panel.clear();
            expect(panel.entries).toHaveLength(0);
        });
    });

    // -------------------------------------------------------------------------
    // resolveWebviewView()
    // -------------------------------------------------------------------------

    describe('resolveWebviewView()', () => {
        it('sets webview.options.enableScripts to true', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.options.enableScripts).toBe(true);
        });

        it('sets webview.html to a string containing DOCTYPE html', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.html).toContain('<!DOCTYPE html>');
        });

        it('html includes a script block', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.html).toContain('<script>');
        });

        it('replays all buffered entries via postMessage', () => {
            const panel = new ConsolePanel();

            panel.log('info', 'buf1');
            panel.log('warning', 'buf2');
            panel.log('error', 'buf3');

            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            // The 3 buffered entries should all have been posted.
            expect(view.webview.postMessage).toHaveBeenCalledTimes(3);

            const calls = (view.webview.postMessage as jest.Mock).mock.calls as Array<[{
                type: string;
                level: string;
                message: string;
            }]>;

            expect(calls[0][0]).toMatchObject({ type: 'log', level: 'info', message: 'buf1' });
            expect(calls[1][0]).toMatchObject({ type: 'log', level: 'warning', message: 'buf2' });
            expect(calls[2][0]).toMatchObject({ type: 'log', level: 'error', message: 'buf3' });
        });

        it('does not call postMessage for replay when buffer is empty', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.postMessage).not.toHaveBeenCalled();
        });

        it('stores the view so subsequent log() calls post messages', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('info', 'after resolve');

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({ message: 'after resolve' })
            );
        });
    });

    // -------------------------------------------------------------------------
    // dispose()
    // -------------------------------------------------------------------------

    describe('dispose()', () => {
        it('clears the _entries array on dispose', () => {
            const panel = new ConsolePanel();
            panel.log('info', 'a');
            panel.log('info', 'b');

            panel.dispose();

            expect(panel.entries).toHaveLength(0);
        });

        it('log() is a no-op after dispose', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.dispose();
            (view.webview.postMessage as jest.Mock).mockClear();

            panel.log('info', 'after dispose');

            // No new entry added, no postMessage called.
            expect(panel.entries).toHaveLength(0);
            expect(view.webview.postMessage).not.toHaveBeenCalled();
        });

        it('clear() is a no-op after dispose', () => {
            const panel = new ConsolePanel();
            const view = makeMockWebviewView();

            panel.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            panel.log('info', 'keep');
            panel.dispose();
            (view.webview.postMessage as jest.Mock).mockClear();

            panel.clear(); // Should be no-op — already disposed.

            // The internal array was already cleared by dispose(); no postMessage for 'clear'.
            expect(view.webview.postMessage).not.toHaveBeenCalled();
        });

        it('dispose() is idempotent — calling twice does not throw', () => {
            const panel = new ConsolePanel();
            expect(() => {
                panel.dispose();
                panel.dispose();
            }).not.toThrow();
        });

        it('log() before resolveWebviewView after dispose is still a no-op', () => {
            const panel = new ConsolePanel();
            panel.dispose();

            panel.log('error', 'ghost');

            expect(panel.entries).toHaveLength(0);
        });

        it('clear() before resolveWebviewView after dispose is a no-op (no throw)', () => {
            const panel = new ConsolePanel();
            panel.dispose();

            expect(() => panel.clear()).not.toThrow();
        });
    });

    // -------------------------------------------------------------------------
    // LogEntry type — timestamp is a Date
    // -------------------------------------------------------------------------

    describe('LogEntry timestamp', () => {
        it('each entry carries a Date timestamp', () => {
            const panel = new ConsolePanel();
            const before = new Date();
            panel.log('info', 'timed entry');
            const after = new Date();

            const entry = panel.entries[0] as LogEntry;
            expect(entry.timestamp.getTime()).toBeGreaterThanOrEqual(before.getTime());
            expect(entry.timestamp.getTime()).toBeLessThanOrEqual(after.getTime());
        });
    });
});
