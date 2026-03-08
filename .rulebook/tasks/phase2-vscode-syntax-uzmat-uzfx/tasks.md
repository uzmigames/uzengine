## Context

Extends `phase1-vscode-syntax-highlighting` (uzscene only).
Adds grammars for Phase 2 file formats: uzmat, uzfx, uzbt, uzcam, uzterrain, uzproj, uzplugin.
Must be implemented AFTER the corresponding file formats are defined.

## Prerequisites

- [x] P1. `phase2-lua-scripting` complete (uzbt/behavior tree format finalized)
- [x] P2. Material format `.uzmat` specified in `docs/specs/assets/materials.md`
- [x] P3. Particle format `.uzfx` specified
- [x] P4. Camera format `.uzcam` specified

## 1. Shared INI Base Grammar

- [x] 1.1 Create `vscode-uzengine/syntaxes/uz-ini-base.tmLanguage.json` — shared rules for all `.uz*` INI formats: section headers `[name]`, `key = value`, comments `#`/`;`, string values, numbers
- [x] 1.2 Create `vscode-uzengine/language-configuration/uz-ini.language-configuration.json` — lineComment `#`/`;`, brackets `[`/`]`, autoClosingPairs, wordPattern

## 2. `.uzmat` Grammar (Material Format)

- [x] 2.1 Create `vscode-uzengine/syntaxes/uzmat.tmLanguage.json` — extends uz-ini-base, adds blend mode keywords (Opaque/Masked/Translucent), alpha mode enums, texture path highlighting
- [x] 2.2 Register in `package.json` — `contributes.languages` + `contributes.grammars` for `.uzmat`

## 3. `.uzfx` Grammar (Particle Format)

- [x] 3.1 Create `vscode-uzengine/syntaxes/uzfx.tmLanguage.json` — emitter type enums, particle property keywords
- [x] 3.2 Register in `package.json` for `.uzfx`

## 4. `.uzbt` Grammar (Behavior Tree)

- [x] 4.1 Create `vscode-uzengine/syntaxes/uzbt.tmLanguage.json` — node type keywords (Sequence, Selector, Condition, Action), indent-based structure
- [x] 4.2 Register in `package.json` for `.uzbt`

## 5. `.uzcam` Grammar (Camera Format)

- [x] 5.1 Create `vscode-uzengine/syntaxes/uzcam.tmLanguage.json` — camera mode enums (Perspective/Ortho/FirstPerson), FOV/clip keywords
- [x] 5.2 Register in `package.json` for `.uzcam`

## 6. `.uzterrain` Grammar

- [x] 6.1 Create `vscode-uzengine/syntaxes/uzterrain.tmLanguage.json` — terrain section keywords, heightmap/splat map path highlighting
- [x] 6.2 Register in `package.json` for `.uzterrain`

## 7. `.uzproj` / `.uzplugin` Grammar (TOML-based)

- [x] 7.1 Create `vscode-uzengine/syntaxes/uzproj.tmLanguage.json` — TOML grammar subset for project config keys
- [x] 7.2 Create `vscode-uzengine/syntaxes/uzplugin.tmLanguage.json` — plugin manifest TOML keys
- [x] 7.3 Register both in `package.json`

## 8. Markdown Injection Grammars

- [x] 8.1 Create `markdown-uzscene.tmLanguage.json` — injection for uzscene/uzmat/uzfx/uzbt/uzcam code fences
- [x] 8.2 Register injection grammar in `package.json`

## 9. Tests

- [x] 9.1 Add tests for each new grammar: file existence, JSON validity, scopeName, required rules
- [x] 9.2 Add tests for each new language in `package.json` contributions
- [x] 9.3 All VSCode extension tests pass (≥95% coverage)
