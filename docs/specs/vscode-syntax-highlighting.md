# Spec: VSCode Syntax Highlighting

**Status**: Planned
**Phase**: 1
**Task**: `phase1-vscode-syntax-highlighting`
**Depends on**: `phase1-lua-scripting`, `phase2-shader-library`, `phase2-behavior-trees`

---

## Overview

The `vscode-uzengine` extension currently only provides JPEG stream rendering of the DX12 renderer. It has no language support for any UzEngine file formats. This spec defines syntax highlighting for all UzEngine proprietary file formats and the embedded languages used for shaders and scripting.

The goal is to make every file a developer opens in VSCode when working on a UzEngine project visually readable — sections, keys, values, types, resource paths, and embedded code should all be color-coded.

---

## File Formats to Support

### INI-like Formats (Custom Grammar)

All UzEngine data files use a shared INI-like format with sections, key-value pairs, comments, and typed values. Each file extension gets its own grammar that inherits from a shared base and adds format-specific keywords.

| Extension   | Description            | Specific Keywords |
|-------------|------------------------|-------------------|
| `.uzscene`  | Scene graph definition | Node types, component names |
| `.uzmat`    | Material definition    | Blend/alpha mode enums, shader ref |
| `.uzfx`     | Particle effects       | Emitter types, blend mode enums |
| `.uzterrain`| Height-map terrain     | `[material.layerN]`, `[foliage.*]`, `[water]` |
| `.uzbt`     | Behavior tree          | BT node types (Sequence, Selector, Leaf) |
| `.uzcam`    | Camera configuration   | Camera modes, DOF/shake params |

### TOML Formats (Custom Grammar)

| Extension   | Description          | Specific Keywords |
|-------------|----------------------|-------------------|
| `.uzproj`   | Project configuration | `[project]`, `[plugins]`, `[locale]`, `[renderer]` |
| `.uzplugin` | Plugin manifest       | `[plugin]`, `[cpp]`, `[permissions]` |

### Embedded Languages (Reuse Public Grammars)

| Extension      | Language | Source |
|----------------|----------|--------|
| `.hlsl`        | HLSL     | microsoft/vscode-hlsl (MIT) |
| `.hlsli`       | HLSL     | Same grammar as `.hlsl` |
| `.fx`          | HLSL     | Same grammar as `.hlsl` |
| `.lua`         | Lua 5.1  | sumneko/lua-language-server syntaxes (MIT) |

### Markdown Code Fence Injection

Code blocks in `.md` files using UzEngine language identifiers get syntax highlighting via TextMate injection grammars:

| Fence tag      | Highlighting applied |
|----------------|----------------------|
| ` ```uzscene ` | `.uzscene` grammar   |
| ` ```uzmat `   | `.uzmat` grammar     |
| ` ```uzfx `    | `.uzfx` grammar      |
| ` ```uzterrain`| `.uzterrain` grammar |
| ` ```uzproj `  | `.uzproj` grammar    |
| ` ```hlsl `    | HLSL grammar         |
| ` ```lua `     | Lua grammar (native VSCode) |

---

## Grammar Design

### Base INI Grammar (`uz-ini-base.tmLanguage.json`)

Shared grammar for all INI-like `.uz*` formats. Captures:

**Comments**
```
; This is a comment
```
Scope: `comment.line.semicolon.uz`

**Section Headers**
```
[uzscene version=1 name="Level01"]
[node id=2 type=StaticMesh name="Ground" parent=1]
```
- Section keyword (e.g. `uzscene`, `node`): `keyword.control.section.uz`
- Attribute names (`id=`, `type=`, `name=`, `parent=`): `entity.other.attribute-name.uz`
- Attribute string values (`"Ground"`): `string.quoted.double.uz`
- Attribute numeric values (`1`, `2`): `constant.numeric.uz`

**Key-Value Properties**
```
mesh             = "res://assets/terrain.glb"
cast_shadow      = true
position         = [10.0, 0.0, -5.0]
layer_mask       = 0xFF
```
- Property key: `variable.other.property.uz`
- `=` operator: `keyword.operator.assignment.uz`
- Boolean values (`true`, `false`): `constant.language.boolean.uz`
- Numeric values (int, float): `constant.numeric.uz`
- Hex values (`0xFF`): `constant.numeric.hex.uz`
- Array brackets `[...]`: `punctuation.definition.array.uz`
- Array commas: `punctuation.separator.uz`

**String Values**
```
mesh = "res://assets/terrain.glb"
```
- Full string: `string.quoted.double.uz`
- `res://` prefix inside strings: `markup.underline.link.uz` (acts as link highlight)
- Path after `res://`: `string.quoted.double.uz`

