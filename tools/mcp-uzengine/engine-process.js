/**
 * engine-process.js — Manages the UzEngine.exe child process lifecycle.
 *
 * Resolves the exe path by searching standard build output locations,
 * spawns it headless, and exposes kill/restart primitives.
 */

import { spawn } from 'child_process';
import fs from 'fs';
import path from 'path';

const WORKSPACE = process.cwd();

const EXE_CANDIDATES = [
    path.join(WORKSPACE, 'build', 'Release', 'UzEngine.exe'),
    path.join(WORKSPACE, 'build', 'Debug', 'UzEngine.exe'),
    path.join(WORKSPACE, 'build', 'UzEngine.exe'),
    path.join(WORKSPACE, 'UzEngine.exe'),
];

/** @type {import('child_process').ChildProcess | null} */
let engineProcess = null;

/**
 * Resolve the UzEngine executable path.
 * @returns {string|null}
 */
export function getEngineExePath() {
    for (const candidate of EXE_CANDIDATES) {
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return null;
}

/**
 * Spawn UzEngine.exe.
 * @param {{ windowed?: boolean }} [opts]
 *   windowed=true  → sets UZENGINE_WINDOWED=1 so a visible debug window appears
 * No-op if already running.
 * @returns {{ ok: boolean, pid?: number, error?: string }}
 */
export function spawnEngine({ windowed = false } = {}) {
    if (engineProcess && engineProcess.exitCode === null) {
        return { ok: true, pid: engineProcess.pid, note: 'already running' };
    }

    const exePath = getEngineExePath();
    if (!exePath) {
        return { ok: false, error: 'UzEngine.exe not found — build the project first' };
    }

    const env = { ...process.env, UZENGINE_HEADLESS: '1' };
    if (windowed) env.UZENGINE_WINDOWED = '1';

    try {
        engineProcess = spawn(exePath, [], {
            cwd: path.dirname(exePath),
            detached: false,
            windowsHide: false,
            env,
        });

        engineProcess.on('exit', () => { engineProcess = null; });
        engineProcess.on('error', () => { engineProcess = null; });

        return { ok: true, pid: engineProcess.pid, exe: exePath };
    } catch (err) {
        engineProcess = null;
        return { ok: false, error: String(err) };
    }
}

/**
 * Kill the currently running UzEngine process.
 * @returns {{ ok: boolean, note?: string }}
 */
export function killEngine() {
    if (!engineProcess || engineProcess.exitCode !== null) {
        return { ok: true, note: 'engine was not running' };
    }
    try {
        engineProcess.kill('SIGTERM');
        engineProcess = null;
        return { ok: true };
    } catch (err) {
        return { ok: false, error: String(err) };
    }
}

/**
 * Returns true if an engine process spawned by this MCP server is alive.
 * @returns {boolean}
 */
export function isEngineRunning() {
    return engineProcess !== null && engineProcess.exitCode === null;
}