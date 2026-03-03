# Proposal: phase4-vfx-ai-generator

## Why

One of the highest-friction moments in VFX authoring is the blank-canvas problem: a
technical artist or generalist developer needs a new effect, but translating a mental image
("a swirling electric portal with crackling blue sparks") into correct module parameters,
gradient stops, and noise frequencies requires deep familiarity with the particle system.
The `vfx_generate` MCP tool eliminates this friction entirely. A single natural-language
prompt produces a complete, playable `.uzfx` v2 graph, all required HLSL custom modules, and
high-quality sprite textures — all wired together and ready to hot-reload into the engine
within 15 seconds. This is not a "good enough for prototyping" feature; the generation
pipeline is designed for production quality because every generated asset can be refined
iteratively through `vfx_refine`, and every generated texture is produced by Nano Banana at
the same resolution and quality as hand-crafted assets.

The screenshot feedback loop is the critical differentiator from a simple "generate JSON"
tool. After the first generation, the artist can look at the live result in the engine
viewport, type "the smoke needs to rise faster and the sparks should spiral outward more
tightly", and the `vfx_refine` tool will load the current graph, send it alongside the
screenshot and the feedback to Claude, receive a targeted patch, apply it, and hot-reload —
all without the artist touching a parameter field. This closes the gap between "AI writes
the first draft" and "AI helps iterate to the final result", making it a genuine authoring
accelerator rather than a curiosity.

The Nano Banana integration is equally important: a technically perfect module graph with a
placeholder grey circle sprite looks unfinished. By automatically generating contextually
appropriate sprites (a smoky wisp, an electric spark sheet, a golden glow disc) during
`vfx_generate`, the first result the artist sees is visually complete and production-plausible,
not a grey placeholder that requires a separate texture-authoring pass.

## What Changes

- **`vfx_generate` MCP tool** (`mcp-server/src/tools/vfx_generate.ts`): implements the
  5-step pipeline (Claude API graph generation → HLSL module extraction → Nano Banana sprite
  generation → IPC hot-reload → screenshot confirmation); accepts `description`, `style`,
  `color_palette`, `reference_image`, `max_emitters`, `budget_particles` parameters.
- **`vfx_refine` MCP tool** (`mcp-server/src/tools/vfx_refine.ts`): loads existing `.uzfx`,
  builds a refinement prompt with current graph JSON + optional screenshot + feedback
  description, sends to Claude, merges the returned patch, validates, writes back, and
  hot-reloads; maintains a `VfxSession` context object for up to 10 minutes per effect.
- **`vfx_list_modules` MCP tool** (`mcp-server/src/tools/vfx_list_modules.ts`): returns
  the complete `ModuleSchema[]` array used as context injection in Claude API calls; sourced
  from a `module_registry.json` file that is auto-generated during engine build by scanning
  the `src/vfx/modules/` HLSL includes for doc-comment headers.
- **Claude API integration**: typed TypeScript client wrapping the Anthropic SDK; uses
  structured output (JSON mode) to guarantee schema-valid `.uzfx` graphs; system prompt
  includes the full `.uzfx` v2 JSON schema, the `ParticleState` HLSL interface, all module
  schemas, and style/color hints; max 2 validation-error retries before returning an error.
- **`VfxSchemaValidator`** (`mcp-server/src/vfx/VfxSchemaValidator.ts`): AJV-based JSON
  Schema validator that checks Claude's output before writing to disk; produces
  human-readable error messages that are re-injected into the Claude conversation on retry.
- **Nano Banana client** (`mcp-server/src/external/NanaBananaClient.ts`): typed wrapper
  around the Nano Banana REST API; `generate_sprite`, `generate_flipbook`,
  `generate_normal_map` methods; handles rate limiting (exponential backoff), 30-second
  timeout, and stores results as PNG; triggers `AssetPipeline` import via IPC for mip +
  BC7 processing.
- **`ai://generate/` URL scheme**: the `VFXGraphLoader` and `VfxSchemaValidator` recognize
  `"ai://generate/{url_encoded_prompt}"` as a texture URL pattern; the MCP server resolves
  these URLs to real `res://` paths during the generation pipeline before writing the final
  `.uzfx` to disk, so the engine never sees an `ai://` URL.
- **`VfxSession` context store** (`mcp-server/src/vfx/VfxSession.ts`): keyed by effect
  path; stores original description, Claude's internal reasoning (from the `thinking` field
  in the API response), and generation timestamp; TTL of 10 minutes; used by `vfx_refine` to
  inject prior reasoning as context for coherent incremental refinement.
- **`_ai_metadata` root key**: the generated `.uzfx` JSON includes a `"_ai_metadata"` object
  with `original_description`, `style`, `color_palette`, `generated_at`, `model_version`,
  and `nano_banana_prompts` for reproducibility and Asset Browser indexing; this key is
  ignored by the engine parser.
- **`res://textures/vfx/ai_generated/` storage layout**: `{effect_name}/{slot}_color.png`,
  `{slot}_normal.png`, `{slot}_flipbook.png`, `metadata.json`; directory created by the MCP
  server if absent; existing files overwritten on regeneration.
- **Integration tests**: `vfx_generate.test.ts` mocks the Claude API and Nano Banana API,
  validates that the full pipeline produces a schema-valid `.uzfx` with correct file
  references and that IPC `vfx.hotReload` is called; `vfx_refine.test.ts` validates patch
  merge and session context injection; all tests run in < 5 seconds with mocks.

## Impact

- **Affected specs**: `docs/specs/rendering/vfx-system-ai.md` (sections 3, 8, 9), `docs/specs/ai-native-features.md` (new `vfx_generate`, `vfx_refine`, `vfx_list_modules` MCP tools), `docs/specs/rendering/particle-system.md` (section 8 evolution note)
- **Affected code**: `mcp-server/src/tools/` (3 new files), `mcp-server/src/external/NanaBananaClient.ts` (new), `mcp-server/src/vfx/` (new directory: `VfxSchemaValidator.ts`, `VfxSession.ts`), `res://textures/vfx/ai_generated/` (created at runtime)
- **Breaking change**: NO — existing MCP tools (`scene_generate`, `shader_assist`, `script_generate`) are unaffected; `vfx_generate` is a new addition to the MCP tool registry
- **User benefit**: Complete VFX effects (graph + HLSL + sprites) generated from a single English sentence in < 15 seconds; iterative AI refinement without manual parameter editing; production-quality Nano Banana sprites automatically matched to effect style