**Enum Values**
```
blend_mode  = OPAQUE
alpha_mode  = ALPHA_BLEND
emitter_type = cone
```
Scope: `support.constant.enum.uz`

---

### Per-Format Keywords

#### `.uzscene` — Node Types

Highlighted as `storage.type.node.uzscene` when appearing as `type=<NodeType>` attribute values:

```
Node3D, StaticMesh, SkeletalMesh, Camera3D, DirectionalLight, PointLight, SpotLight,
RigidBody, StaticBody, CharacterBody, TriggerVolume, CSGBox, CSGSphere, CSGCylinder,
AudioSource, AudioListener, NavMeshAgent, UIRoot, ParticleEmitter, Terrain, WaterBody
```

Also highlighted as property key keywords:
```
mesh, material, position, rotation, scale, script, cast_shadow, receive_shadow,
visible, collision_layer, collision_mask, linear_velocity, angular_velocity
```

#### `.uzmat` — Material Enums

Enum values for material-specific properties (`support.constant.enum.uzmat`):
```
OPAQUE, ALPHA_BLEND, ALPHA_CLIP, ADDITIVE, PREMULTIPLIED, TWO_SIDED
```

The `[variants]` section header is highlighted as `keyword.other.variants.uzmat`.

#### `.uzfx` — Particle Enums

Emitter type enums (`support.constant.enum.uzfx`):
```
cone, sphere, box, point, hemisphere, edge
```

Blend mode enums:
```
additive, alpha, premultiplied, opaque
```

Fields with min/max suffix pairs highlighted as `variable.other.minmax.uzfx`:
```
lifetime_min, lifetime_max, start_size_min, start_size_max, start_speed_min, start_speed_max
```

#### `.uzterrain` — Terrain Sections

Subsection patterns highlighted as `keyword.other.section.uzterrain`:
```
[material.layer0], [material.layer1], [foliage.grass_clumps], [water]
```

#### `.uzbt` — Behavior Tree Node Types

BT node type values (`storage.type.node.uzbt`):
```
Sequence, Selector, Parallel, Inverter, Succeeder, Repeater,
Condition, Action, Wait, BlackboardCheck, BlackboardSet
```

#### `.uzcam` — Camera Modes

Camera mode enum values (`support.constant.enum.uzcam`):
```
perspective, orthographic, cinematic, first_person, third_person,
spring_arm, free_look, top_down, isometric
```

---

### TOML Grammar (`.uzproj`, `.uzplugin`)

Captures standard TOML syntax plus UzEngine-specific section keyword highlighting.

**Standard TOML**
- Section headers `[table]` / `[table.subtable]`: `entity.name.section.toml`
- Keys: `variable.other.key.toml`
- String values: `string.quoted.double.toml`
- Integer/float: `constant.numeric.toml`
- Boolean: `constant.language.boolean.toml`
- Array brackets: `punctuation.definition.array.toml`
- Inline table `{...}`: `punctuation.definition.table.toml`
- Comments `#`: `comment.line.number-sign.toml`

**UzEngine-Specific Section Keywords** (`keyword.control.section.uz`):

`.uzproj` sections: `project`, `plugins`, `plugin_paths`, `locale`, `scripting`, `renderer`, `input`, `build`, `editor`

`.uzplugin` sections: `plugin`, `vscode`, `cpp`, `permissions`

---

### HLSL Grammar (`.hlsl`, `.hlsli`, `.fx`)

Embedded grammar sourced from `microsoft/vscode-hlsl` (MIT license). Registered under language ID `hlsl`.

