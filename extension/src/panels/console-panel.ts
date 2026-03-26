/// UzEngine Console panel — streams UZ_LOG() messages from the C++ runtime.
///
/// Installs a ThreadSafeFunction via `addon.setLogCallback()`. The native
/// addon marshals log events produced on any internal thread back to the
/// Node.js event loop, where this class formats and writes them to a VSCode
/// OutputChannel.

import * as vscode from 'vscode';
import type { UzEngineAddon } from '../uzengine';

// ── Constants ─────────────────────────────────────────────────────────────────

/// Human-readable names for the numeric log levels defined in `LogLevel`.
/// Index matches the `level` argument passed to `LogCallback`.
const LEVEL_NAMES: readonly string[] = [
    'Trace',    // 0
    'Debug',    // 1
    'Info',     // 2
    'Warning',  // 3
    'Error',    // 4
    'Fatal',    // 5
];

// ── EngineConsole ─────────────────────────────────────────────────────────────

/// Manages the "UzEngine Console" output channel and routes runtime log events
/// into it.
///
/// A single `EngineConsole` instance should exist per extension activation.
/// On `dispose()`, the log callback is unregistered and the channel is freed.
export class EngineConsole implements vscode.Disposable {
    private readonly channel: vscode.OutputChannel;

    constructor(private readonly addon: UzEngineAddon) {
        this.channel = vscode.window.createOutputChannel('UzEngine Console');

        addon.setLogCallback((level: number, category: string, message: string) => {
            const levelName = LEVEL_NAMES[level] ?? `Level${level}`;
            // ISO 8601 slice — "2026-03-25T12:34:56.789Z" → "12:34:56.789"
            const timestamp = new Date().toISOString().slice(11, 23);
            this.channel.appendLine(
                `[${timestamp}] [${category}] [${levelName}] ${message}`
            );
        });
    }

    dispose(): void {
        // Unregister the callback so the native addon stops referencing the
        // JS function after the channel is gone.
        try {
            this.addon.setLogCallback(null);
        } catch {
            // Ignore errors if the addon was already torn down.
        }
        this.channel.dispose();
    }
}
