import { beforeEach, describe, expect, it, vi } from 'vitest';
import type * as vscode from 'vscode';

const ui = vi.hoisted(() => ({
  createStatusBarItem: vi.fn(),
  executeCommand: vi.fn(),
  showInformationMessage: vi.fn(),
}));

vi.mock('vscode', () => ({
  window: {
    createStatusBarItem: ui.createStatusBarItem,
    showInformationMessage: ui.showInformationMessage,
  },
  commands: { executeCommand: ui.executeCommand },
  StatusBarAlignment: { Left: 1 },
  ThemeColor: class {
    constructor(public readonly id: string) {}
  },
}));

import { EngineMode } from '../engine-mode';

describe('EngineMode workspace state and commands', () => {
  let status: {
    command: string;
    text: string;
    tooltip: string;
    backgroundColor: unknown;
    show: ReturnType<typeof vi.fn>;
    dispose: ReturnType<typeof vi.fn>;
  };
  let update: ReturnType<typeof vi.fn>;
  let subscriptions: vscode.Disposable[];

  function context(active?: boolean): vscode.ExtensionContext {
    return {
      workspaceState: {
        get: vi.fn((_key: string, fallback: boolean) => active ?? fallback),
        update,
      },
      subscriptions,
    } as unknown as vscode.ExtensionContext;
  }

  beforeEach(() => {
    vi.clearAllMocks();
    status = {
      command: '',
      text: '',
      tooltip: '',
      backgroundColor: undefined,
      show: vi.fn(),
      dispose: vi.fn(),
    };
    update = vi.fn().mockResolvedValue(undefined);
    subscriptions = [];
    ui.createStatusBarItem.mockReturnValue(status);
  });

  it('defaults to code mode and registers the visible toggle for disposal', () => {
    new EngineMode(context());
    expect(status.text).toBe('$(code) Code Mode');
    expect(status.command).toBe('uzengine.toggleEngineMode');
    expect(status.backgroundColor).toBeUndefined();
    expect(status.show).toHaveBeenCalledOnce();
    expect(subscriptions).toContain(status);
    expect(update).not.toHaveBeenCalled();
    expect(ui.executeCommand).not.toHaveBeenCalled();
  });

  it('restores persisted engine mode without reopening the viewport', () => {
    new EngineMode(context(true));
    expect(status.text).toBe('$(game) Engine Mode');
    expect(status.backgroundColor).toEqual({ id: 'statusBarItem.warningBackground' });
    expect(ui.executeCommand).not.toHaveBeenCalled();
  });

  it('persists activation and opens viewport before focusing the hierarchy', () => {
    const mode = new EngineMode(context(false));
    mode.toggle();
    expect(update).toHaveBeenCalledWith('uzengine.engineModeActive', true);
    expect(ui.executeCommand.mock.calls).toEqual([
      ['uzengine.openViewport'],
      ['uzengine.sceneTree.focus'],
    ]);
    expect(status.text).toBe('$(game) Engine Mode');
    expect(ui.showInformationMessage).toHaveBeenCalledWith('Engine Mode activated');
  });

  it('persists deactivation and clears status highlighting', () => {
    const mode = new EngineMode(context(true));
    mode.toggle();
    expect(update).toHaveBeenCalledWith('uzengine.engineModeActive', false);
    expect(status.text).toBe('$(code) Code Mode');
    expect(status.backgroundColor).toBeUndefined();
    expect(ui.executeCommand).not.toHaveBeenCalled();
    expect(ui.showInformationMessage).toHaveBeenCalledWith('Code Mode activated');
  });

  it('supports repeated toggling in one workspace session', () => {
    const mode = new EngineMode(context());
    mode.toggle();
    mode.toggle();
    mode.toggle();
    expect(update.mock.calls.map((call) => call[1])).toEqual([true, false, true]);
    expect(ui.executeCommand).toHaveBeenCalledTimes(4);
  });
});
