import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type * as vscode from 'vscode';
import type { RuntimeClient, StatsCallback, FrameCallback } from '../runtime-client';

const ui = vi.hoisted(() => ({
  createOutputChannel: vi.fn(),
  createWebviewPanel: vi.fn(),
  showErrorMessage: vi.fn(),
  showWarningMessage: vi.fn(),
  workspace: { workspaceFolders: undefined as unknown },
}));
vi.mock('vscode', () => ({
  window: ui,
  workspace: ui.workspace,
  ViewColumn: { One: 1 },
  EventEmitter: class {
    private listeners: Array<(value: unknown) => void> = [];
    event = (listener: (value: unknown) => void) => {
      this.listeners.push(listener);
      return { dispose() {} };
    };
    fire(value: unknown) {
      this.listeners.forEach((listener) => listener(value));
    }
    dispose() {
      this.listeners = [];
    }
  },
  TreeItem: class {
    constructor(
      public label: string,
      public collapsibleState: number,
    ) {}
  },
  TreeItemCollapsibleState: { None: 0, Expanded: 2 },
  ThemeIcon: class {
    constructor(public id: string) {}
  },
}));
import { SceneTreeProvider } from '../panels/scene-tree-panel';
import { EngineConsole } from '../panels/console-panel';
import { ViewportPanel } from '../panels/viewport-panel';
import { SceneEditorProvider } from '../panels/scene-editor-provider';

class Panel {
  visible = true;
  receive: (message: unknown) => void = () => {};
  close: () => void = () => {};
  state: () => void = () => {};
  webview = {
    html: '',
    options: {},
    postMessage: vi.fn().mockResolvedValue(true),
    onDidReceiveMessage: (callback: (message: unknown) => void) => {
      this.receive = callback;
    },
  };
  onDidDispose = (callback: () => void) => {
    this.close = callback;
  };
  onDidChangeViewState = (callback: () => void) => {
    this.state = callback;
  };
  dispose = vi.fn(() => this.close());
  reveal = vi.fn();
  asVscode(): vscode.WebviewPanel {
    return this as unknown as vscode.WebviewPanel;
  }
}
function nativeClient() {
  return Object.fromEntries(
    [
      'init',
      'restart',
      'getSceneTree',
      'setLogListener',
      'createViewport',
      'destroyViewport',
      'setEditorMode',
      'setContentRoot',
      'loadScene',
      'startRenderLoop',
      'stopRenderLoop',
      'getCodecExtradata',
      'resizeViewport',
      'mouseMove',
      'mouseButton',
      'key',
      'keyEx',
      'scroll',
    ].map((method) => [method, vi.fn().mockResolvedValue(undefined)]),
  );
}
function document(
  path = '/project/content/scenes/test.uzscene',
  text = '{"name":"Test Scene"}',
): vscode.TextDocument {
  return { uri: { fsPath: path }, getText: () => text } as unknown as vscode.TextDocument;
}
const settle = () => new Promise((resolve) => setImmediate(resolve));
const temporary: string[] = [];
beforeEach(() => {
  vi.clearAllMocks();
  ui.workspace.workspaceFolders = undefined;
});
afterEach(() => {
  vi.restoreAllMocks();
  temporary.splice(0).forEach((dir) => rmSync(dir, { recursive: true, force: true }));
});