Key token categories:
- **Keywords**: `cbuffer`, `tbuffer`, `Texture2D`, `RWTexture2D`, `StructuredBuffer`, `RWStructuredBuffer`, `SamplerState`, `SamplerComparisonState`, `AppendStructuredBuffer`, `ConsumeStructuredBuffer`
- **Types**: `float`, `float2`, `float3`, `float4`, `float4x4`, `int`, `int2`, `int3`, `int4`, `uint`, `uint2`, `uint3`, `uint4`, `bool`, `half`, `double`, `min16float`, `min16int`, `min16uint`
- **Qualifiers**: `in`, `out`, `inout`, `uniform`, `static`, `const`, `inline`, `extern`, `volatile`, `precise`, `row_major`, `column_major`, `groupshared`, `nointerpolation`, `linear`, `centroid`, `noperspective`, `sample`
- **Semantics** (after `:`): `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD0`–`TEXCOORD7`, `COLOR0`–`COLOR3`, `SV_POSITION`, `SV_TARGET`, `SV_TARGET0`–`SV_TARGET7`, `SV_DEPTH`, `SV_InstanceID`, `SV_VertexID`, `SV_GroupIndex`, `SV_DispatchThreadID`, `SV_GroupID`, `SV_GroupThreadID`
- **Register Bindings**: `register(b0)`, `register(t0)`, `register(s0)`, `register(u0)`, `register(space0)`
- **Built-in Functions**: `mul`, `dot`, `cross`, `normalize`, `saturate`, `lerp`, `clamp`, `abs`, `sign`, `floor`, `ceil`, `frac`, `step`, `smoothstep`, `reflect`, `refract`, `length`, `distance`, `pow`, `sqrt`, `rsqrt`, `exp`, `exp2`, `log`, `log2`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `transpose`, `determinant`, `ddx`, `ddy`, `ddx_fine`, `ddy_fine`
- **Preprocessor**: `#include`, `#define`, `#ifndef`, `#ifdef`, `#if`, `#else`, `#elif`, `#endif`, `#pragma`

---

### Lua Grammar (`.lua`)

Embedded grammar sourced from `sumneko/lua-language-server` syntaxes (MIT license). Registered under language ID `lua`.

**Standard Lua 5.1 / LuaJIT 2.1**
- Keywords: `function`, `local`, `if`, `then`, `else`, `elseif`, `end`, `return`, `for`, `while`, `do`, `repeat`, `until`, `break`, `not`, `and`, `or`, `in`
- Constants: `nil`, `true`, `false`
- Standard libs: `math`, `string`, `table`, `io`, `os`, `coroutine`, `package`, `debug`
- Comments: `--`, `--[[ ... ]]`
- Strings: `"..."`, `'...'`, `[[...]]`, `[=[...]=]`
- Numbers: integers, floats (`1.0`, `.5`, `1e3`), hex (`0xFF`)

**UzEngine Engine API keywords** (`support.class.uzengine.lua`):
```
Vec3, Vec2, Quat, Color, AABB, Ray, Mat4, ctx, self
```

**UzEngine API method patterns** (`support.function.uzengine.lua`):
```
ctx.scene, ctx.input, ctx.world, ctx.audio, ctx.ui, ctx.saves,
ctx.locale, ctx.console, ctx:log, ctx:schedule_after, ctx:schedule_every,
ctx:cancel_timer, ctx:emit
```

---

### Markdown Injection Grammars

Two injection grammars are registered to handle UzEngine code fences in Markdown files:

1. **`markdown-uzscene.tmLanguage.json`** — handles ` ```uzscene `, ` ```uzmat `, ` ```uzfx `, ` ```uzterrain `, ` ```uzbt `, ` ```uzcam `
2. **`markdown-hlsl.tmLanguage.json`** — handles ` ```hlsl `, ` ```hlsli `

Both inject into `text.html.markdown` scope and use embedded language declarations so that VSCode applies the correct grammar inside the fenced block.

---

## Language Configuration

### `uz-ini.language-configuration.json`

Applies to all INI-like `.uz*` formats:

