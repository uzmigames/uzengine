import { beforeEach, describe, expect, it, vi } from 'vitest';
import type * as vscode from 'vscode';
import type { RuntimeClient } from '../runtime-client';
import type { SceneTreeProvider } from '../panels/scene-tree-panel';

const ui = vi.hoisted(() => ({
  createWebviewPanel: vi.fn(),
  showErrorMessage: vi.fn(),
  showWarningMessage: vi.fn(),
}));
vi.mock('vscode', () => ({
  window: ui,
  workspace: { workspaceFolders: undefined },
  ViewColumn: { One: 1 },
  EventEmitter: class {
    event = () => ({ dispose() {} });
    fire() {}
    dispose() {}
  },
}));
import { SceneEditorProvider } from '../panels/scene-editor-provider';
import { ViewportPanel } from '../panels/viewport-panel';

/** Minimal `WebviewPanel` double that captures the HTML and message listener. */
class Panel {
  visible = true;
  receive: (message: unknown) => void = () => {};
  webview = {
    html: '',
    options: {} as vscode.WebviewOptions,
    postMessage: vi.fn().mockResolvedValue(true),
    onDidReceiveMessage: (callback: (message: unknown) => void) => {
      this.receive = callback;
    },
  };
  onDidDispose = vi.fn();
  onDidChangeViewState = vi.fn();
  dispose = vi.fn();
  asVscode(): vscode.WebviewPanel {
    return this as unknown as vscode.WebviewPanel;
  }
}

const runtimeMethods = [
  'init',
  'restart',
  'createViewport',
  'destroyViewport',
  'setEditorMode',
  'setContentRoot',
  'loadScene',
  'startRenderLoop',
  'stopRenderLoop',
  'getCodecExtradata',
  'resizeViewport',
  'requestKeyframe',
  'mouseMove',
  'mouseButton',
  'key',
  'keyEx',
  'scroll',
  'play',
] as const;
type Client = Record<(typeof runtimeMethods)[number], ReturnType<typeof vi.fn>>;
function runtimeClient(): Client {
  return Object.fromEntries(
    runtimeMethods.map((method) => [method, vi.fn().mockResolvedValue(undefined)]),
  ) as Client;
}
/** Every client call except the lifecycle calls made while opening a viewport. */
function inputCalls(client: Client): number {
  return (
    ['resizeViewport', 'requestKeyframe', 'mouseMove', 'mouseButton', 'key', 'keyEx', 'scroll']
      .map((method) => client[method as keyof Client].mock.calls.length)
      .reduce((a, b) => a + b, 0) + client.play.mock.calls.length
  );
}
function sceneDocument(text: string): vscode.TextDocument {
  return {
    uri: { fsPath: '/project/content/scenes/test.uzscene' },
    getText: () => text,
  } as unknown as vscode.TextDocument;
}
const settle = () => new Promise((resolve) => setImmediate(resolve));

/** Parses the page CSP and asserts every script/style element carries its nonce. */
function assertNonceCsp(html: string): string {
  const csp = /<meta http-equiv="Content-Security-Policy" content="([^"]+)">/.exec(html)![1];
  const directives = new Map(
    csp.split(';').map((d) => {
      const [name, ...values] = d.trim().split(/\s+/);
      return [name, values] as const;
    }),
  );
  const scriptSrc = directives.get('script-src')!;
  expect(scriptSrc).toHaveLength(1);
  const nonce = /^'nonce-([A-Za-z0-9+/=]+)'$/.exec(scriptSrc[0])![1];
  expect(Buffer.from(nonce, 'base64')).toHaveLength(16);
  expect(csp).not.toContain('unsafe-inline');
  expect(csp).not.toContain('unsafe-eval');
  expect(directives.get('default-src')).toEqual(["'none'"]);
  expect(directives.get('style-src')).toEqual([`'nonce-${nonce}'`]);
  expect(directives.get('base-uri')).toEqual(["'none'"]);
  const tags = html.match(/<(script|style)\b[^>]*>/g)!;
  expect(tags).toHaveLength(2);
  expect([...tags].sort()).toEqual([`<script nonce="${nonce}">`, `<style nonce="${nonce}">`]);
  return nonce;
}

