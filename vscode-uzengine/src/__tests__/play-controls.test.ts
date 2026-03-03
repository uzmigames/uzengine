/**
 * play-controls.test.ts — Unit tests for PlayControls.
 *
 * Tests cover:
 *  - Constructor creates four status bar items
 *  - Initial state is 'idle' — play visible, pause/stop hidden
 *  - play() transitions to 'playing', sends playmode.start IPC
 *  - play() shows pause and stop items when playing
 *  - play() does nothing if already in playing state
 *  - play() shows error message when IPC fails
 *  - play() does nothing when disposed
 *  - pause() transitions to 'paused', sends engine.pause IPC
 *  - pause() does nothing if not in playing state
 *  - pause() shows error message when IPC fails
 *  - pause() does nothing when disposed
 *  - stop() transitions to 'idle', sends playmode.stop IPC
 *  - stop() does nothing if already in idle state
 *  - stop() still transitions to idle even when IPC fails
 *  - stop() does nothing when disposed
 *  - setSceneName() and setFps() update status bar info item
 *  - FPS is formatted to 0 decimal places
 *  - FPS <= 0 is not shown in info
 *  - Empty scene name is not shown in info
 *  - dispose() is idempotent
 *  - Status bar items configured correctly in each state
 */

import { PlayControls } from '../play-controls';
import { IpcClient, IpcClientError } from '../ipc-client';
import { window } from '../__mocks__/vscode';

// ---------------------------------------------------------------------------
// Mock IpcClient
// ---------------------------------------------------------------------------

function makeMockIpc(): { client: IpcClient; request: jest.Mock } {
    const request = jest.fn().mockResolvedValue({});
    const client = { request } as unknown as IpcClient;
    return { client, request };
}

// ---------------------------------------------------------------------------
// Status bar item tracker
// ---------------------------------------------------------------------------

type MockStatusBarItem = {
    text: string;
    tooltip: string;
    command: string;
    backgroundColor: unknown;
    show: jest.Mock;
    hide: jest.Mock;
    dispose: jest.Mock;
};

