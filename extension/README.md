# @uzmigames/extension

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![TypeScript](https://img.shields.io/badge/TypeScript-5.3-blue.svg)]()
[![VSCode](https://img.shields.io/badge/VSCode-1.85+-purple.svg)]()

UzEngine VSCode extension — the editor interface. Loads the native `.node` addon and provides the full game engine editor experience inside VSCode.

**This module is pure TypeScript.** All native code lives in [`runtime/`](../runtime/) and [`napi/`](../napi/).

## Build

```bash
# Install dependencies
npm install

# Build TypeScript
npm run build
```

The `.node` binary must be copied to `native/` before building. The root `npm run build` handles this automatically.

## Run

Press **F5** in VSCode (launch config in `.vscode/launch.json`) to start the Extension Development Host.

## Commands

| Command | Description |
|---------|------------|
| `UzEngine: Start` | Initialize the engine runtime |

## Directory Structure

```
extension/
├── package.json       # @uzmigames/extension — VSCode extension manifest
├── tsconfig.json
├── native/            # .node binary (copied by build pipeline, gitignored)
│   └── uzengine.node
└── src/
    └── extension.ts   # Extension entry point
```

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
