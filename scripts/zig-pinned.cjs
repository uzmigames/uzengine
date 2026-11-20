#!/usr/bin/env node
// Resolve the vendored Zig 0.15.2 for runtime builds so a stray `zig` on PATH
// (e.g. a WinGet 0.16.0) cannot be picked up by `npm run build:runtime`.
// Falls back to `zig` on PATH on non-Windows / CI where the vendored toolchain
// is absent. Runs from runtime/ (where build.zig lives) and forwards all args.
"use strict";
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

const repoRoot = path.resolve(__dirname, "..");
const pinned = path.join(repoRoot, ".tools", "zig-x86_64-windows-0.15.2", "zig.exe");
const zig = process.platform === "win32" && fs.existsSync(pinned) ? pinned : "zig";

const res = spawnSync(zig, process.argv.slice(2), {
  stdio: "inherit",
  cwd: path.join(repoRoot, "runtime"),
});
if (res.error) {
  console.error("[zig-pinned] failed to launch " + zig + ": " + res.error.message);
  process.exit(1);
}
process.exit(res.status === null ? 1 : res.status);
