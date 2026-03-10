# Changelog — extension

## [0.2.0] — 2026-03-10

### Added
- VSCode extension skeleton with activation entry point
- Native addon loading (`uzengine.node` from `native/`)
- ESLint 9 flat config with typescript-eslint 8 (errors only, no warnings)
- Prettier 3.8 config
- Vitest 4 config with 95% coverage threshold
- LogOutputChannel logging (replaces console.log)
- Syntax highlighting for HLSL (`.hlsl`, `.hlsli`, `.fx`, `.fxh`)
- Syntax highlighting for Zig (`.zig`)
- Language registration for UzEngine formats (`.uzscene`, `.uzmaterial`, `.uzmesh`, `.uzprefab`, `.uzproject`)
- Markdown fenced code block injection for `hlsl` and `zig`
