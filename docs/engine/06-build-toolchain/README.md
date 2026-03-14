# Build Toolchain

Zig build system, third-party dependencies, CLI tools, and N-API bridge.

> **Status**: Active (Zig build done; N-API integration in progress)
> **Source**: `build.zig`, `tools/`, `runtime/napi/`

| Document | Topic |
|----------|-------|
| [01-zig-build-system.md](01-zig-build-system.md) | Zig build system design, cross-compilation, platform guards |
| [02-thirdparty-catalog.md](02-thirdparty-catalog.md) | All third-party libraries — versions, licenses, integration method |
| [03-tools.md](03-tools.md) | CLI tools — uzcook (asset cooker), uzembed (shader embed) |
| [04-napi-bridge.md](04-napi-bridge.md) | Node-API .node addon — VSCode extension to native bridge |
| [05-build-configs.md](05-build-configs.md) | Debug, RelWithDebInfo, Release build configurations |
