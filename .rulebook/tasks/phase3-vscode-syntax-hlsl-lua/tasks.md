## Context

Extends `phase1-vscode-syntax-highlighting` and `phase2-vscode-syntax-uzmat-uzfx`.
Adds HLSL and Lua grammar support. Must be implemented after Phase 3 shader work
and Phase 2 Lua scripting are complete.

## Prerequisites

- [ ] P1. `phase3-shader-library` complete (HLSL authoring established)
- [ ] P2. `phase2-lua-scripting` complete (Lua VM integrated)

## 1. HLSL Grammar

- [ ] 1.1 Create `vscode-uzengine/syntaxes/hlsl.tmLanguage.json` — HLSL 2021 keywords, types (float4, matrix, etc.), semantics (POSITION, TEXCOORD, SV_Target), intrinsics (saturate, lerp, dot, etc.), preprocessor directives
- [ ] 1.2 Create `vscode-uzengine/language-configuration/hlsl.language-configuration.json` — lineComment `//`, blockComment `/* */`, brackets, indentation rules
- [ ] 1.3 Register in `package.json` for `.hlsl`, `.hlsli`, `.fx` extensions
- [ ] 1.4 Consider adopting existing MIT-licensed HLSL grammar (microsoft/vscode-hlsl) with attribution

## 2. Lua Grammar

- [ ] 2.1 Create `vscode-uzengine/syntaxes/lua.tmLanguage.json` — Lua 5.4 / LuaJIT keywords, string patterns, `--` comment, `--[[` block comment, UzEngine API globals (ctx, scene, etc.)
- [ ] 2.2 Create `vscode-uzengine/language-configuration/lua.language-configuration.json` — lineComment `--`, blockComment `--[[`/`]]`, brackets, autoClosingPairs
- [ ] 2.3 Register in `package.json` for `.lua` extension (scoped to UzEngine workspace to avoid conflict with existing Lua extensions)
- [ ] 2.4 Consider adopting existing MIT-licensed Lua grammar (sumneko/lua-language-server) with attribution

## 3. Markdown Injection for HLSL

- [ ] 3.1 Create `vscode-uzengine/syntaxes/markdown-hlsl.tmLanguage.json` — injection for `hlsl` code fences in Markdown files
- [ ] 3.2 Register injection grammar in `package.json`

## 4. Tests

- [ ] 4.1 HLSL grammar: file existence, JSON validity, scopeName, keyword rules, semantic rules
- [ ] 4.2 Lua grammar: file existence, JSON validity, scopeName, keyword rules, comment rules
- [ ] 4.3 Markdown injections: injection scopeName format correct
- [ ] 4.4 All VSCode extension tests pass (≥95% coverage)
