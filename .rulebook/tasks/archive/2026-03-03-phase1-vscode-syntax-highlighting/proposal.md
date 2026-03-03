# Proposal: phase1-vscode-syntax-highlighting

## Why

The `vscode-uzengine` extension currently only streams the DX12 renderer output. Developers working on UzEngine projects open `.uzscene`, `.uzmat`, `.hlsl`, `.lua`, and other files daily, but VSCode shows them as plain text — no color, no structure. This makes reading and editing these files error-prone and slow.

Adding syntax highlighting for all UzEngine file formats transforms the extension from a renderer viewer into a proper development environment. Every file type used in a UzEngine project will be visually structured, making the editor feel native to the engine.

## What Changes

- **`vscode-uzengine/syntaxes/`** — new folder with 13 TextMate grammar files:
  - `uz-ini-base.tmLanguage.json` — shared base for all INI-like `.uz*` formats
  - `uzscene.tmLanguage.json` — `.uzscene` + node type keywords
  - `uzmat.tmLanguage.json` — `.uzmat` + blend/alpha mode enums
  - `uzfx.tmLanguage.json` — `.uzfx` + emitter type enums
  - `uzterrain.tmLanguage.json` — `.uzterrain` + terrain section keywords
  - `uzbt.tmLanguage.json` — `.uzbt` + behavior tree node types
  - `uzcam.tmLanguage.json` — `.uzcam` + camera mode enums
  - `uzproj.tmLanguage.json` — `.uzproj` TOML grammar
  - `uzplugin.tmLanguage.json` — `.uzplugin` TOML grammar
  - `hlsl.tmLanguage.json` — HLSL grammar (MIT, from microsoft/vscode-hlsl)
  - `lua.tmLanguage.json` — Lua grammar (MIT, from sumneko/lua-language-server)
  - `markdown-uzscene.tmLanguage.json` — injection for `uzscene/uzmat/uzfx/uzbt/uzcam` code fences in Markdown
  - `markdown-hlsl.tmLanguage.json` — injection for `hlsl` code fences in Markdown

- **`vscode-uzengine/language-configuration/`** — new folder with 3 language configuration files:
  - `uz-ini.language-configuration.json` — comments (`;`), brackets, auto-close for all `.uz*` INI formats
  - `hlsl.language-configuration.json` — comments (`//`, `/* */`), brackets for HLSL
  - `lua.language-configuration.json` — comments (`--`, `--[[]]`), brackets for Lua

- **`vscode-uzengine/package.json`** — add `languages` and `grammars` contributions for 10 language IDs

## Impact

- Affected specs: `docs/specs/vscode-syntax-highlighting.md` (new)
- Affected code: `vscode-uzengine/package.json`, new `syntaxes/` and `language-configuration/` folders
- Breaking change: NO — purely additive, no existing functionality changed
- User benefit: All UzEngine file types are visually structured in VSCode. Sections, node types, shader semantics, Lua keywords, and resource paths are color-coded. Markdown docs with code examples are also highlighted correctly.
