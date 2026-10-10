/// UzEngine engine-mode toggle — status bar button that switches between
/// "Code Mode" (normal VSCode editing) and "Engine Mode" (viewport + scene tree).
///
/// Persists the current mode in `workspaceState` so it survives extension
/// restarts within the same workspace session.

import * as vscode from 'vscode';

// ── Constants ─────────────────────────────────────────────────────────────────

/** Key used to persist engine-mode state across extension restarts. */
const STATE_KEY = 'uzengine.engineModeActive';

// ── EngineMode ────────────────────────────────────────────────────────────────

/// Manages the status bar item that toggles between Code Mode and Engine Mode.
///
/// The item is created in the constructor and disposed via VSCode's subscriptions
/// mechanism — pass the item to `context.subscriptions.push` through this class.
///
/// In Engine Mode the extension automatically opens the viewport panel and
/// focuses the Scene Hierarchy view so the user sees the engine layout
/// immediately without manual panel management.
export class EngineMode {
  private readonly statusBarItem: vscode.StatusBarItem;
  private isEngineMode: boolean;

  constructor(private readonly context: vscode.ExtensionContext) {
    // Restore persisted state from the previous session.
    this.isEngineMode = context.workspaceState.get<boolean>(STATE_KEY, false);

    this.statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    this.statusBarItem.command = 'uzengine.toggleEngineMode';
    this.updateStatusBar();
    this.statusBarItem.show();

    // Register for disposal via the extension context.
    context.subscriptions.push(this.statusBarItem);
  }

  // ── Public API ────────────────────────────────────────────────────────────

  /// Toggles between Code Mode and Engine Mode.
  ///
  /// When switching TO Engine Mode:
  ///   - Opens the viewport panel (`uzengine.openViewport`)
  ///   - Focuses the Scene Hierarchy view (`uzengine.sceneTree.focus`)
  ///
  /// When switching BACK to Code Mode the layout is left as-is; the user
  /// can rearrange panels manually. A notification confirms the change.
  toggle(): void {
    this.isEngineMode = !this.isEngineMode;
    // Persist so the mode survives extension restarts.
    void this.context.workspaceState.update(STATE_KEY, this.isEngineMode);
    this.updateStatusBar();

    if (this.isEngineMode) {
      // Open the viewport and bring the scene tree into focus.
      void vscode.commands.executeCommand('uzengine.openViewport');
      void vscode.commands.executeCommand('uzengine.sceneTree.focus');
    }

    vscode.window.showInformationMessage(
      this.isEngineMode ? 'Engine Mode activated' : 'Code Mode activated',
    );
  }

  // ── Private helpers ───────────────────────────────────────────────────────

  private updateStatusBar(): void {
    if (this.isEngineMode) {
      this.statusBarItem.text = '$(game) Engine Mode';
      this.statusBarItem.tooltip = 'UzEngine: Engine Mode active — click to switch to Code Mode';
      this.statusBarItem.backgroundColor = new vscode.ThemeColor('statusBarItem.warningBackground');
    } else {
      this.statusBarItem.text = '$(code) Code Mode';
      this.statusBarItem.tooltip = 'UzEngine: Code Mode active — click to switch to Engine Mode';
      this.statusBarItem.backgroundColor = undefined;
    }
  }
}
