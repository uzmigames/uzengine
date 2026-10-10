import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type * as vscode from 'vscode';

const api = vi.hoisted(() => ({
  createOutputChannel: vi.fn(),
  createTreeView: vi.fn(),
  registerCustomEditorProvider: vi.fn(),
  showInformationMessage: vi.fn(),
  showErrorMessage: vi.fn(),
  showOpenDialog: vi.fn(),
  registerCommand: vi.fn(),
  Client: vi.fn(),
  Viewport: vi.fn(),
  Tree: vi.fn(),
  Console: vi.fn(),
  Mode: vi.fn(),
  Editor: vi.fn(),
}));
vi.mock('vscode', () => ({ window: api, commands: { registerCommand: api.registerCommand } }));
vi.mock('../runtime-client', () => ({ RuntimeClient: api.Client }));
vi.mock('../panels/viewport-panel', () => ({ ViewportPanel: api.Viewport }));
vi.mock('../panels/scene-tree-panel', () => ({ SceneTreeProvider: api.Tree }));
vi.mock('../panels/console-panel', () => ({ EngineConsole: api.Console }));
vi.mock('../engine-mode', () => ({ EngineMode: api.Mode }));
vi.mock('../panels/scene-editor-provider', () => ({ SceneEditorProvider: api.Editor }));
import { activate, deactivate } from '../extension';

