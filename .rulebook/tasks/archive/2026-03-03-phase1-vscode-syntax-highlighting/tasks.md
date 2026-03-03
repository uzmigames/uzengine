## 1. `.uzscene` Grammar

- [x] 1.1 Create `vscode-uzengine/syntaxes/uzscene.tmLanguage.json` — section headers, keys, `=` separator, string values, numbers, NodeType keywords (Empty, Node3D, StaticMesh, DirectionalLight, PointLight, SpotLight, Camera, AudioSource, AudioListener), comments (`#`/`;`)
- [x] 1.2 Create `vscode-uzengine/syntaxes/uzscene-language-config.json` — lineComment `#`, brackets `[`/`]`, autoClosingPairs, wordPattern

## 2. Extension Registration

- [x] 2.1 Update `vscode-uzengine/package.json` — add `contributes.languages` for `uzscene` with `.uzscene` extension
- [x] 2.2 Update `vscode-uzengine/package.json` — add `contributes.grammars` with scopeName `source.uzscene`

## 3. Tests (`vscode-uzengine/src/__tests__/syntax-highlighting.test.ts`)

- [x] 3.1 41 tests: file existence, JSON validity, package.json contributions, grammar structure, language config structure
- [x] 3.2 All 50 VSCode extension tests pass (41 new + 9 existing)

## Deferred to Phase 2+ (from original proposal — file formats not yet implemented)

- [ ] D1. `uz-ini-base.tmLanguage.json` — shared INI base grammar (defer: no uzmat/uzfx yet)
- [ ] D2. `uzmat.tmLanguage.json` — `.uzmat` material format (defer: format not implemented)
- [ ] D3. `uzfx.tmLanguage.json` — `.uzfx` particle format (defer: format not implemented)
- [ ] D4. `uzterrain.tmLanguage.json` — `.uzterrain` format (defer: terrain not in Phase 1)
- [ ] D5. `uzbt.tmLanguage.json` — `.uzbt` behavior tree (defer: BT not in Phase 1)
- [ ] D6. `uzcam.tmLanguage.json` — `.uzcam` camera format (defer: camera format not in Phase 1)
- [ ] D7. `uzproj.tmLanguage.json` — `.uzproj` TOML grammar (defer: project format not finalized)
- [ ] D8. `uzplugin.tmLanguage.json` — `.uzplugin` TOML grammar (defer: plugin system not in Phase 1)
- [ ] D9. `hlsl.tmLanguage.json` — HLSL grammar (defer: Phase 3 shader work)
- [ ] D10. `lua.tmLanguage.json` — Lua grammar (defer: Phase 2 scripting)
- [ ] D11. `markdown-uzscene.tmLanguage.json` — Markdown injection (defer: after all formats done)
- [ ] D12. `markdown-hlsl.tmLanguage.json` — Markdown HLSL injection (defer: after HLSL grammar)
- [ ] D13. `language-configuration/` folder — uz-ini, hlsl, lua configs (defer: Phase 2+)
