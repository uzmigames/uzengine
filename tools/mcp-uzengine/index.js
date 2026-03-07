#!/usr/bin/env node
/**
 * @uzmigames/uzengine-mcp
 *
 * MCP server (stdio transport) exposing 5 stable primitive tools for
 * interacting with a running UzEngine instance via the file-based IPC.
 *
 * Tool surface is intentionally minimal and never grows with engine features.
 * High-level recipes (VFX, shaders, cameras) live in Rulebook Skills.
 *
 * Tools:
 *   uz_call        — call any registered engine function
 *   uz_screenshot  — capture current frame as base64 PNG
 *   uz_status      — read real-time engine metrics
 *   uz_read_file   — read a project file
 *   uz_write_file  — write a project file
 *   uz_restart     — kill and restart UzEngine.exe
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import {
    CallToolRequestSchema,
    ListToolsRequestSchema,
} from '@modelcontextprotocol/sdk/types.js';

import { execSync } from 'child_process';
import { randomUUID } from 'crypto';
import path from 'path';
import { killEngine, spawnEngine } from './engine-process.js';
import { engineCall, getPreviewDir, readFile, readStatus, waitForFile } from './ipc-client.js';

// ---------------------------------------------------------------------------
// Tool definitions
// ---------------------------------------------------------------------------

const TOOLS = [
    {
        name: 'uz_call',
        description:
            'Call any registered UzEngine function via the IPC protocol. ' +
            'Use this to control scene parameters, trigger actions, or query engine state. ' +
            'Returns {ok, result, error}.',
        inputSchema: {
            type: 'object',
            properties: {
                function: {
                    type: 'string',
                    description:
                        'Engine function name, e.g. "engine.ping", "scene.setRotation", ' +
                        '"scene.setBackground", "scene.setPaused", "file.read", "file.write", ' +
                        '"file.list", "engine.screenshot", "shader.reload".',
                },
                args: {
                    type: 'object',
                    description: 'Arguments to pass to the function. Optional.',
                    default: {},
                },
                timeout_ms: {
                    type: 'number',
                    description: 'Max wait time in milliseconds. Default: 4000.',
                    default: 4000,
                },
                port: {
                    type: 'number',
                    description: 'WebSocket port of the target engine instance. Default: 9002.',
                    default: 9002,
                },
            },
            required: ['function'],
        },
    },
    {
        name: 'uz_screenshot',
        description:
            'Capture the current rendered frame as a PNG image. ' +
            'Returns the image as base64 so you can visually inspect the scene. ' +
            'Use this to verify visual changes after calling uz_call.',
        inputSchema: {
            type: 'object',
            properties: {
                wait_ms: {
                    type: 'number',
                    description: 'Max time to wait for PNG to be written. Default: 4000.',
                    default: 4000,
                },
                port: {
                    type: 'number',
                    description: 'WebSocket port of the target engine instance. Default: 9002.',
                    default: 9002,
                },
            },
        },
    },
    {
        name: 'uz_status',
        description:
            'Read real-time engine metrics: fps, frame count, uptime, encoder, ' +
            'resolution, current scene parameters (rotation speed, background, pause state). ' +
            'Returns {alive:false} if UzEngine is not running.',
        inputSchema: {
            type: 'object',
            properties: {
                port: {
                    type: 'number',
                    description: 'WebSocket port of the target engine instance. Default: 9002.',
                    default: 9002,
                },
            },
        },
    },
    {
        name: 'uz_read_file',
        description:
            'Read a file from the UzEngine project directory. ' +
            'Path is relative to the build output directory (e.g. "../../src/renderer/renderer.cpp"). ' +
            'Use this to inspect source files, shaders, or configs.',
        inputSchema: {
            type: 'object',
            properties: {
                path: {
                    type: 'string',
                    description: 'File path relative to build/Release/ directory.',
                },
            },
            required: ['path'],
        },
    },
    {
        name: 'uz_write_file',
        description:
            'Write content to a file in the UzEngine project directory. ' +
            'Path is relative to the build output directory. ' +
            'Use this to create/modify shaders, scene configs, or scripts.',
        inputSchema: {
            type: 'object',
            properties: {
                path: {
                    type: 'string',
                    description: 'File path relative to build/Release/ directory.',
                },
                content: {
                    type: 'string',
                    description: 'File content to write.',
                },
            },
            required: ['path', 'content'],
        },
    },
    {
        name: 'uz_restart',
        description:
            'Kill the running UzEngine.exe process and start a fresh one. ' +
            'Use this after rebuilding the C++ project or when the engine is stuck. ' +
            'Waits up to wait_ms for the engine to become alive again before returning.',
        inputSchema: {
            type: 'object',
            properties: {
                wait_ms: {
                    type: 'number',
                    description: 'Max time to wait for engine to come back online. Default: 8000.',
                    default: 8000,
                },
                windowed: {
                    type: 'boolean',
                    description: 'Show a visible window (debug mode). Default: false (headless).',
                    default: false,
                },
                scene: {
                    type: 'string',
                    description: 'Path to .uzscene file to load on startup (e.g. "assets/scenes/materials.uzscene").',
                    default: '',
                },
                port: {
                    type: 'number',
                    description: 'WebSocket port for this engine instance. Default: 9002.',
                    default: 9002,
                },
            },
        },
    },
    {
        name: 'uz_build',
        description:
            'Build the UzEngine C++ project and return structured results. ' +
            'Returns {success, errors[], warnings_count, build_time_ms}. ' +
            'Use this to verify code changes compile successfully. ' +
            'Supports automatic retries on build failure.',
        inputSchema: {
            type: 'object',
            properties: {
                config: {
                    type: 'string',
                    description: 'Build configuration: Release or Debug. Default: Release.',
                    default: 'Release',
                },
                max_retries: {
                    type: 'number',
                    description: 'Max retry attempts on build failure. Default: 1, Max: 3.',
                    default: 1,
                },
            },
        },
    },
];

// ---------------------------------------------------------------------------
// Handler implementations
// ---------------------------------------------------------------------------

async function handleUzCall(args) {
    const fn = args.function;
    const fnArgs = args.args ?? {};
    const timeoutMs = args.timeout_ms ?? 4000;
    const port = args.port ?? 9002;

    const result = await engineCall(fn, fnArgs, timeoutMs, port);
    return textResult(JSON.stringify(result, null, 2));
}

async function handleUzScreenshot(args) {
    const waitMs = args.wait_ms ?? 4000;
    const port = args.port ?? 9002;
    const id = randomUUID();
    const previewDir = getPreviewDir(port);
    const pngPath = path.join(previewDir, `frame_${id}.png`);
    const before = Date.now();

    // Ask engine to save the screenshot
    const callResult = await engineCall('engine.screenshot', { id }, waitMs, port);
    if (!callResult.ok) {
        return textResult(JSON.stringify({ ok: false, error: callResult.error }));
    }

    // Wait for the PNG file to appear
    const appeared = await waitForFile(pngPath, waitMs, before);
    if (!appeared) {
        return textResult(JSON.stringify({ ok: false, error: 'PNG not written within timeout' }));
    }

    // Read and base64-encode
    const pngBuffer = readFile(pngPath);
    const base64 = pngBuffer.toString('base64');

    // Return both a text summary and an inline image for Claude to see
    return {
        content: [
            {
                type: 'text',
                text: JSON.stringify({ ok: true, path: pngPath, size_bytes: pngBuffer.length }),
            },
            {
                type: 'image',
                data: base64,
                mimeType: 'image/png',
            },
        ],
    };
}

function handleUzStatus(args) {
    const port = args.port ?? 9002;
    const status = readStatus(port);
    return textResult(JSON.stringify(status, null, 2));
}

async function handleUzReadFile(args) {
    const result = await engineCall('file.read', { path: args.path });
    return textResult(JSON.stringify(result, null, 2));
}

async function handleUzWriteFile(args) {
    const result = await engineCall('file.write', { path: args.path, content: args.content });
    return textResult(JSON.stringify(result, null, 2));
}

async function handleUzRestart(args) {
    const waitMs = args.wait_ms ?? 8000;
    const windowed = args.windowed ?? false;
    const scene = args.scene ?? '';
    const port = args.port ?? 9002;
    const start = Date.now();

    killEngine();

    // Wait briefly for process to fully exit
    await new Promise(resolve => setTimeout(resolve, 800));

    const spawnResult = spawnEngine({ windowed, scene, port });
    if (!spawnResult.ok) {
        return textResult(JSON.stringify({ ok: false, error: spawnResult.error }));
    }

    // Poll until engine becomes alive or timeout
    const deadline = start + waitMs;
    while (Date.now() < deadline) {
        await new Promise(resolve => setTimeout(resolve, 500));
        const status = readStatus(port);
        if (status.alive) {
            return textResult(JSON.stringify({
                ok: true,
                pid: spawnResult.pid,
                elapsed_ms: Date.now() - start,
            }));
        }
    }

    return textResult(JSON.stringify({
        ok: false,
        error: `Engine did not come online within ${waitMs}ms`,
        pid: spawnResult.pid,
    }));
}

function handleUzBuild(args) {
    const config = args.config ?? 'Release';
    const maxRetries = Math.min(Math.max(args.max_retries ?? 1, 1), 3);
    const projectRoot = path.resolve(PREVIEW_DIR, '..', '..', '..');
    const scriptPath = path.join(projectRoot, 'tools', 'build_and_report.ps1');

    let lastResult = null;
    for (let attempt = 1; attempt <= maxRetries; attempt++) {
        try {
            const output = execSync(
                `pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "${scriptPath}" -Config "${config}"`,
                {
                    cwd: projectRoot,
                    timeout: 300_000, // 5 min build timeout
                    encoding: 'utf8',
                    stdio: ['pipe', 'pipe', 'pipe'],
                }
            );
            try {
                lastResult = JSON.parse(output.trim());
            } catch {
                lastResult = { success: true, raw_output: output.trim() };
            }
            lastResult.attempt = attempt;
            lastResult.max_retries = maxRetries;
            return textResult(JSON.stringify(lastResult, null, 2));
        } catch (err) {
            const stdout = err.stdout?.toString() ?? '';
            try {
                lastResult = JSON.parse(stdout.trim());
            } catch {
                lastResult = {
                    success: false,
                    errors: [{ severity: 'error', code: 'BUILD_EXEC_FAILED', message: err.message }],
                    warnings_count: 0,
                    build_time_ms: 0,
                };
            }
            lastResult.attempt = attempt;
            lastResult.max_retries = maxRetries;

            if (attempt < maxRetries) {
                // Will retry
                continue;
            }
        }
    }

    return textResult(JSON.stringify(lastResult, null, 2));
}

function textResult(text) {
    return { content: [{ type: 'text', text }] };
}

// ---------------------------------------------------------------------------
// MCP Server
// ---------------------------------------------------------------------------

const server = new Server(
    { name: '@uzmigames/uzengine-mcp', version: '0.1.0' },
    { capabilities: { tools: {} } }
);

server.setRequestHandler(ListToolsRequestSchema, async () => ({ tools: TOOLS }));

server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;

    try {
        switch (name) {
            case 'uz_call': return await handleUzCall(args ?? {});
            case 'uz_screenshot': return await handleUzScreenshot(args ?? {});
            case 'uz_status': return handleUzStatus(args ?? {});
            case 'uz_read_file': return await handleUzReadFile(args ?? {});
            case 'uz_write_file': return await handleUzWriteFile(args ?? {});
            case 'uz_restart': return await handleUzRestart(args ?? {});
            case 'uz_build': return handleUzBuild(args ?? {});
            default:
                return textResult(JSON.stringify({ error: `Unknown tool: ${name}` }));
        }
    } catch (err) {
        return textResult(JSON.stringify({ error: String(err) }));
    }
});

// Start
const transport = new StdioServerTransport();
await server.connect(transport);