describe('scene editor webview hardening', () => {
  let client: Client;
  let log: Record<'info' | 'warn' | 'error' | 'debug', ReturnType<typeof vi.fn>>;
  let provider: SceneEditorProvider;
  function open(text: string, panel = new Panel()): Panel {
    provider.resolveCustomTextEditor(
      sceneDocument(text),
      panel.asVscode(),
      {} as vscode.CancellationToken,
    );
    return panel;
  }
  beforeEach(() => {
    vi.clearAllMocks();
    client = runtimeClient();
    log = { info: vi.fn(), warn: vi.fn(), error: vi.fn(), debug: vi.fn() };
    provider = new SceneEditorProvider(
      () => client as unknown as RuntimeClient,
      { refresh: vi.fn() } as unknown as SceneTreeProvider,
      log as unknown as vscode.LogOutputChannel,
    );
  });

  it('escapes a hostile scene name in the title and toolbar so no injected script exists', () => {
    const name = `</title><script>vscode.postMessage({type:'play'})</script><img src=x onerror="alert(1)">`;
    const html = open(JSON.stringify({ name })).webview.html;
    const escaped =
      '&lt;/title&gt;&lt;script&gt;vscode.postMessage({type:&#39;play&#39;})&lt;/script&gt;' +
      '&lt;img src=x onerror=&quot;alert(1)&quot;&gt;';
    expect(html).toContain(`<title>${escaped}</title>`);
    expect(html).toContain(`<span class="scene-name">${escaped}</span>`);
    expect(html).not.toContain('<img');
    expect(html.match(/<script\b/g)).toHaveLength(1);
    assertNonceCsp(html);
  });

  it('uses a fresh nonce per render and denies all local resource roots', () => {
    const first = open('{"name":"A"}');
    const second = open('{"name":"A"}');
    expect(assertNonceCsp(first.webview.html)).not.toBe(assertNonceCsp(second.webview.html));
    expect(first.webview.options).toEqual({ enableScripts: true, localResourceRoots: [] });
  });

  it.each(['42', 'null', '[]', '{}', '{"name":42}', '{"name":{"x":1}}', '{"name":""}', '{oops'])(
    'falls back to the default title for scene JSON %s',
    (text) => {
      expect(open(text).webview.html).toContain('<title>Unknown Scene</title>');
    },
  );

  it('rejects malformed messages before and after the viewport exists without reaching the runtime', async () => {
    const panel = open('{"name":"Test"}');
    panel.receive({ type: 'resize', width: 1e9, height: 1e9 });
    panel.receive({ type: 'ready', width: 1 });
    await settle();
    expect(client.createViewport).toHaveBeenCalledExactlyOnceWith(0, 1280, 720);
    for (const message of [
      null,
      'ready',
      { type: 'play' },
      { type: 'loadScene', path: 'C:/secret.uzscene' },
      { type: 'mouseMove', x: Number.NaN, y: 0 },
      { type: 'mouseButton', button: '0', pressed: true },
      { type: 'key', keycode: 0x10000 + 0x57, pressed: true },
      { type: 'scroll', delta: Number.POSITIVE_INFINITY },
      { type: 'resize', width: 16385, height: 720 },
    ])
      panel.receive(message);
    expect(inputCalls(client)).toBe(0);
    expect(log.warn).toHaveBeenCalledTimes(10);
    expect(log.warn).toHaveBeenCalledWith('[SceneEditor] rejected malformed webview message');
    panel.receive({ type: 'key', keycode: 0x57, pressed: true });
    expect(client.key).toHaveBeenCalledExactlyOnceWith(0x57, true);
  });
});

describe('viewport webview hardening', () => {
  let client: Client;
  let panel: Panel;
  beforeEach(() => {
    vi.clearAllMocks();
    client = runtimeClient();
    panel = new Panel();
    ui.createWebviewPanel.mockReturnValue(panel.asVscode());
    new ViewportPanel(
      {} as vscode.Uri,
      client as unknown as RuntimeClient,
      {
        refresh: vi.fn(),
      } as unknown as SceneTreeProvider,
    );
  });

  it('ships a nonce-only CSP with the nonce on every inline script and style', () => {
    assertNonceCsp(panel.webview.html);
    expect(panel.webview.html).not.toMatch(/\son[a-z]+=|\sstyle=/);
  });

  it('drops malformed messages and inherited key names instead of forwarding them', async () => {
    for (const message of [
      undefined,
      { type: 'play' },
      { type: 'resize', width: 1e9, height: 1e9 },
      { type: 'mouseDelta', dx: Number.NaN, dy: 0 },
      { type: 'mouseButton', button: 9, pressed: true },
      { type: 'key', code: 'KeyW', pressed: true, modifiers: 1 << 20 },
      { type: 'key', code: 'constructor', pressed: true },
      { type: 'key', code: '__proto__', pressed: true },
      { type: 'key', code: 'toString', pressed: false },
      { type: 'scroll', delta: '1' },
    ])
      panel.receive(message);
    await settle();
    expect(inputCalls(client)).toBe(0);
    expect(client.createViewport).not.toHaveBeenCalled();
    panel.receive({ type: 'key', code: 'KeyW', pressed: true, modifiers: 3 });
    expect(client.keyEx).toHaveBeenCalledExactlyOnceWith(0x57, true, 3);
  });
});
