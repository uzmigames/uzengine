# Engine Core Specs

| Spec | Description |
|------|-------------|
| [memory-system.md](memory-system.md) | Arena allocators (frame/level/permanent), ownership model, Handle<T>, unique_ptr for GPU |
| [debug-profiling.md](debug-profiling.md) | Assert system, Tracy CPU/GPU profiler, ImGui overlay, doctest unit tests, ASAN |
| [crash-reporting.md](crash-reporting.md) | SEH unhandled exception capture, minidump, crash log JSON, system info, Lua state, IPC integration |
| [build-system.md](build-system.md) | Cook → Pack → Compile → Stage → Package pipeline, NSIS installer, cross-platform abstraction |
| [project-modules.md](project-modules.md) | Independent Git repos per team, access via GitHub permissions, module sync, per-module `.uzpak` partitioning |
| [plugin-system.md](plugin-system.md) | `.uzplugin` manifest, stable C ABI plugin_api_t, SEH isolation, VSCode extension side, hot-reload |
| [ai-native-features.md](ai-native-features.md) | MCP tools: scene_generate, shader_assist, script_generate, scene_analyze, ai.context |
