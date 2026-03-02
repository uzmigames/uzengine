## 1. Grammar Base — Shared INI Foundation

- [ ] 1.1 Create `vscode-uzengine/syntaxes/uz-ini-base.tmLanguage.json` — section headers `[key attr=val]`, property keys, `=` operator, string values, boolean constants, numeric values, hex values, array literals `[x, y, z]`, resource paths `res://...`, comments `;`
- [ ] 1.2 Create `vscode-uzengine/language-configuration/uz-ini.language-configuration.json` — lineComment `;`, brackets `[`/`]`, autoClosingPairs for `"` and `[`

## 2. INI-like Format Grammars (per extension)

- [ ] 2.1 Create `vscode-uzengine/syntaxes/uzscene.tmLanguage.json` — extends base, adds node type keywords: `Node3D`, `StaticMesh`, `SkeletalMesh`, `Camera3D`, `DirectionalLight`, `PointLight`, `SpotLight`, `RigidBody`, `StaticBody`, `CharacterBody`, `TriggerVolume`, `CSGBox`, `AudioSource`, `NavMeshAgent`, `UIRoot`, `ParticleEmitter`, `Terrain`, `WaterBody`
- [ ] 2.2 Create `vscode-uzengine/syntaxes/uzmat.tmLanguage.json` — extends base, adds material enum values: `OPAQUE`, `ALPHA_BLEND`, `ALPHA_CLIP`, `ADDITIVE`, `PREMULTIPLIED`, `TWO_SIDED`, highlights `[variants]` section keyword
- [ ] 2.3 Create `vscode-uzengine/syntaxes/uzfx.tmLanguage.json` — extends base, adds emitter type enums: `cone`, `sphere`, `box`, `point`, `hemisphere`, `edge`; blend mode enums: `additive`, `alpha`, `premultiplied`; highlights `*_min`/`*_max` property suffix pairs
- [ ] 2.4 Create `vscode-uzengine/syntaxes/uzterrain.tmLanguage.json` — extends base, adds terrain section keywords: `[material.layer*]`, `[foliage.*]`, `[water]`
- [ ] 2.5 Create `vscode-uzengine/syntaxes/uzbt.tmLanguage.json` — extends base, adds BT node type keywords: `Sequence`, `Selector`, `Parallel`, `Inverter`, `Succeeder`, `Repeater`, `Condition`, `Action`, `Wait`, `BlackboardCheck`, `BlackboardSet`
- [ ] 2.6 Create `vscode-uzengine/syntaxes/uzcam.tmLanguage.json` — extends base, adds camera mode enums: `perspective`, `orthographic`, `cinematic`, `first_person`, `third_person`, `spring_arm`, `free_look`, `top_down`, `isometric`

## 3. TOML Format Grammars

- [ ] 3.1 Create `vscode-uzengine/syntaxes/uzproj.tmLanguage.json` — full TOML grammar (sections, keys, strings, numbers, booleans, arrays, inline tables, comments `#`); adds UzEngine section keyword highlighting for `project`, `plugins`, `plugin_paths`, `locale`, `scripting`, `renderer`, `input`, `build`, `editor`
- [ ] 3.2 Create `vscode-uzengine/syntaxes/uzplugin.tmLanguage.json` — same TOML base, adds UzEngine section keywords for `plugin`, `vscode`, `cpp`, `permissions`

## 4. Embedded Languages (Public Grammars — MIT)

- [ ] 4.1 Download and include `vscode-uzengine/syntaxes/hlsl.tmLanguage.json` from `microsoft/vscode-hlsl` — HLSL keywords, types, semantics (`:SV_POSITION`, `:TEXCOORD*`), register bindings (`register(b0)`), built-in functions, preprocessor directives; preserve MIT attribution in file header
- [ ] 4.2 Create `vscode-uzengine/language-configuration/hlsl.language-configuration.json` — lineComment `//`, blockComment `/* */`, brackets `{}/[]/()`, autoClosingPairs
- [ ] 4.3 Download and include `vscode-uzengine/syntaxes/lua.tmLanguage.json` from `sumneko/lua-language-server` — Lua 5.1 keywords, constants, standard libs, comments `--`/`--[[]]`, string types, numbers; add UzEngine API identifiers `Vec3`, `Quat`, `ctx`, `self` as `support.class.uzengine.lua`
- [ ] 4.4 Create `vscode-uzengine/language-configuration/lua.language-configuration.json` — lineComment `--`, blockComment `--[[`/`]]`, brackets `{}/[]/()`, autoClosingPairs for `"`, `'`

## 5. Markdown Injection Grammars

- [ ] 5.1 Create `vscode-uzengine/syntaxes/markdown-uzscene.tmLanguage.json` — injection grammar that injects into `text.html.markdown`, matches code fences with identifiers `uzscene`, `uzmat`, `uzfx`, `uzterrain`, `uzbt`, `uzcam`, assigns correct embedded scopes
- [ ] 5.2 Create `vscode-uzengine/syntaxes/markdown-hlsl.tmLanguage.json` — injection grammar matching code fences with identifier `hlsl`, assigns `meta.embedded.block.hlsl`

## 6. Extension Registration

- [ ] 6.1 Update `vscode-uzengine/package.json` — add `contributes.languages` array with 10 language definitions (uzscene, uzmat, uzfx, uzterrain, uzbt, uzcam, uzproj, uzplugin, hlsl, lua) each with correct extension associations and language-configuration paths
- [ ] 6.2 Update `vscode-uzengine/package.json` — add `contributes.grammars` array with 12 grammar registrations (10 languages + 2 markdown injection grammars) each with scopeName, path, and embeddedLanguages where applicable

## 7. Testing

- [ ] 7.1 Create `vscode-uzengine/test-fixtures/` folder with sample files for each format: `sample.uzscene`, `sample.uzmat`, `sample.uzfx`, `sample.uzterrain`, `sample.uzbt`, `sample.uzcam`, `sample.uzproj`, `sample.uzplugin`, `sample.hlsl`, `sample.lua`, `sample.md`
- [ ] 7.2 Open each fixture file in Extension Development Host (F5) and verify: sections highlighted, keywords colored, strings and numbers tokenized, comments grayed out
- [ ] 7.3 Verify `.md` fixture file shows highlighted code inside ` ```uzscene ` and ` ```hlsl ` fences
- [ ] 7.4 Run `npm run compile` — zero TypeScript errors
- [ ] 7.5 Verify no conflicts with other extensions (test with Lua extension installed, HLSL Tools installed)

## 8. Documentation

- [x] 8.1 `docs/specs/vscode-syntax-highlighting.md` — complete spec (done during planning)
- [ ] 8.2 Update `docs/README.md` — add `vscode-syntax-highlighting.md` to the specs table
- [ ] 8.3 Update `vscode-uzengine/README.md` — add language support section listing all supported file types
