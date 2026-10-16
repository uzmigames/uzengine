import { EventEmitter } from 'node:events';
import type * as vscode from 'vscode';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { WorkerCommand, WorkerResponse } from '../runtime-worker';

const platform = vi.hoisted(() => ({ fork: vi.fn(), showErrorMessage: vi.fn() }));
vi.mock('child_process', () => ({ fork: platform.fork }));
vi.mock('vscode', () => ({ window: { showErrorMessage: platform.showErrorMessage } }));
import { RuntimeClient, RuntimeClientError } from '../runtime-client';

class Worker extends EventEmitter {
  connected = true;
  killed = false;
  send = vi.fn((_command: WorkerCommand, callback?: (error: Error | null) => void) => {
    callback?.(null);
    return true;
  });
  kill = vi.fn(() => {
    this.killed = true;
    return true;
  });
  reply(result?: unknown, index = this.send.mock.calls.length - 1): void {
    const command = this.send.mock.calls[index][0];
    this.emit('message', { id: command.id, ok: true, result } satisfies WorkerResponse);
  }
}

describe('RuntimeClient IPC and process lifecycle', () => {
  let workers: Worker[];
  let client: RuntimeClient;
  let log: {
    info: ReturnType<typeof vi.fn>;
    warn: ReturnType<typeof vi.fn>;
    error: ReturnType<typeof vi.fn>;
  };
  beforeEach(() => {
    vi.clearAllMocks();
    workers = [];
    platform.fork.mockImplementation(() => {
      const worker = new Worker();
      workers.push(worker);
      return worker;
    });
    platform.showErrorMessage.mockResolvedValue(undefined);
    log = { info: vi.fn(), warn: vi.fn(), error: vi.fn() };
    client = new RuntimeClient(log as unknown as vscode.LogOutputChannel);
  });
  afterEach(() => {
    client.dispose();
    vi.useRealTimers();
  });

  it('spawns lazily and correlates concurrent replies regardless of reply order', async () => {
    expect(platform.fork).not.toHaveBeenCalled();
    const version = client.getVersion();
    const backend = client.getBackend();
    expect(platform.fork).toHaveBeenCalledOnce();
    expect(platform.fork.mock.calls[0][0]).toMatch(/runtime-worker\.js$/);
    expect(workers[0].send.mock.calls.map((call) => call[0].id)).toEqual([1, 2]);
    workers[0].reply('dx12', 1);
    workers[0].reply('0.7.0', 0);
    await expect(version).resolves.toBe('0.7.0');
    await expect(backend).resolves.toBe('dx12');
    workers[0].emit('message', { id: 999, ok: true, result: 'late' });
  });

  const commands: Array<
    [string, (c: RuntimeClient) => Promise<unknown>, Record<string, unknown>, unknown]
  > = [
    [
      'viewport creation',
      (c) => c.createViewport(42, 800, 600),
      { cmd: 'createViewport', parent: 42, width: 800, height: 600 },
      undefined,
    ],
    ['viewport destruction', (c) => c.destroyViewport(), { cmd: 'destroyViewport' }, undefined],
    [
      'viewport resize',
      (c) => c.resizeViewport(640, 480),
      { cmd: 'resizeViewport', width: 640, height: 480 },
      undefined,
    ],
    [
      'viewport position',
      (c) => c.setViewportPosition(10, 20, 640, 480),
      { cmd: 'setViewportPosition', x: 10, y: 20, width: 640, height: 480 },
      undefined,
    ],
    ['keyframe', (c) => c.requestKeyframe(), { cmd: 'requestKeyframe' }, undefined],
    [
      'bitrate',
      (c) => c.setEncodeBitrate(8000),
      { cmd: 'setEncodeBitrate', kbps: 8000 },
      undefined,
    ],
    [
      'editor mode',
      (c) => c.setEditorMode(true),
      { cmd: 'setEditorMode', enabled: true },
      undefined,
    ],
    [
      'content root',
      (c) => c.setContentRoot('assets'),
      { cmd: 'setContentRoot', path: 'assets' },
      undefined,
    ],
    [
      'scene load',
      (c) => c.loadScene('scene.uzscene'),
      { cmd: 'loadScene', path: 'scene.uzscene' },
      undefined,
    ],
    [
      'scene save',
      (c) => c.saveScene('saved.uzscene'),
      { cmd: 'saveScene', path: 'saved.uzscene' },
      undefined,
    ],
    ['scene tree', (c) => c.getSceneTree(), { cmd: 'getSceneTree' }, '{"entities":[]}'],
    ['entity creation', (c) => c.createEntity('Light'), { cmd: 'createEntity', name: 'Light' }, 19],
    [
      'entity destruction',
      (c) => c.destroyEntity(19),
      { cmd: 'destroyEntity', entityId: 19 },
      undefined,
    ],
    [
      'component read',
      (c) => c.getComponentJson(19, 'Transform'),
      { cmd: 'getComponentJson', entityId: 19, componentType: 'Transform' },
      '{}',
    ],
    [
      'component write',
      (c) => c.setComponentProperty(19, 'Transform', 'x', '1'),
      {
        cmd: 'setComponentProperty',
        entityId: 19,
        componentType: 'Transform',
        property: 'x',
        valueJson: '1',
      },
      true,
    ],
    [
      'component list',
      (c) => c.getEntityComponents(19),
      { cmd: 'getEntityComponents', entityId: 19 },
      '["Transform"]',
    ],
    ['selection', (c) => c.selectEntity(19), { cmd: 'selectEntity', entityId: 19 }, undefined],
    ['selected entity', (c) => c.getSelectedEntity(), { cmd: 'getSelectedEntity' }, 19],
    ['play', (c) => c.play(), { cmd: 'play' }, true],
    ['stop', (c) => c.stop(), { cmd: 'stop' }, true],
    ['play state', (c) => c.isPlaying(), { cmd: 'isPlaying' }, false],
    ['shutdown', (c) => c.shutdown(), { cmd: 'shutdown' }, undefined],
  ];
  it.each(commands)(
    'preserves arguments and results for %s',
    async (_name, invoke, command, result) => {
      const pending = invoke(client);
      expect(workers[0].send.mock.calls[0][0]).toEqual({ id: 1, ...command });
      workers[0].reply(result);
      await expect(pending).resolves.toEqual(result);
    },
  );

  it('restores the engine directory before initializing a replacement worker', async () => {
    const setting = client.setEngineDir('engine directory');
    workers[0].reply();
    await setting;
    client.restart();
    const init = client.init();
    expect(workers[1].send.mock.calls[0][0]).toMatchObject({
      cmd: 'setEngineDir',
      path: 'engine directory',
    });
    workers[1].reply();
    await Promise.resolve();
    expect(workers[1].send.mock.calls[1][0].cmd).toBe('init');
    workers[1].reply();
    await init;
  });

  it('initializes without a remembered directory and decodes optional binary replies', async () => {
    const init = client.init();
    expect(workers[0].send.mock.calls[0][0].cmd).toBe('init');
    workers[0].reply();
    await init;
    for (const invoke of [() => client.getCodecExtradata(), () => client.captureScreenshot()]) {
      const empty = invoke();
      workers[0].reply(null);
      await expect(empty).resolves.toBeNull();
      const binary = invoke();
      workers[0].reply('AP+A');
      await expect(binary).resolves.toEqual(Buffer.from([0, 255, 128]));
    }
  });

  it('forwards frames, stats and logs only to active listeners', async () => {
    const stats = vi.fn(),
      frames = vi.fn(),
      logs = vi.fn();
    client.setLogListener(logs);
    const start = client.startRenderLoop(60, stats, frames);
    workers[0].reply();
    await start;
    const sample = { type: 'stats', fps: 60, frameMs: 16, drawCalls: 3, vertices: 9, triangles: 3 };
    workers[0].emit('message', sample);
    workers[0].emit('message', { type: 'frame', data: 'AP+A', key: true });
    workers[0].emit('message', { type: 'log', level: 2, category: 'Scene', message: 'loaded' });
    expect(stats).toHaveBeenCalledWith({
      fps: 60,
      frameMs: 16,
      drawCalls: 3,
      vertices: 9,
      triangles: 3,
      frameIndex: 0,
    });
    expect(frames).toHaveBeenCalledWith('AP+A', true);
    expect(logs).toHaveBeenCalledWith(2, 'Scene', 'loaded');
    const stop = client.stopRenderLoop();
    workers[0].reply();
    await stop;
    client.setLogListener(undefined);
    workers[0].emit('message', sample);
    workers[0].emit('message', { type: 'frame', data: 'late', key: false });
    workers[0].emit('message', { type: 'log', level: 4, category: 'GPU', message: 'lost' });
    expect(stats).toHaveBeenCalledOnce();
    expect(frames).toHaveBeenCalledOnce();
    expect(logs).toHaveBeenCalledOnce();
    expect(log.error).toHaveBeenCalledWith('[Worker][GPU] lost');
  });

  it('sends input without starting a worker or waiting for replies', async () => {
    client.mouseMove(1, 2);
    expect(platform.fork).not.toHaveBeenCalled();
    const version = client.getVersion();
    workers[0].reply('v');
    await version;
    client.mouseMove(1, 2);
    client.mouseButton(1, true);
    client.key(32, false);
    client.keyEx(65, true, 2);
    client.scroll(-1);
    expect(workers[0].send.mock.calls.slice(1).map((call) => call[0])).toEqual([
      { id: 2, cmd: 'mouseMove', x: 1, y: 2 },
      { id: 3, cmd: 'mouseButton', button: 1, pressed: true },
      { id: 4, cmd: 'key', keycode: 32, pressed: false },
      { id: 5, cmd: 'keyEx', keycode: 65, pressed: true, modifiers: 2 },
      { id: 6, cmd: 'scroll', delta: -1 },
    ]);
    workers[0].connected = false;
    client.scroll(1);
    expect(workers[0].send).toHaveBeenCalledTimes(6);
  });

  it('rejects native failures and synchronous send failures', async () => {
    const command = client.getVersion();
    workers[0].emit('message', { id: 1, ok: false, error: 'addon unavailable' });
    await expect(command).rejects.toMatchObject({ code: 'native', message: 'addon unavailable' });
    workers[0].send.mockImplementation(() => {
      throw 'closed channel';
    });
    await expect(client.getBackend()).rejects.toThrow('closed channel');
    expect(() => client.scroll(1)).not.toThrow();
    workers[0].connected = false;
    await expect(client.getBackend()).rejects.toThrow('not connected');
  });

  it('rejects pending calls when the worker crashes and offers restart', async () => {
    const command = client.getVersion();
    const rejected = expect(command).rejects.toMatchObject({
      code: 'worker-exit',
      message: expect.stringContaining('exited with code 9'),
    });
    platform.showErrorMessage.mockResolvedValue('Restart');
    workers[0].emit('exit', 9, null);
    await rejected;
    expect(platform.showErrorMessage).toHaveBeenCalledOnce();
    const next = client.getVersion();
    workers[1].reply('new');
    await expect(next).resolves.toBe('new');
  });

  it('does not let a replaced worker exit or message settle calls on its successor', async () => {
    const oldCall = client.getVersion();
    const rejected = expect(oldCall).rejects.toThrow();
    const old = workers[0];
    client.restart();
    await rejected;
    const fresh = client.getBackend();
    const settled = vi.fn();
    void fresh.then(settled, settled);
    const freshId = workers[1].send.mock.calls[0][0].id;
    old.emit('message', { id: freshId, ok: true, result: 'stale' });
    old.emit('exit', null, 'SIGTERM');
    await Promise.resolve();
    expect(settled).not.toHaveBeenCalled();
    expect(platform.showErrorMessage).not.toHaveBeenCalled();
    workers[1].reply('dx12');
    await expect(fresh).resolves.toBe('dx12');
    const next = client.getVersion();
    expect(platform.fork).toHaveBeenCalledTimes(2);
    workers[1].reply('v');
    await next;
  });

  it('never spawns a worker after disposal', async () => {
    client.dispose();
    client.dispose();
    await expect(client.getVersion()).rejects.toMatchObject({ code: 'disposed' });
    expect(platform.fork).not.toHaveBeenCalled();
  });

  it.each([
    [0, null, 'code 0'],
    [null, 'SIGTERM', 'signal SIGTERM'],
    [null, null, 'code unknown'],
  ] as const)(
    'settles pending calls on process termination (%s, %s)',
    async (code, signal, reason) => {
      const pending = client.getVersion();
      const rejected = expect(pending).rejects.toThrow(reason);
      workers[0].emit('exit', code, signal);
      await rejected;
      if (code === 0) expect(platform.showErrorMessage).not.toHaveBeenCalled();
    },
  );

  it.each([
    ['getVersion', (c: RuntimeClient) => c.getVersion(), 10_000],
    ['init', (c: RuntimeClient) => c.init(), 30_000],
    ['createViewport', (c: RuntimeClient) => c.createViewport(0, 1, 1), 60_000],
    ['loadScene', (c: RuntimeClient) => c.loadScene('scene'), 60_000],
    ['saveScene', (c: RuntimeClient) => c.saveScene('scene'), 30_000],
  ] as const)('times out %s at its deadline and ignores late replies', async (_name, invoke, budget) => {
    vi.useFakeTimers();
    const call = invoke(client);
    const settled = vi.fn();
    void call.then(settled, settled);
    const rejected = expect(call).rejects.toMatchObject({ code: 'timeout' });
    await vi.advanceTimersByTimeAsync(budget - 1);
    expect(settled).not.toHaveBeenCalled();
    await vi.advanceTimersByTimeAsync(1);
    await rejected;
    expect(settled.mock.calls[0][0]).toBeInstanceOf(RuntimeClientError);
    workers[0].reply('late');
    expect(settled).toHaveBeenCalledOnce();
    expect(vi.getTimerCount()).toBe(0);
  });

  it('clears deadlines for successful and native-error replies', async () => {
    vi.useFakeTimers();
    const success = client.getVersion();
    workers[0].reply('v');
    await expect(success).resolves.toBe('v');
    expect(vi.getTimerCount()).toBe(0);
    const failure = client.getBackend();
    const rejected = expect(failure).rejects.toMatchObject({ code: 'native' });
    workers[0].emit('message', { id: 2, ok: false, error: 'native failure' });
    await rejected;
    expect(vi.getTimerCount()).toBe(0);
  });

  it.each(['exit', 'dispose', 'restart'] as const)('clears all deadlines on %s', async (event) => {
    vi.useFakeTimers();
    const calls = [client.getVersion(), client.getBackend()];
    const rejected = calls.map((call) =>
      expect(call).rejects.toMatchObject({ code: event === 'dispose' ? 'disposed' : 'worker-exit' }),
    );
    expect(vi.getTimerCount()).toBe(2);
    if (event === 'exit') workers[0].emit('exit', 0, null);
    else if (event === 'dispose') client.dispose();
    else client.restart();
    await Promise.all(rejected);
    expect(vi.getTimerCount()).toBe(0);
  });

  it('rejects the 65th pending request with backpressure and frees a slot on reply', async () => {
    vi.useFakeTimers();
    const calls = Array.from({ length: 64 }, () => client.getVersion());
    await expect(client.getBackend()).rejects.toMatchObject({ code: 'backpressure' });
    expect(workers[0].send).toHaveBeenCalledTimes(64);
    workers[0].reply('v', 0);
    await calls[0];
    const next = client.getBackend();
    workers[0].reply('dx12');
    await expect(next).resolves.toBe('dx12');
    for (let i = 1; i < calls.length; i++) workers[0].reply('v', i);
    await Promise.all(calls);
    expect(vi.getTimerCount()).toBe(0);
  });

  it('drops fire-and-forget input after send returns false until its callback fires', async () => {
    const version = client.getVersion();
    workers[0].reply('v');
    await version;
    let drained: ((error: Error | null) => void) | undefined;
    workers[0].send.mockImplementationOnce((_command, callback) => {
      drained = callback;
      return false;
    });
    client.mouseMove(1, 2);
    client.mouseButton(1, true);
    client.key(32, true);
    client.keyEx(32, false, 0);
    client.scroll(1);
    expect(workers[0].send).toHaveBeenCalledTimes(2);
    drained!(null);
    client.scroll(2);
    expect(workers[0].send).toHaveBeenCalledTimes(3);
    expect(workers[0].send.mock.calls[2][0]).toMatchObject({ cmd: 'scroll', delta: 2 });
  });

  it('clears deadlines on asynchronous send errors', async () => {
    vi.useFakeTimers();
    const first = client.getVersion();
    workers[0].reply('v');
    await first;
    workers[0].send.mockImplementationOnce((_command, callback) => {
      callback?.(new Error('channel closed'));
      return false;
    });
    await expect(client.getBackend()).rejects.toMatchObject({
      code: 'worker-exit',
      message: 'channel closed',
    });
    expect(vi.getTimerCount()).toBe(0);
  });

  it('settles failed forks even without an exit event and ignores later errors from them', async () => {
    const pending = client.getVersion();
    const rejected = expect(pending).rejects.toThrow('spawn failed');
    workers[0].emit('error', new Error('spawn failed'));
    await rejected;
    expect(log.error).toHaveBeenCalledWith('[RuntimeClient] Worker process error: spawn failed');
    const fresh = client.getVersion();
    workers[0].emit('error', new Error('late error'));
    expect(log.error).toHaveBeenCalledTimes(1);
    workers[1].reply('new');
    await expect(fresh).resolves.toBe('new');
  });

  it('does not kill an already dead process and preserves Error send failures', async () => {
    const version = client.getVersion();
    workers[0].reply('v');
    await version;
    workers[0].send.mockImplementation(() => {
      throw new Error('send failure');
    });
    await expect(client.getBackend()).rejects.toThrow('send failure');
    workers[0].emit('message', { type: 'future-message' });
    workers[0].killed = true;
    client.dispose();
    expect(workers[0].kill).not.toHaveBeenCalled();
  });

  it('ignores a delayed restart prompt once a replacement is already running', async () => {
    let choose: (choice: string) => void = () => {};
    platform.showErrorMessage.mockReturnValue(
      new Promise<string>((resolve) => {
        choose = resolve;
      }),
    );
    const original = client.getVersion();
    const rejected = expect(original).rejects.toThrow();
    workers[0].emit('exit', 1, null);
    await rejected;
    const fresh = client.getVersion();
    choose('Restart');
    await Promise.resolve();
    expect(workers[1].kill).not.toHaveBeenCalled();
    workers[1].reply('v');
    await fresh;
  });
});