describe('scene hierarchy and console', () => {
  it('reads nested and legacy array trees, labels unnamed entities and clears unavailable scenes', async () => {
    const client = nativeClient();
    let available = true;
    const tree = new SceneTreeProvider(() =>
      available ? (client as unknown as RuntimeClient) : undefined,
    );
    const changed = vi.fn();
    tree.onDidChangeTreeData(changed);
    const child = { id: 2, name: '', children: [] };
    const parent = { id: 1, name: 'Root', children: [child] };
    client.getSceneTree.mockResolvedValue(JSON.stringify({ entities: [parent] }));
    tree.refresh();
    await settle();
    expect(tree.getChildren()).toEqual([parent]);
    expect(tree.getChildren(parent)).toEqual([child]);
    expect(tree.getTreeItem(parent)).toMatchObject({ label: 'Root', collapsibleState: 2, id: '1' });
    expect(tree.getTreeItem(child)).toMatchObject({
      label: 'Entity 2',
      collapsibleState: 0,
      tooltip: 'ID: 2',
    });
    client.getSceneTree.mockResolvedValue(JSON.stringify([child]));
    tree.refresh();
    await settle();
    expect(tree.getChildren()).toEqual([child]);
    available = false;
    tree.refresh();
    expect(tree.getChildren()).toEqual([]);
    expect(changed).toHaveBeenCalledTimes(3);
  });

  it.each([null, '', '{invalid', '{"entities":null}', 'null'])(
    'clears invalid tree response %s',
    async (response) => {
      const client = nativeClient();
      client.getSceneTree.mockResolvedValue(response);
      const tree = new SceneTreeProvider(() => client as unknown as RuntimeClient);
      tree.refresh();
      await settle();
      expect(tree.getChildren()).toEqual([]);
      client.getSceneTree.mockRejectedValue(new Error('worker stopped'));
      tree.refresh();
      await settle();
      expect(tree.getChildren()).toEqual([]);
    },
  );

  it('replaces log subscriptions, renders severity and frees its channel after a worker failure', () => {
    const output = { appendLine: vi.fn(), dispose: vi.fn() };
    ui.createOutputChannel.mockReturnValue(output);
    const console = new EngineConsole();
    const a = nativeClient(),
      b = nativeClient();
    console.attachClient(a as unknown as RuntimeClient);
    console.attachClient(a as unknown as RuntimeClient);
    const listener = a.setLogListener.mock.calls[1][0] as (
      level: number,
      category: string,
      message: string,
    ) => void;
    listener(4, 'GPU', 'lost');
    listener(7, 'GPU', 'custom');
    expect(output.appendLine).toHaveBeenCalledWith(expect.stringContaining('[GPU] [Error] lost'));
    expect(output.appendLine).toHaveBeenCalledWith(expect.stringContaining('[Level7] custom'));
    console.attachClient(b as unknown as RuntimeClient);
    expect(a.setLogListener).toHaveBeenLastCalledWith(undefined);
    b.setLogListener.mockImplementation(() => {
      throw new Error('already closed');
    });
    console.dispose();
    expect(output.dispose).toHaveBeenCalledOnce();
    new EngineConsole().dispose();
  });
});

