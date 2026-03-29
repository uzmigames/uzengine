/// UzEngine Console panel — streams UZ_LOG() messages from the C++ runtime.
///
/// In the original architecture, this class installed a ThreadSafeFunction
/// directly on the .node addon. In the isolated-worker architecture, log
/// messages are forwarded from the worker process via IPC and delivered here
/// through `RuntimeClient.setLogListener()`.
///
/// The console accepts either a `RuntimeClient` or an addon-compatible object
/// exposing a `setLogCallback` method so that it can be wired up regardless of
/// which execution model is active.
///
/// The output channel is created immediately at extension activation time so
/// that early messages (before the worker is alive) are never dropped.

import * as vscode from 'vscode';
import type { RuntimeClient } from '../runtime-client';

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
/// On `dispose()`, the log listener is unregistered and the channel is freed.
///
/// # Client lifetime
/// The constructor does NOT require the `RuntimeClient` — the output channel
/// is created immediately so messages printed before the worker starts are not
/// lost. Call `attachClient(client)` once the `RuntimeClient` exists to wire
/// up the log forwarding. Calling `attachClient` more than once is safe
/// (replaces the previous listener).
export class EngineConsole implements vscode.Disposable {
    private readonly channel: vscode.OutputChannel;
    /// The client whose log events are currently being forwarded into this channel.
    private clientRef: RuntimeClient | undefined;

    constructor() {
        this.channel = vscode.window.createOutputChannel('UzEngine Console');
    }

    /// Wires log forwarding from the given `RuntimeClient` into this channel.
    ///
    /// Replaces any previously attached client. The previous client's listener
    /// is cleared before the new one is set.
    attachClient(client: RuntimeClient): void {
        // Clear listener on the previous client if one was attached.
        if (this.clientRef && this.clientRef !== client) {
            this.clientRef.setLogListener(undefined);
        }

        this.clientRef = client;
        client.setLogListener((level: number, category: string, message: string) => {
            const levelName = LEVEL_NAMES[level] ?? `Level${level}`;
            // ISO 8601 slice — "2026-03-25T12:34:56.789Z" → "12:34:56.789"
            const timestamp = new Date().toISOString().slice(11, 23);
            this.channel.appendLine(
                `[${timestamp}] [${category}] [${levelName}] ${message}`
            );
        });
    }

    dispose(): void {
        // Unregister the log listener so the RuntimeClient stops referencing
        // the JS function after the channel is freed.
        if (this.clientRef) {
            try {
                this.clientRef.setLogListener(undefined);
            } catch {
                // Ignore errors if the client was already torn down.
            }
            this.clientRef = undefined;
        }
        this.channel.dispose();
    }
}
