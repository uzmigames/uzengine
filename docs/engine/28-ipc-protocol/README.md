# IPC Protocol

N-API bridge and H.264 frame streaming between the engine runtime and the VSCode extension.

> **Status**: Partial (POC 0.2 validated)
> **Source**: `runtime/napi/`, `extension/src/`

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | IPC architecture overview — extension-to-runtime communication |
| [02-napi-bridge.md](02-napi-bridge.md) | Node-API .node addon — JS/C++ boundary, threading model |
| [03-frame-streaming.md](03-frame-streaming.md) | DX12 readback → libjpeg-turbo/H.264 encode → WebSocket → Canvas |