describe('viewport webview protocol', () => {
  let panel: Panel;
  let client: ReturnType<typeof nativeClient>;
  let view: ViewportPanel;
  let refresh: ReturnType<typeof vi.fn>;
  beforeEach(() => {
    panel = new Panel();
    client = nativeClient();
    refresh = vi.fn();
    ui.createWebviewPanel.mockReturnValue(panel.asVscode());
    view = new ViewportPanel({} as vscode.Uri, () => client as unknown as RuntimeClient, {
      refresh,
    } as unknown as SceneTreeProvider);
  });
  afterEach(async () => {
    view.dispose();
    await settle();
  });

  it('closes a panel before initialization without creating or destroying native resources', async () => {
    view.dispose();
    await settle();
    expect(client.createViewport).not.toHaveBeenCalled();
    expect(client.destroyViewport).not.toHaveBeenCalled();
    const direct = new ViewportPanel(
      {} as vscode.Uri,
      client as unknown as RuntimeClient,
      { refresh } as unknown as SceneTreeProvider,
    );
    direct.dispose();
    await settle();
  });

  it('starts once on ready, forwards frames and inputs, pauses while hidden and cleans up', async () => {
    client.getCodecExtradata.mockResolvedValue(Buffer.from([1, 2, 3]));
    panel.receive({ type: 'ready' });
    await settle();
    expect(client.createViewport).toHaveBeenCalledWith(0, 1280, 720);
    expect(client.startRenderLoop).toHaveBeenCalledTimes(1);
    const stats = client.startRenderLoop.mock.calls[0][1] as StatsCallback;
    const frame = client.startRenderLoop.mock.calls[0][2] as FrameCallback;
    stats({ fps: 60, frameMs: 16, drawCalls: 2, vertices: 6, triangles: 2, frameIndex: 1 });
    frame('encoded');
    expect(panel.webview.postMessage).toHaveBeenCalledWith({ type: 'init', extradata: [1, 2, 3] });
    expect(panel.webview.postMessage).toHaveBeenCalledWith({ type: 'frame', data: 'encoded' });
    panel.receive({ type: 'ready' });
    await settle();
    expect(client.startRenderLoop).toHaveBeenCalledTimes(1);
    for (const message of [
      { type: 'mouseMove', x: 2, y: 3 },
      { type: 'mouseDelta', dx: -2, dy: -3 },
      { type: 'mouseButton', button: 1, pressed: true },
      { type: 'key', code: 'KeyW', pressed: true, modifiers: 2 },
      { type: 'key', code: 'Space', pressed: false },
      { type: 'key', code: 'unknown', pressed: true },
      { type: 'scroll', delta: -1 },
    ])
      panel.receive(message);
    expect(client.mouseMove.mock.calls).toEqual([
      [2, 3],
      [-2, -3],
    ]);
    expect(client.mouseButton).toHaveBeenCalledWith(1, true);
    expect(client.keyEx.mock.calls).toEqual([
      [0x57, true, 2],
      [0x20, false, 0],
    ]);
    expect(client.scroll).toHaveBeenCalledWith(-1);
    view.reveal();
    expect(panel.reveal).toHaveBeenCalledOnce();
    panel.visible = false;
    panel.state();
    await settle();
    expect(client.stopRenderLoop).toHaveBeenCalledOnce();
    panel.visible = true;
    panel.state();
    await settle();
    expect(client.startRenderLoop).toHaveBeenCalledTimes(2);
    const disposed = vi.fn();
    view.onDidDispose(disposed);
    view.dispose();
    await settle();
    view.dispose();
    expect(client.destroyViewport).toHaveBeenCalledOnce();
    expect(disposed).toHaveBeenCalledOnce();
    panel.webview.postMessage.mockClear();
    stats({ fps: 0, frameMs: 0, drawCalls: 0, vertices: 0, triangles: 0, frameIndex: 0 });
    frame('late');
    panel.receive({ type: 'ready' });
    await settle();
    expect(panel.webview.postMessage).not.toHaveBeenCalled();
  });

  it('tolerates an existing viewport, unavailable codec data and teardown errors', async () => {
    client.createViewport.mockRejectedValue(new Error('exists'));
    client.getCodecExtradata.mockRejectedValue(new Error('not ready'));
    client.stopRenderLoop.mockRejectedValue(new Error('worker stopped'));
    client.destroyViewport.mockRejectedValue(new Error('worker stopped'));
    panel.receive({ type: 'ready' });
    await settle();
    expect(client.startRenderLoop).toHaveBeenCalledOnce();
    view.dispose();
    await settle();
    expect(client.destroyViewport).toHaveBeenCalledOnce();
  });

  it('discovers usable workspace projects while skipping corrupt, missing and deeply nested candidates', async () => {
    const root = mkdtempSync(join(tmpdir(), 'uz-viewport-test-'));
    temporary.push(root);
    mkdirSync(join(root, 'projects'));
    mkdirSync(join(root, 'content'));
    mkdirSync(join(root, 'nested', 'too', 'deep'), { recursive: true });
    mkdirSync(join(root, 'node_modules'));
    mkdirSync(join(root, '.hidden'));
    writeFileSync(join(root, 'invalid.uzproject'), '{broken');
    writeFileSync(join(root, 'empty.uzproject'), '{}');
    writeFileSync(join(root, 'missing.uzproject'), '{"default_scene":"absent"}');
    writeFileSync(join(root, 'projects', 'valid.uzproject'), '{"default_scene":"scene.uzscene"}');
    writeFileSync(join(root, 'content', 'scene.uzscene'), '{}');
    ui.workspace.workspaceFolders = [
      { uri: { fsPath: join(root, 'absent-directory') } },
      { uri: { fsPath: root } },
    ];
    panel.receive({ type: 'ready' });
    await settle();
    expect(client.setContentRoot).toHaveBeenCalledWith(join(root, 'content'));
    expect(client.loadScene).toHaveBeenCalledWith(join(root, 'content', 'scene.uzscene'));
    expect(refresh).toHaveBeenCalledOnce();
  });
});