function captureStatusBarItems(): MockStatusBarItem[] {
    const items: MockStatusBarItem[] = [];
    (window.createStatusBarItem as jest.Mock).mockImplementation(() => {
        const item: MockStatusBarItem = {
            text: '',
            tooltip: '',
            command: '',
            backgroundColor: undefined,
            show: jest.fn(),
            hide: jest.fn(),
            dispose: jest.fn(),
        };
        items.push(item);
        return item;
    });
    return items;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
});

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('PlayControls', () => {

    describe('construction', () => {
        it('creates exactly four status bar items', () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            expect(items).toHaveLength(4);
            controls.dispose();
        });

        it('assigns commands to the correct items', () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            // Items: [play, pause, stop, info]
            expect(items[0].command).toBe('uzengine.play');
            expect(items[1].command).toBe('uzengine.pause');
            expect(items[2].command).toBe('uzengine.stop');

            controls.dispose();
        });

        it('initial state is "idle"', () => {
            captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);
            expect(controls.state).toBe('idle');
            controls.dispose();
        });

        it('shows play item and hides pause/stop/info in idle state', () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            const [play, pause, stop, info] = items;

            expect(play.show).toHaveBeenCalled();
            expect(pause.hide).toHaveBeenCalled();
            expect(stop.hide).toHaveBeenCalled();
            expect(info.hide).toHaveBeenCalled();

            controls.dispose();
        });

        it('play item text contains "Play" in idle state', () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            expect(items[0].text).toContain('Play');
            controls.dispose();
        });
    });

    describe('play()', () => {
        it('sends playmode.start IPC and transitions to playing', async () => {
            const items = captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();

            expect(request).toHaveBeenCalledWith('playmode.start', {});
            expect(controls.state).toBe('playing');

            controls.dispose();
        });

        it('shows pause and stop items after play()', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();

            const [, pause, stop, info] = items;
            expect(pause.show).toHaveBeenCalled();
            expect(stop.show).toHaveBeenCalled();
            expect(info.show).toHaveBeenCalled();

            controls.dispose();
        });

        it('does nothing if already in playing state', async () => {
            const { client, request } = makeMockIpc();
            captureStatusBarItems();
            const controls = new PlayControls(client);

            await controls.play(); // state → playing
            request.mockClear();

            await controls.play(); // should be a no-op
            expect(request).not.toHaveBeenCalled();
            expect(controls.state).toBe('playing');

            controls.dispose();
        });

        it('shows error message when IPC fails', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            request.mockRejectedValue(new IpcClientError('Disconnected', 'playmode.start', 'x'));
            const controls = new PlayControls(client);

            await controls.play();

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Failed to start play mode')
            );
            expect(controls.state).toBe('idle'); // Should remain idle on failure.

            controls.dispose();
        });

        it('does nothing when disposed', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);
            controls.dispose();

            await controls.play();
            expect(request).not.toHaveBeenCalled();
        });
    });

    describe('pause()', () => {
        it('sends engine.pause IPC and transitions to paused', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            request.mockClear();

            await controls.pause();

            expect(request).toHaveBeenCalledWith('engine.pause', {});
            expect(controls.state).toBe('paused');

            controls.dispose();
        });

        it('does nothing when state is idle', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.pause(); // state is 'idle'
            expect(request).not.toHaveBeenCalled();
            expect(controls.state).toBe('idle');

            controls.dispose();
        });

        it('shows error message when IPC fails', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            request.mockRejectedValue(new IpcClientError('Pause failed', 'engine.pause', 'x'));

            await controls.pause();

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Failed to pause')
            );
            expect(controls.state).toBe('playing'); // Stays playing on failure.

            controls.dispose();
        });

        it('does nothing when disposed', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);
            controls.dispose();

            await controls.pause();
            expect(request).not.toHaveBeenCalled();
        });
    });

    describe('stop()', () => {
        it('sends playmode.stop IPC and transitions to idle from playing', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            request.mockClear();

            await controls.stop();

            expect(request).toHaveBeenCalledWith('playmode.stop', {});
            expect(controls.state).toBe('idle');

            controls.dispose();
        });

        it('transitions to idle from paused state', async () => {
            captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            await controls.pause();
            await controls.stop();

            expect(controls.state).toBe('idle');
            controls.dispose();
        });

        it('does nothing when already in idle state', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.stop();
            expect(request).not.toHaveBeenCalled();
            expect(controls.state).toBe('idle');

            controls.dispose();
        });

        it('still transitions to idle when IPC fails', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            request.mockRejectedValue(new IpcClientError('Stop failed', 'playmode.stop', 'x'));

            await controls.stop();

            // Should still transition to idle.
            expect(controls.state).toBe('idle');
            controls.dispose();
        });

        it('hides pause/stop/info items after stop()', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            await controls.stop();

            const [, pause, stop, info] = items;
            // After stop, we're in idle state — hide should have been called.
            expect(pause.hide).toHaveBeenCalled();
            expect(stop.hide).toHaveBeenCalled();
            expect(info.hide).toHaveBeenCalled();

            controls.dispose();
        });

        it('does nothing when disposed', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);
            controls.dispose();

            await controls.stop();
            expect(request).not.toHaveBeenCalled();
        });
    });

    describe('setSceneName()', () => {
        it('updates the info item text to include the scene name', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            controls.setSceneName('main.uzscene');

            const info = items[3];
            expect(info.text).toContain('main.uzscene');

            controls.dispose();
        });

        it('updates even before playing (no effect on info visibility in idle)', () => {
            captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            // Should not throw even in idle state.
            expect(() => controls.setSceneName('level1.uzscene')).not.toThrow();
            controls.dispose();
        });
    });

    describe('setFps()', () => {
        it('shows FPS in info item when playing', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            controls.setFps(59.7);

            const info = items[3];
            expect(info.text).toContain('60 FPS'); // Rounded to 0 decimals.

            controls.dispose();
        });

        it('does not include FPS when fps is 0', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            controls.setFps(0);

            const info = items[3];
            expect(info.text).not.toContain('FPS');

            controls.dispose();
        });

        it('shows both scene name and FPS when both are set', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            controls.setSceneName('game.uzscene');
            controls.setFps(120);

            const info = items[3];
            expect(info.text).toContain('game.uzscene');
            expect(info.text).toContain('120 FPS');

            controls.dispose();
        });
    });

    describe('status bar item state in each play mode', () => {
        it('play item text shows $(play) Resume and pause is dimmed in paused state', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            await controls.pause();

            const [play, pause] = items;
            expect(play.text).toContain('Resume');
            // Pause item should be dimmed (has backgroundColor set).
            expect(pause.backgroundColor).toBeDefined();

            controls.dispose();
        });

        it('play item has warning background and pause button shows when playing', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();

            const [play, pause] = items;
            expect(play.backgroundColor).toBeDefined(); // dimmed play button
            expect(pause.backgroundColor).toBeUndefined(); // pause not dimmed

            controls.dispose();
        });
    });

    describe('non-Error exception messages', () => {
        it('shows String(err) when play() IPC rejects with a non-Error value', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            request.mockRejectedValue('raw string error from play');
            const controls = new PlayControls(client);

            await controls.play();

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('raw string error from play')
            );
            controls.dispose();
        });

        it('shows String(err) when pause() IPC rejects with a non-Error value', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            request.mockRejectedValue('raw string error from pause');
            await controls.pause();

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('raw string error from pause')
            );
            controls.dispose();
        });
    });

    describe('_applyState() disposed guard', () => {
        it('setSceneName() returns early from _applyState when disposed', () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);
            controls.dispose();

            // After dispose, _applyState should be a no-op — calling show/hide
            // on the status bar items after dispose should not happen.
            const showCallCountBefore = items[0].show.mock.calls.length;
            controls.setSceneName('test.uzscene');
            // Show count should not have increased since _applyState returned early.
            expect(items[0].show.mock.calls.length).toBe(showCallCountBefore);
        });

        it('setFps() returns early from _applyState when disposed', () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);
            controls.dispose();

            const showCallCountBefore = items[0].show.mock.calls.length;
            controls.setFps(60);
            expect(items[0].show.mock.calls.length).toBe(showCallCountBefore);
        });
    });

    describe('info item — empty scene name and zero FPS', () => {
        it('shows "| UzEngine" when no scene name and no FPS', async () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            // No scene name, no FPS set — parts[] is empty

            const info = items[3];
            expect(info.text).toBe('| UzEngine');

            controls.dispose();
        });
    });

    describe('pause() — does nothing when state is paused (not playing)', () => {
        it('does nothing when state is paused', async () => {
            captureStatusBarItems();
            const { client, request } = makeMockIpc();
            const controls = new PlayControls(client);

            await controls.play();
            await controls.pause(); // now in paused state
            request.mockClear();

            await controls.pause(); // should be a no-op (not in playing state)
            expect(request).not.toHaveBeenCalled();
            expect(controls.state).toBe('paused');

            controls.dispose();
        });
    });

    describe('dispose()', () => {
        it('is idempotent — multiple dispose() calls do not throw', () => {
            captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);

            expect(() => {
                controls.dispose();
                controls.dispose();
            }).not.toThrow();
        });

        it('calls dispose() on all four status bar items', () => {
            const items = captureStatusBarItems();
            const { client } = makeMockIpc();
            const controls = new PlayControls(client);
            controls.dispose();

            items.forEach(item => {
                expect(item.dispose).toHaveBeenCalled();
            });
        });
    });
});
