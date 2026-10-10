import { EventEmitter } from 'node:events';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { FrameStats, LogCallback, UzEngineAddon } from '../uzengine';
import { runWorker, type WorkerCommand } from '../runtime-worker';

const disk = vi.hoisted(() => ({
  mkdirSync: vi.fn(),
  appendFileSync: vi.fn(),
  copyFileSync: vi.fn(),
  rmSync: vi.fn(),
}));
vi.mock('node:fs', () => disk);
vi.mock('node:os', () => ({ homedir: () => '/test-user', tmpdir: () => '/test-temp' }));

class Host extends EventEmitter {
  env: NodeJS.ProcessEnv = { LOCALAPPDATA: '/test-local', PATH: 'original-path' };
  platform: NodeJS.Platform = 'win32';
  pid = 123;
  version = 'test-node';
  arch = 'x64';
  send: ReturnType<typeof vi.fn> | undefined = vi.fn();
  exit = vi.fn();
}

describe('runtime worker at the native addon boundary', () => {
  let host: Host;
  let addon: Record<string, ReturnType<typeof vi.fn>>;
  let loader: ReturnType<typeof vi.fn<(path: string) => UzEngineAddon>>;
  beforeEach(() => {
    vi.resetAllMocks();
    host = new Host();
    addon = Object.fromEntries(
      [
        'init',
        'shutdown',
        'setLogCallback',
        'setEngineDir',
        'getVersion',
        'getBackend',
        'createViewport',
        'destroyViewport',
        'resizeViewport',
        'setViewportPosition',
        'startRenderLoop',
        'stopRenderLoop',
        'requestKeyframe',
        'getCodecExtradata',
        'setEncodeBitrate',
        'setEditorMode',
        'setContentRoot',
        'loadScene',
        'saveScene',
        'getSceneTree',
        'createEntity',
        'destroyEntity',
        'getComponentJson',
        'setComponentProperty',
        'captureScreenshot',
        'onMouseMove',
        'onMouseButton',
        'onKey',
        'onKeyEx',
        'onScroll',
        'getEntityComponents',
        'selectEntity',
        'getSelectedEntity',
        'play',
        'stop',
        'isPlaying',
      ].map((name) => [name, vi.fn()]),
    );
    loader = vi.fn(() => addon as unknown as UzEngineAddon);
  });
  function start(): void {
    runWorker(host as unknown as Parameters<typeof runWorker>[0], loader);
  }
  function send(command: WorkerCommand): void {
    host.emit('message', command);
  }

  const cases: Array<[WorkerCommand, string, unknown[], unknown?]> = [
    [{ id: 1, cmd: 'setEngineDir', path: 'engine dir' }, 'setEngineDir', ['engine dir']],
    [{ id: 1, cmd: 'getVersion' }, 'getVersion', [], 'version'],
    [{ id: 1, cmd: 'getBackend' }, 'getBackend', [], 'dx12'],
    [
      { id: 1, cmd: 'createViewport', parent: 4, width: 640, height: 480 },
      'createViewport',
      [4, 640, 480],
    ],
    [{ id: 1, cmd: 'destroyViewport' }, 'destroyViewport', []],
    [{ id: 1, cmd: 'resizeViewport', width: 640, height: 480 }, 'resizeViewport', [640, 480]],
    [
      { id: 1, cmd: 'setViewportPosition', x: 1, y: 2, width: 3, height: 4 },
      'setViewportPosition',
      [1, 2, 3, 4],
    ],
    [{ id: 1, cmd: 'stopRenderLoop' }, 'stopRenderLoop', []],
    [{ id: 1, cmd: 'requestKeyframe' }, 'requestKeyframe', []],
    [{ id: 1, cmd: 'setEncodeBitrate', kbps: 8000 }, 'setEncodeBitrate', [8000]],
    [{ id: 1, cmd: 'setEditorMode', enabled: true }, 'setEditorMode', [true]],
    [{ id: 1, cmd: 'setContentRoot', path: 'content dir' }, 'setContentRoot', ['content dir']],
    [{ id: 1, cmd: 'loadScene', path: 'scene' }, 'loadScene', ['scene']],
    [{ id: 1, cmd: 'saveScene', path: 'saved' }, 'saveScene', ['saved']],
    [{ id: 1, cmd: 'getSceneTree' }, 'getSceneTree', [], '{"entities":[]}'],
    [{ id: 1, cmd: 'createEntity', name: 'Light' }, 'createEntity', ['Light'], 31],
    [{ id: 1, cmd: 'destroyEntity', entityId: 31 }, 'destroyEntity', [31]],
    [
      { id: 1, cmd: 'getComponentJson', entityId: 31, componentType: 'Transform' },
      'getComponentJson',
      [31, 'Transform'],
      '{}',
    ],
    [
      {
        id: 1,
        cmd: 'setComponentProperty',
        entityId: 31,
        componentType: 'Transform',
        property: 'x',
        valueJson: '1',
      },
      'setComponentProperty',
      [31, 'Transform', 'x', '1'],
      true,
    ],
    [{ id: 1, cmd: 'mouseMove', x: 10, y: 20 }, 'onMouseMove', [10, 20]],
    [{ id: 1, cmd: 'mouseButton', button: 1, pressed: true }, 'onMouseButton', [1, true]],
    [{ id: 1, cmd: 'key', keycode: 32, pressed: false }, 'onKey', [32, false]],
    [{ id: 1, cmd: 'keyEx', keycode: 32, pressed: true, modifiers: 2 }, 'onKeyEx', [32, true, 2]],
    [{ id: 1, cmd: 'scroll', delta: -1 }, 'onScroll', [-1]],
    [
      { id: 1, cmd: 'getEntityComponents', entityId: 31 },
      'getEntityComponents',
      [31],
      '["Transform"]',
    ],
    [{ id: 1, cmd: 'selectEntity', entityId: 31 }, 'selectEntity', [31]],
    [{ id: 1, cmd: 'getSelectedEntity' }, 'getSelectedEntity', [], 31],
    [{ id: 1, cmd: 'play' }, 'play', [], true],
    [{ id: 1, cmd: 'stop' }, 'stop', [], true],
    [{ id: 1, cmd: 'isPlaying' }, 'isPlaying', [], false],
  ];
  it.each(cases)('dispatches $cmd through %s', (command, method, args, result) => {
    addon[method].mockReturnValue(result);
    start();
    send(command);
    expect(addon[method]).toHaveBeenCalledExactlyOnceWith(...args);
    expect(host.send).toHaveBeenLastCalledWith({ id: 1, ok: true, result });
  });

  it('copies only the addon/runtime to a private process directory and cleans up on exit', () => {
    start();
    expect(loader.mock.calls[0][0]).toMatch(/uzengine-worker-123[/\\]uzengine.node$/);
    expect(disk.copyFileSync).toHaveBeenCalledTimes(2);
    expect(host.env.PATH).toContain('original-path');
    host.emit('exit', 0);
    expect(disk.rmSync).toHaveBeenCalledWith(expect.stringMatching(/uzengine-worker-123$/), {
      recursive: true,
      force: true,
    });
  });

  it.each([new Error('read only'), 'read only'])(
    'falls back to native files if copying fails (%s)',
    (error) => {
      disk.copyFileSync.mockImplementation(() => {
        throw error;
      });
      disk.mkdirSync.mockImplementationOnce(() => {
        throw new Error('no logs directory');
      });
      disk.appendFileSync.mockImplementation(() => {
        throw new Error('full disk');
      });
      start();
      expect(loader.mock.calls[0][0]).toMatch(/native[/\\]uzengine.node$/);
      addon.getVersion.mockReturnValue('v');
      send({ id: 1, cmd: 'getVersion' });
      expect(host.send).toHaveBeenLastCalledWith({ id: 1, ok: true, result: 'v' });
      disk.rmSync.mockImplementation(() => {
        throw new Error('locked');
      });
      expect(() => host.emit('exit', 0)).not.toThrow();
    },
  );

  it.each(['win32', 'linux'] as const)('works without LOCALAPPDATA or PATH on %s', (platform) => {
    host.platform = platform;
    host.env = {};
    start();
    expect(loader).toHaveBeenCalledOnce();
    expect(host.env.PATH).toContain('native');
  });

  it.each([new Error('ABI mismatch'), 'ABI mismatch'])(
    'reports loader failure for each command (%s)',
    (error) => {
      loader.mockImplementation(() => {
        throw error;
      });
      start();
      send({ id: 4, cmd: 'init' });
      expect(host.send).toHaveBeenLastCalledWith({
        id: 4,
        ok: false,
        error: 'UzEngine native addon not loaded: ABI mismatch',
      });
    },
  );

  it('serializes binary replies and preserves null encoder/capture results', () => {
    start();
    for (const cmd of ['getCodecExtradata', 'captureScreenshot'] as const) {
      addon[cmd].mockReturnValue(Buffer.from([0, 255, 128]));
      send({ id: 5, cmd });
      expect(host.send).toHaveBeenLastCalledWith({ id: 5, ok: true, result: 'AP+A' });
      addon[cmd].mockReturnValue(null);
      send({ id: 6, cmd });
      expect(host.send).toHaveBeenLastCalledWith({ id: 6, ok: true, result: null });
    }
  });

  it('forwards sampled native logs, stats and binary frames without changing their values', () => {
    start();
    send({ id: 1, cmd: 'init' });
    const log = addon.setLogCallback.mock.calls[0][0] as LogCallback;
    for (let i = 0; i < 52; i++) log(2, 'Scene', 'loaded');
    log(7, 'GPU', 'unrecognized severity');
    expect(host.send).toHaveBeenLastCalledWith({
      type: 'log',
      level: 7,
      category: 'GPU',
      message: 'unrecognized severity',
    });
    send({ id: 2, cmd: 'startRenderLoop', fps: 60 });
    const stats = addon.startRenderLoop.mock.calls[0][1] as (stats: FrameStats) => void;
    const frame = addon.startRenderLoop.mock.calls[0][2] as (buffer: Buffer) => void;
    const sample = { fps: 60, frameMs: 16, drawCalls: 3, vertices: 9, triangles: 3, frameIndex: 7 };
    for (let i = 0; i < 121; i++) {
      stats(sample);
      frame(Buffer.from([0, 255, 128]));
    }
    expect(host.send).toHaveBeenCalledWith({
      type: 'stats',
      fps: 60,
      frameMs: 16,
      drawCalls: 3,
      vertices: 9,
      triangles: 3,
    });
    expect(host.send).toHaveBeenLastCalledWith({ type: 'frame', data: 'AP+A' });
    host.send = undefined;
    expect(() => {
      log(4, 'GPU', 'lost');
      stats(sample);
      frame(Buffer.alloc(0));
    }).not.toThrow();
  });

  it('replies to unknown commands and native exceptions without terminating the worker', () => {
    start();
    send({ id: 2, cmd: 'unknown' } as unknown as WorkerCommand);
    expect(host.send).toHaveBeenLastCalledWith({
      id: 2,
      ok: false,
      error: 'Unknown command: unknown',
    });
    addon.loadScene.mockImplementation(() => {
      throw new Error('invalid scene');
    });
    send({ id: 3, cmd: 'loadScene', path: 'invalid' });
    expect(host.send).toHaveBeenLastCalledWith({ id: 3, ok: false, error: 'invalid scene' });
    addon.loadScene.mockImplementation(() => {
      throw 'invalid scene';
    });
    send({ id: 4, cmd: 'loadScene', path: 'invalid' });
    expect(host.send).toHaveBeenLastCalledWith({ id: 4, ok: false, error: 'invalid scene' });
    expect(host.exit).not.toHaveBeenCalled();
  });

  it.each([true, false])(
    'flushes shutdown replies even when addon cleanup fails (loaded=%s)',
    async (loaded) => {
      if (!loaded)
        loader.mockImplementation(() => {
          throw new Error('missing addon');
        });
      for (const method of ['setLogCallback', 'destroyViewport', 'shutdown']) {
        addon[method].mockImplementation(() => {
          throw new Error('already stopped');
        });
      }
      start();
      send({ id: 8, cmd: 'shutdown' });
      expect(host.send).toHaveBeenLastCalledWith({ id: 8, ok: true, result: undefined });
      expect(host.exit).not.toHaveBeenCalled();
      await new Promise((resolve) => setImmediate(resolve));
      expect(host.exit).toHaveBeenCalledWith(0);
    },
  );

  it('reports fatal exceptions and rejected promises through IPC and tolerates a missing channel', () => {
    start();
    host.emit('unhandledRejection', new Error('rejection'));
    expect(host.send).toHaveBeenLastCalledWith(
      expect.objectContaining({ level: 5, message: 'Unhandled rejection: rejection' }),
    );
    host.emit('unhandledRejection', 'plain rejection');
    const error = new Error('fatal');
    host.emit('uncaughtException', error);
    expect(host.exit).toHaveBeenCalledWith(1);
    error.stack = undefined;
    host.emit('uncaughtException', error);
    host.send = undefined;
    expect(() => {
      host.emit('unhandledRejection', 'offline');
      host.emit('uncaughtException', error);
    }).not.toThrow();
  });
});