describe('scene editor session and failure contracts', () => {
  let panel: Panel;
  let client: ReturnType<typeof nativeClient>;
  let provider: SceneEditorProvider;
  let log: Record<'info' | 'warn' | 'error' | 'debug', ReturnType<typeof vi.fn>>;
  let refresh: ReturnType<typeof vi.fn>;
  function open(doc = document(), target = panel): void {
    provider.resolveCustomTextEditor(doc, target.asVscode(), {} as vscode.CancellationToken);
  }
  beforeEach(() => {
    panel = new Panel();
    client = nativeClient();
    refresh = vi.fn();
    log = { info: vi.fn(), warn: vi.fn(), error: vi.fn(), debug: vi.fn() };
    provider = new SceneEditorProvider(
      () => client as unknown as RuntimeClient,
      { refresh } as unknown as SceneTreeProvider,
      log as unknown as vscode.LogOutputChannel,
    );
  });

  it('loads the scene after content root and viewport setup, streams frames and forwards input', async () => {
    open();
    expect(panel.webview.html).toContain('Test Scene');
    panel.state();
    panel.receive({ type: 'mouseButton', button: 1, pressed: true });
    expect(client.mouseButton).not.toHaveBeenCalled();
    panel.receive({ type: 'resize', width: 800, height: 600 });
    panel.receive({ type: 'resize', width: 0, height: 0 });
    panel.receive({ type: 'ready' });
    panel.receive({ type: 'ready' });
    await settle();
    expect(client.init).toHaveBeenCalledOnce();
    expect(client.createViewport).toHaveBeenCalledWith(0, 800, 600);
    expect(client.setContentRoot).toHaveBeenCalledWith('/project/content');
    expect(client.setContentRoot.mock.invocationCallOrder[0]).toBeLessThan(
      client.init.mock.invocationCallOrder[0],
    );
    expect(client.loadScene.mock.invocationCallOrder[0]).toBeLessThan(
      client.startRenderLoop.mock.invocationCallOrder[0],
    );
    expect(refresh).toHaveBeenCalledOnce();
    const stats = client.startRenderLoop.mock.calls[0][1] as StatsCallback;
    const frame = client.startRenderLoop.mock.calls[0][2] as FrameCallback;
    for (let i = 0; i < 31; i++) {
      stats({ fps: 120, frameMs: 8, drawCalls: 2, vertices: 6, triangles: 2, frameIndex: i });
      frame('encoded');
    }
    expect(panel.webview.postMessage).toHaveBeenCalledWith({ type: 'frame', data: 'encoded' });
    vi.spyOn(Date, 'now').mockReturnValue(1000);
    for (const message of [
      { type: 'ready' },
      { type: 'mouseMove', x: 2, y: 3 },
      { type: 'mouseMove', x: 4, y: 5 },
      { type: 'mouseButton', button: 1, pressed: true },
      { type: 'key', keycode: 32, pressed: false },
      { type: 'scroll', delta: -1 },
      { type: 'decoderError', message: 'decode' },
      { type: 'resize', width: 900, height: 700 },
      { type: 'resize', width: 0, height: 0 },
    ])
      panel.receive(message);
    expect(client.mouseMove).toHaveBeenCalledExactlyOnceWith(2, 3);
    expect(client.key).toHaveBeenCalledWith(32, false);
    expect(client.resizeViewport).toHaveBeenCalledExactlyOnceWith(900, 700);
    panel.visible = false;
    panel.state();
    await settle();
    panel.receive({ type: 'ready' });
    await settle();
    expect(client.startRenderLoop).toHaveBeenCalledTimes(2);
    panel.visible = false;
    panel.state();
    await settle();
    panel.visible = true;
    panel.state();
    await settle();
    panel.state();
    expect(client.startRenderLoop).toHaveBeenCalledTimes(3);
    panel.close();
    await settle();
    expect(client.destroyViewport).toHaveBeenCalledTimes(2);
  });

  it.each([new Error('scene invalid'), 'scene invalid'])(
    'does not start rendering if scene loading fails (%s)',
    async (error) => {
      client.loadScene.mockRejectedValue(error);
      open();
      panel.receive({ type: 'ready' });
      await settle();
      expect(client.startRenderLoop).not.toHaveBeenCalled();
      expect(ui.showErrorMessage).toHaveBeenCalledWith('UzEngine scene load failed: scene invalid');
    },
  );

  it.each([new Error('GPU unavailable'), 'GPU unavailable'])(
    'reports viewport failure (%s)',
    async (error) => {
      client.createViewport.mockRejectedValue(error);
      open();
      panel.receive({ type: 'ready' });
      await settle();
      expect(client.loadScene).not.toHaveBeenCalled();
      expect(ui.showErrorMessage).toHaveBeenCalledWith(
        'UzEngine viewport creation failed: GPU unavailable',
      );
    },
  );

  it.each([new Error('root unavailable'), 'root unavailable'])(
    'stops if reapplying content root fails (%s)',
    async (error) => {
      client.setContentRoot.mockResolvedValueOnce(undefined).mockRejectedValueOnce(error);
      open();
      panel.receive({ type: 'ready' });
      await settle();
      expect(client.loadScene).not.toHaveBeenCalled();
      expect(ui.showWarningMessage).toHaveBeenCalledWith(
        'UzEngine setContentRoot failed: root unavailable',
      );
    },
  );

  it.each([new Error('worker stopped'), 'worker stopped'])(
    'handles render and shutdown failures (%s)',
    async (error) => {
      client.init.mockRejectedValue(error);
      client.destroyViewport.mockRejectedValue(error);
      client.startRenderLoop.mockRejectedValueOnce(error);
      client.stopRenderLoop.mockRejectedValue(error);
      open(document('standalone.uzscene', '{broken'));
      panel.receive({ type: 'ready' });
      await settle();
      expect(panel.webview.html).toContain('Unknown Scene');
      expect(client.setContentRoot).not.toHaveBeenCalled();
      expect(log.error).toHaveBeenCalledWith(
        '[SceneEditor] startRenderLoop FAILED: worker stopped',
      );
      panel.receive({ type: 'ready' });
      await settle();
      panel.close();
      await settle();
      expect(log.warn).toHaveBeenCalledWith('[SceneEditor] stopRenderLoop: worker stopped');
    },
  );

  it('ignores old messages and abandons initialization when another document takes over', async () => {
    let release: (value?: unknown) => void = () => {};
    client.setContentRoot.mockImplementationOnce(
      () =>
        new Promise((resolve) => {
          release = resolve;
        }),
    );
    open(document(undefined, '{}'));
    panel.receive({ type: 'ready' });
    const next = new Panel();
    open(document(), next);
    panel.receive({ type: 'ready' });
    release();
    await settle();
    expect(client.createViewport).not.toHaveBeenCalled();
    next.receive({ type: 'ready' });
    await settle();
    expect(client.createViewport).toHaveBeenCalledOnce();
    const third = new Panel();
    open(document(), third);
    expect(client.restart).toHaveBeenCalledOnce();
    third.close();
  });
});