```json
{
  "comments": {
    "lineComment": ";"
  },
  "brackets": [
    ["[", "]"],
    ["{", "}"]
  ],
  "autoClosingPairs": [
    { "open": "[", "close": "]" },
    { "open": "\"", "close": "\"" }
  ],
  "surroundingPairs": [
    ["[", "]"],
    ["\"", "\""]
  ]
}
```

### `hlsl.language-configuration.json`

```json
{
  "comments": {
    "lineComment": "//",
    "blockComment": ["/*", "*/"]
  },
  "brackets": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"]
  ],
  "autoClosingPairs": [
    { "open": "{", "close": "}" },
    { "open": "[", "close": "]" },
    { "open": "(", "close": ")" },
    { "open": "\"", "close": "\"" }
  ],
  "surroundingPairs": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"],
    ["\"", "\""]
  ]
}
```

### `lua.language-configuration.json`

```json
{
  "comments": {
    "lineComment": "--",
    "blockComment": ["--[[", "]]"]
  },
  "brackets": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"]
  ],
  "autoClosingPairs": [
    { "open": "{", "close": "}" },
    { "open": "[", "close": "]" },
    { "open": "(", "close": ")" },
    { "open": "\"", "close": "\"" },
    { "open": "'", "close": "'" }
  ],
  "surroundingPairs": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"],
    ["\"", "\""],
    ["'", "'"]
  ]
}
```

---

## Extension Registration (`package.json` additions)

### Languages Contributed

```json
"languages": [
  { "id": "uzscene",   "aliases": ["UzEngine Scene"],         "extensions": [".uzscene"],  "configuration": "./language-configuration/uz-ini.language-configuration.json" },
  { "id": "uzmat",     "aliases": ["UzEngine Material"],      "extensions": [".uzmat"],    "configuration": "./language-configuration/uz-ini.language-configuration.json" },
  { "id": "uzfx",      "aliases": ["UzEngine FX"],            "extensions": [".uzfx"],     "configuration": "./language-configuration/uz-ini.language-configuration.json" },
  { "id": "uzterrain", "aliases": ["UzEngine Terrain"],       "extensions": [".uzterrain"],"configuration": "./language-configuration/uz-ini.language-configuration.json" },
  { "id": "uzbt",      "aliases": ["UzEngine BehaviorTree"],  "extensions": [".uzbt"],     "configuration": "./language-configuration/uz-ini.language-configuration.json" },
  { "id": "uzcam",     "aliases": ["UzEngine Camera"],        "extensions": [".uzcam"],    "configuration": "./language-configuration/uz-ini.language-configuration.json" },
  { "id": "uzproj",    "aliases": ["UzEngine Project"],       "extensions": [".uzproj"],   "configuration": "./language-configuration/uz-ini.language-configuration.json" },
  { "id": "uzplugin",  "aliases": ["UzEngine Plugin"],        "extensions": [".uzplugin"], "configuration": "./language-configuration/uz-ini.language-configuration.json" },
  { "id": "hlsl",      "aliases": ["HLSL", "High Level Shader Language"], "extensions": [".hlsl", ".hlsli", ".fx"], "configuration": "./language-configuration/hlsl.language-configuration.json" },
  { "id": "lua",       "aliases": ["Lua"],                    "extensions": [".lua"],      "configuration": "./language-configuration/lua.language-configuration.json" }
]
```

### Grammars Contributed

