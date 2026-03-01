/**
 * ipc-client.js — Low-level client for UzEngine file-based IPC protocol.
 *
 * Reads/writes files in build/Release/preview/ to communicate with the
 * running UzEngine.exe process. If the engine is not running, operations
 * degrade gracefully (uz_status returns alive:false, uz_call times out).
 */

import fs from 'fs';
import path from 'path';
import { randomUUID } from 'crypto';

// Resolve the preview directory relative to the workspace root.
// When invoked via npx from the project root, process.cwd() is the workspace.
const PREVIEW_DIR = path.join(process.cwd(), 'build', 'Release', 'preview');
const COMMAND_FILE = path.join(PREVIEW_DIR, 'command.json');
const RESPONSE_FILE = path.join(PREVIEW_DIR, 'response.json');
const STATUS_FILE = path.join(PREVIEW_DIR, 'status.json');

const DEFAULT_TIMEOUT_MS = 4000;
const POLL_INTERVAL_MS = 50;

/**
 * Write command.json and poll response.json until the matching ID appears.
 * @param {string} fn - Engine function name (e.g. "engine.ping")
 * @param {object} args - Arguments object
 * @param {number} timeoutMs - Max wait in milliseconds
 * @returns {Promise<object>} Parsed response result field
 */
export async function engineCall(fn, args = {}, timeoutMs = DEFAULT_TIMEOUT_MS) {
    const id = randomUUID();
    const command = JSON.stringify({ id, type: 'call', function: fn, args });

    // Write command atomically via temp file
    const tmpPath = COMMAND_FILE + '.tmp';
    fs.writeFileSync(tmpPath, command, 'utf8');
    fs.renameSync(tmpPath, COMMAND_FILE);

    // Poll response.json for matching ID
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        await sleep(POLL_INTERVAL_MS);
        try {
            const raw = fs.readFileSync(RESPONSE_FILE, 'utf8');
            const resp = JSON.parse(raw);
            if (resp.id === id) {
                if (!resp.ok) {
                    return { ok: false, error: resp.error ?? 'unknown error' };
                }
                return { ok: true, result: resp.result ?? null };
            }
        } catch {
            // File not yet written or not valid JSON — keep polling
        }
    }

    return { ok: false, error: `timeout after ${timeoutMs}ms waiting for engine response` };
}

/**
 * Read status.json and return its contents, or a degraded status if unavailable.
 * @returns {object}
 */
export function readStatus() {
    try {
        const raw = fs.readFileSync(STATUS_FILE, 'utf8');
        return JSON.parse(raw);
    } catch {
        return { alive: false, error: 'status.json not found — is UzEngine running?' };
    }
}

/**
 * Wait for a file to appear (or be updated) within the timeout.
 * @param {string} filePath
 * @param {number} timeoutMs
 * @param {number} [sinceMs] - Only accept files modified after this epoch ms
 * @returns {Promise<boolean>}
 */
export async function waitForFile(filePath, timeoutMs, sinceMs = 0) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        try {
            const stat = fs.statSync(filePath);
            if (stat.mtimeMs >= sinceMs) {
                return true;
            }
        } catch {
            // File doesn't exist yet
        }
        await sleep(POLL_INTERVAL_MS);
    }
    return false;
}

/**
 * Read a file and return its contents as a Buffer.
 * @param {string} filePath
 * @returns {Buffer}
 */
export function readFile(filePath) {
    return fs.readFileSync(filePath);
}

export { PREVIEW_DIR, randomUUID };

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}
