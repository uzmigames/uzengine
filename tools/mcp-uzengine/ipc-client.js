/**
 * ipc-client.js — Low-level client for UzEngine file-based IPC protocol.
 *
 * Reads/writes files in build/Release/preview/ to communicate with the
 * running UzEngine.exe process. If the engine is not running, operations
 * degrade gracefully (uz_status returns alive:false, uz_call times out).
 */

import { randomUUID } from 'crypto';
import fs from 'fs';
import path from 'path';

// Resolve the preview directory relative to this script's location.
// Script lives at <project>/tools/mcp-uzengine/ipc-client.js
// so we go up 3 levels to reach the project root.
import { fileURLToPath } from 'url';
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.resolve(__dirname, '..', '..');
const BUILD_RELEASE = path.join(PROJECT_ROOT, 'build', 'Release');

/**
 * Resolve the IPC preview directory for a given port.
 * Port 9002 (default) maps to the legacy "preview/" for backward compat.
 * Any other port maps to "preview-PORT/".
 * @param {number} [port=9002]
 * @returns {string} Absolute path to the preview directory for this port.
 */
export function getPreviewDir(port = 9002) {
    const dirName = port === 9002 ? 'preview' : `preview-${port}`;
    return path.join(BUILD_RELEASE, dirName);
}

// Default preview dir — kept for PREVIEW_DIR export (used by index.js for screenshot path).
const PREVIEW_DIR = getPreviewDir(9002);

const DEFAULT_TIMEOUT_MS = 4000;
const POLL_INTERVAL_MS = 50;

/**
 * Write command.json and poll response.json until the matching ID appears.
 * @param {string} fn - Engine function name (e.g. "engine.ping")
 * @param {object} args - Arguments object
 * @param {number} timeoutMs - Max wait in milliseconds
 * @param {number} [port=9002] - Engine instance port (selects IPC directory)
 * @returns {Promise<object>} Parsed response result field
 */
export async function engineCall(fn, args = {}, timeoutMs = DEFAULT_TIMEOUT_MS, port = 9002) {
    const previewDir = getPreviewDir(port);
    const commandFile = path.join(previewDir, 'command.json');
    const responseFile = path.join(previewDir, 'response.json');

    const id = randomUUID();
    const command = JSON.stringify({ id, type: 'call', function: fn, args });

    // Ensure directory exists (new instances may not have created it yet)
    fs.mkdirSync(previewDir, { recursive: true });

    // Write command atomically via temp file
    const tmpPath = commandFile + '.tmp';
    fs.writeFileSync(tmpPath, command, 'utf8');
    fs.renameSync(tmpPath, commandFile);

    // Poll response.json for matching ID
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        await sleep(POLL_INTERVAL_MS);
        try {
            const raw = fs.readFileSync(responseFile, 'utf8');
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
 * @param {number} [port=9002] - Engine instance port (selects IPC directory)
 * @returns {object}
 */
export function readStatus(port = 9002) {
    const statusFile = path.join(getPreviewDir(port), 'status.json');
    try {
        const raw = fs.readFileSync(statusFile, 'utf8');
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