```json
"grammars": [
  { "language": "uzscene",   "scopeName": "source.uzscene",   "path": "./syntaxes/uzscene.tmLanguage.json" },
  { "language": "uzmat",     "scopeName": "source.uzmat",     "path": "./syntaxes/uzmat.tmLanguage.json" },
  { "language": "uzfx",      "scopeName": "source.uzfx",      "path": "./syntaxes/uzfx.tmLanguage.json" },
  { "language": "uzterrain", "scopeName": "source.uzterrain", "path": "./syntaxes/uzterrain.tmLanguage.json" },
  { "language": "uzbt",      "scopeName": "source.uzbt",      "path": "./syntaxes/uzbt.tmLanguage.json" },
  { "language": "uzcam",     "scopeName": "source.uzcam",     "path": "./syntaxes/uzcam.tmLanguage.json" },
  { "language": "uzproj",    "scopeName": "source.uzproj",    "path": "./syntaxes/uzproj.tmLanguage.json" },
  { "language": "uzplugin",  "scopeName": "source.uzplugin",  "path": "./syntaxes/uzplugin.tmLanguage.json" },
  { "language": "hlsl",      "scopeName": "source.hlsl",      "path": "./syntaxes/hlsl.tmLanguage.json" },
  { "language": "lua",       "scopeName": "source.lua",       "path": "./syntaxes/lua.tmLanguage.json" },
  {
    "scopeName": "markdown.uzscene.codeblock",
    "path": "./syntaxes/markdown-uzscene.tmLanguage.json",
    "injectTo": ["text.html.markdown"],
    "embeddedLanguages": {
      "meta.embedded.block.uzscene": "uzscene",
      "meta.embedded.block.uzmat": "uzmat",
      "meta.embedded.block.uzfx": "uzfx",
      "meta.embedded.block.uzterrain": "uzterrain",
      "meta.embedded.block.uzbt": "uzbt",
      "meta.embedded.block.uzcam": "uzcam"
    }
  },
  {
    "scopeName": "markdown.hlsl.codeblock",
    "path": "./syntaxes/markdown-hlsl.tmLanguage.json",
    "injectTo": ["text.html.markdown"],
    "embeddedLanguages": {
      "meta.embedded.block.hlsl": "hlsl"
    }
  }
]
```

---

## File Structure

```
vscode-uzengine/
├── syntaxes/
│   ├── uz-ini-base.tmLanguage.json        — Shared INI base grammar
│   ├── uzscene.tmLanguage.json            — Scene grammar (extends base)
│   ├── uzmat.tmLanguage.json              — Material grammar (extends base)
│   ├── uzfx.tmLanguage.json               — FX grammar (extends base)
│   ├── uzterrain.tmLanguage.json          — Terrain grammar (extends base)
│   ├── uzbt.tmLanguage.json               — Behavior tree grammar (extends base)
│   ├── uzcam.tmLanguage.json              — Camera grammar (extends base)
│   ├── uzproj.tmLanguage.json             — Project TOML grammar
│   ├── uzplugin.tmLanguage.json           — Plugin TOML grammar
│   ├── hlsl.tmLanguage.json               — HLSL grammar (from microsoft/vscode-hlsl)
│   ├── lua.tmLanguage.json                — Lua grammar (from sumneko/lua-language-server)
│   ├── markdown-uzscene.tmLanguage.json   — Markdown injection for uz* formats
│   └── markdown-hlsl.tmLanguage.json      — Markdown injection for HLSL
├── language-configuration/
│   ├── uz-ini.language-configuration.json
│   ├── hlsl.language-configuration.json
│   └── lua.language-configuration.json
└── package.json                            — Updated with languages + grammars
```

---

## Grammar Sources (MIT License)

- **HLSL**: https://github.com/microsoft/vscode-hlsl — `syntaxes/hlsl.tmLanguage.json`
- **Lua**: https://github.com/sumneko/lua-language-server — `syntaxes/Lua.tmLanguage.json`
- **TOML base reference**: https://github.com/tamasfe/taplo — `editors/vscode/syntaxes/TOML.tmLanguage.json`

All three are MIT-licensed and can be included directly in the extension. Attribution must be preserved in the file headers.

---

## Definition of Done

- [ ] All 10 language IDs registered and associated with file extensions
- [ ] All grammar files created and free of JSON parse errors
- [ ] Language configuration files created for brackets, comments, auto-close
- [ ] Opening a `.uzscene` file shows colored sections, node types, resource paths
- [ ] Opening a `.uzmat` file shows colored blend_mode enums
- [ ] Opening a `.hlsl` file shows colored keywords, semantics, register bindings
- [ ] Opening a `.lua` file shows colored Lua keywords + UzEngine API identifiers
- [ ] A `.md` file with ` ```uzscene ` shows highlighted content inside the fence
- [ ] `npm run compile` succeeds with zero TypeScript errors
- [ ] No conflicts with other installed VSCode extensions (Lua, HLSL, etc.)