const settle = () => new Promise((resolve) => setImmediate(resolve));
describe('extension activation and user commands', () => {
  let commands: Map<string, (...args: unknown[]) => unknown>;
  let client: {
    init: ReturnType<typeof vi.fn>;
    setEngineDir: ReturnType<typeof vi.fn>;
    getVersion: ReturnType<typeof vi.fn>;
    loadScene: ReturnType<typeof vi.fn>;
    dispose: ReturnType<typeof vi.fn>;
  };
  let tree: { refresh: ReturnType<typeof vi.fn> };
  let viewport: {
    reveal: ReturnType<typeof vi.fn>;
    dispose: ReturnType<typeof vi.fn>;
    onDidDispose: ReturnType<typeof vi.fn>;
  };
  let console: { attachClient: ReturnType<typeof vi.fn> };
  let mode: { toggle: ReturnType<typeof vi.fn> };
  let log: { info: ReturnType<typeof vi.fn>; error: ReturnType<typeof vi.fn> };
  let context: vscode.ExtensionContext;
  beforeEach(() => {
    vi.resetAllMocks();
    commands = new Map();
    client = {
      init: vi.fn().mockResolvedValue(undefined),
      setEngineDir: vi.fn().mockResolvedValue(undefined),
      getVersion: vi.fn().mockResolvedValue('0.7.0'),
      loadScene: vi.fn().mockResolvedValue(undefined),
      dispose: vi.fn(),
    };
    tree = { refresh: vi.fn() };
    console = { attachClient: vi.fn() };
    mode = { toggle: vi.fn() };
    viewport = { reveal: vi.fn(), dispose: vi.fn(), onDidDispose: vi.fn() };
    log = { info: vi.fn(), error: vi.fn() };
    api.Client.mockImplementation(function () {
      return client;
    });
    api.Tree.mockImplementation(function () {
      return tree;
    });
    api.Console.mockImplementation(function () {
      return console;
    });
    api.Mode.mockImplementation(function () {
      return mode;
    });
    api.Viewport.mockImplementation(function () {
      return viewport;
    });
    api.Editor.mockImplementation(function () {
      return {};
    });
    api.createOutputChannel.mockReturnValue(log);
    api.createTreeView.mockReturnValue({ dispose: vi.fn() });
    api.registerCustomEditorProvider.mockReturnValue({ dispose: vi.fn() });
    api.registerCommand.mockImplementation(
      (name: string, command: (...args: unknown[]) => unknown) => {
        commands.set(name, command);
        return { dispose: vi.fn() };
      },
    );
    context = { subscriptions: [], extensionUri: {} } as unknown as vscode.ExtensionContext;
    activate(context);
  });
  afterEach(() => deactivate());
  const uri = { fsPath: 'scene.uzscene' };
  function run(name: string, ...args: unknown[]): unknown {
    return commands.get(`uzengine.${name}`)!(...args);
  }

  it('registers all commands and disposables while keeping native startup lazy', () => {
    expect([...commands.keys()]).toEqual([
      'uzengine.start',
      'uzengine.openViewport',
      'uzengine.openScene',
      'uzengine.toggleEngineMode',
      'uzengine.refreshSceneTree',
    ]);
    expect(api.Client).not.toHaveBeenCalled();
    expect(context.subscriptions).toContain(log);
    expect(context.subscriptions).toContain(console);
    const getter = api.Tree.mock.calls[0][0] as () => unknown;
    expect(getter()).toBeUndefined();
    run('toggleEngineMode');
    run('refreshSceneTree');
    expect(mode.toggle).toHaveBeenCalledOnce();
    expect(tree.refresh).toHaveBeenCalledOnce();
    deactivate();
    expect(client.dispose).not.toHaveBeenCalled();
  });

  it('initializes a single client, forwards logs and reports the runtime version', async () => {
    run('start');
    await settle();
    run('start');
    await settle();
    expect(api.Client).toHaveBeenCalledOnce();
    expect(console.attachClient).toHaveBeenCalledWith(client);
    expect(client.setEngineDir).toHaveBeenCalledWith(expect.stringMatching(/native$/));
    expect(api.showInformationMessage).toHaveBeenCalledWith('UzEngine 0.7.0 initialized');
    const getter = api.Tree.mock.calls[0][0] as () => unknown;
    expect(getter()).toBe(client);
    deactivate();
    deactivate();
    expect(client.dispose).toHaveBeenCalledOnce();
  });

  it.each([new Error('native error'), 'native error'])(
    'reports initialization failures (%s)',
    async (error) => {
      client.init.mockRejectedValue(error);
      client.setEngineDir.mockRejectedValue(error);
      run('start');
      await settle();
      expect(api.showErrorMessage).toHaveBeenCalledWith('UzEngine init failed: native error');
    },
  );

  it('reveals the existing viewport and recreates it after closure', () => {
    run('openViewport');
    run('openViewport');
    expect(api.Viewport).toHaveBeenCalledOnce();
    expect(viewport.reveal).toHaveBeenCalledOnce();
    (viewport.onDidDispose.mock.calls[0][0] as () => void)();
    run('openViewport');
    expect(api.Viewport).toHaveBeenCalledTimes(2);
    deactivate();
    expect(viewport.dispose).toHaveBeenCalledOnce();
  });

  it('loads a URI or selected scene and refreshes hierarchy only after success', async () => {
    await run('openScene', uri);
    expect(client.loadScene).toHaveBeenCalledWith(uri.fsPath);
    expect(tree.refresh).toHaveBeenCalledOnce();
    api.showOpenDialog.mockResolvedValue([uri]);
    await run('openScene');
    expect(viewport.reveal).toHaveBeenCalledOnce();
    (viewport.onDidDispose.mock.calls[0][0] as () => void)();
    await run('openScene', uri);
    expect(api.Viewport).toHaveBeenCalledTimes(2);
  });

  it.each([undefined, []])(
    'does not spawn a client if scene selection is canceled (%s)',
    async (selection) => {
      api.showOpenDialog.mockResolvedValue(selection);
      await run('openScene');
      expect(api.Client).not.toHaveBeenCalled();
    },
  );

  it.each([new Error('invalid scene'), 'invalid scene'])(
    'surfaces scene failures without refreshing stale state (%s)',
    async (error) => {
      client.loadScene.mockRejectedValue(error);
      await run('openScene', uri);
      expect(tree.refresh).not.toHaveBeenCalled();
      expect(api.showErrorMessage).toHaveBeenCalledWith('Failed to load scene: invalid scene');
    },
  );
});
