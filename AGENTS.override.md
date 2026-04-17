# Project-Specific Overrides
# Languages: C++23 (engine), HLSL (shaders), TypeScript (VSCode extension), Lua (scripting)

## ABSOLUTE RULES (highest precedence)

- **NEVER REVERT** — no git revert/reset/checkout--/restore. Fix forward. Use `--cvar r.Feature=0`.
- **FAIL TWICE → OPEN TEAM** — agents must READ CODE, produce root cause before any fix.
- **NEVER STOP TO ASK** — implement continuously. Never "quer que eu continue?".
- **OBEY USER EXACTLY** — no reinterpretation, no "better" alternatives.
- **RUNTIME IS 100% PATH-AGNOSTIC** — NEVER hardcode filesystem paths in `runtime/` code (no absolute paths, no drive letters, no project-specific relative paths like `"sample/content/..."` or `"content/meshes/foo.uzmesh"`). All paths enter via CLI args (`--project`, `--cvar`), `.uzproj` files, asset manifests, or explicit API calls from the application layer. Use `uz::GetExeDir()` for exe-relative resolution. Demos/samples/tests outside `runtime/` MAY have paths (they are consumers). Before committing `runtime/` changes, grep for `":/"`, `".uz"`, `"content/"`, `"sample/"`, drive letters.

## RENDERING RULES

- **Falcor = ground truth** (`E:/Falcor`). PSNR >= 35 dB = pass. Adjust UzEngine, never Falcor.
- **Full parity** — ALL parameters, ALL defaults from published papers. See `.claude/rules/ue5-component-parity.md`.
- **Research first** — SIGGRAPH/GDC paper before ANY shader code. Never trust model knowledge for rendering math.
- **Shader citation**: `/// @ref Author Year "Title"` + `@constants` for every numeric constant.
- **Incremental** — solid color → one feature → verify → next. Stuck 2-3x? Delete and restart.
- **No stubs/TODOs/placeholders** — implement completely or explain why impossible.
- **Agents opt-in** — implement directly unless user requests delegation.
- **Diagnostics BEFORE code** — `--pass-dump` / `--diag` / `--pixel-debug` / `--vis` before editing shaders.

## BUILD

```bash
cd runtime && zig build                    # Runtime
cd runtime && zig build -Ddll=true         # DLL (VSCode)
cd demos/<name> && zig build               # Demo (rebuild runtime first)
```

## AGENT TABLE

| Task | Agent | Model |
|------|-------|-------|
| Shaders | shader-engineer | opus |
| C++ | cpp-core-expert | opus |
| RHI | rhi-engineer | opus |
| Render graph | render-graph-engineer | opus |
| ECS | ecs-systems-engineer | opus |
| Integration (3+ files) | systems-integration | opus |
| Build system | build-system-engineer | sonnet |
| TypeScript/VSCode | typescript-vscode-extension | sonnet |
| Lua | lua-scripting-engineer | sonnet |
| Tests | test-engineer | sonnet |
| Docs | docs-operator | haiku |

## MCP DOCS SERVER (`uzengine-docs`)

`search_docs(query)` | `get_entity(id)` | `get_data_flow(field)` | `get_relationships(id)` | `get_refs(author?)`

## LOGS

| Runtime | `%LOCALAPPDATA%/UzEngine/logs/uzengine_*.log` |
| Crashes | `{engine_dir}/crashes/*/crash.{dmp,txt,json}` |
