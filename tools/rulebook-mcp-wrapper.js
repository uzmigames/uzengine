#!/usr/bin/env node
// Wrapper for rulebook MCP server that filters non-JSON stdout.
// The rulebook server prints debug logs like [BackgroundIndexer] to stdout,
// which corrupts the JSON-RPC protocol. This wrapper only forwards valid
// JSON-RPC messages to the parent process.

const { spawn } = require('child_process');
const path = require('path');

const cwd = 'E:\\UzEngine';

const child = spawn('npx', ['-y', '@hivehub/rulebook@latest', 'mcp-server'], {
    cwd,
    stdio: ['pipe', 'pipe', 'pipe'],
    shell: true,
    env: { ...process.env, NODE_NO_WARNINGS: '1' }
});

// Forward stdin from parent to child
process.stdin.pipe(child.stdin);

// Filter child stdout — only forward lines starting with { (JSON-RPC)
let buffer = '';
child.stdout.on('data', (chunk) => {
    buffer += chunk.toString();
    const lines = buffer.split('\n');
    buffer = lines.pop(); // keep incomplete line in buffer

    for (const line of lines) {
        const trimmed = line.trim();
        if (trimmed.startsWith('{') || trimmed.startsWith('Content-Length:') || trimmed === '') {
            process.stdout.write(line + '\n');
        }
        // else: drop non-JSON log lines like [BackgroundIndexer], [rulebook-mcp], etc.
    }
});

// Forward stderr as-is (logs go here, invisible to MCP protocol)
child.stderr.pipe(process.stderr);

child.on('exit', (code) => {
    process.exit(code || 0);
});

process.on('SIGTERM', () => child.kill('SIGTERM'));
process.on('SIGINT', () => child.kill('SIGINT'));
