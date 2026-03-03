# Proposal: phase4-worldgen-ai-generator

## Why

The node graph compiler (`phase4-worldgen-graph-compiler`) and the MMORPG-specific nodes
(`phase4-worldgen-mmorpg-nodes`) give world designers a powerful parametric tool — but they
still require the designer to understand which nodes to use, how to connect them, and what
parameter values produce the intended gameplay result. This is a significant skill prerequisite.
A new team member building their first MMORPG zone must read the full node catalogue, experiment
with combinations, and iterate manually before producing anything usable. Even experienced
designers face the "blank canvas" problem: starting a new zone from scratch is cognitively
expensive when the configuration space is dozens of nodes and hundreds of parameters.

The `world_generate` MCP tool solves this by shifting the authoring medium from node configuration
to natural language. A designer types "A 4km mystical highland zone for a magic MMORPG: 3 flat
combat plateaus at different heights, a central mountain with ARK-style cliffs, pine forests in
the lowlands, ancient stone ruins on the plateaus, 4 vertical levels with walkable ramps" and
receives a complete, valid `.uzworldgen` graph within 60 seconds. The designer does not need to
know that this description maps to `TerracedLevels(num_levels:4)` + `Plateau(targets:[0.25,0.5,
0.75])` + `CliffContour(slope_threshold:0.65)` + three `BiomeDefinition` nodes. The AI knows
the node catalogue and the MMORPG constraints and makes those decisions. The designer keeps
creative control: after generation, they can open the graph in the node editor and adjust any
parameter, or use `world_refine` with natural language feedback to drive further iterations
without manually touching the graph JSON.

The screenshot feedback loop distinguishes this from a naive "prompt → JSON → done" pipeline.
World generation has visual quality criteria — "are the plateaus actually flat?", "do the cliffs
follow the mountain shape naturally?", "is there clearly visible access between the levels?" —
that cannot be verified from JSON alone. By incorporating a screenshot of the compiled terrain
into the evaluation loop, the AI can detect whether its generated graph actually achieved the
intended result, identify which nodes need parameter adjustment, and recompile. This is the same
evaluation loop that a human designer uses (compile → look at result → adjust → recompile) but
compressed to 15–20 seconds per iteration and driven automatically. The maximum 3 iteration
limit prevents infinite loops while allowing the most common cases to self-correct.

## What Changes

- **`world_generate` MCP tool** (`mcp-server/src/tools/worldGenTools.ts`) — TypeScript MCP
  tool handler; accepts `description`, `world_size_km`, `style`, `num_plateaus`, `num_levels`,
  `biomes[]`, `seed`, `resolution`, `template` parameters; constructs a two-part Claude API
  prompt: system prompt contains the full node catalogue (auto-generated from
  `worldgen.listNodes()` IPC call, always current), MMORPG constraint rules, and the closest
  template graph JSON as a starting reference; user prompt is the caller's description plus
  extracted structured parameters; sends to Claude API and receives `.uzworldgen` JSON;
  validates the generated JSON against `WorldGenValidator`; if validation fails, appends error
  feedback and retries (max 2 retries); writes validated graph to
  `res://worldgen/{sanitized_name}.uzworldgen`

- **`world_refine` MCP tool** (`mcp-server/src/tools/worldGenTools.ts`) — accepts
  `graph_path`, `feedback`, `screenshot_base64`, `max_iterations`; reads existing
  `.uzworldgen` graph; constructs Claude API prompt with: current graph JSON, screenshot,
  feedback text, and a parameter mutation reference table mapping common feedback phrases to
  specific node parameter deltas (spec § 5.3); Claude returns a diff of parameter changes
  (not a full graph rewrite — targeted mutations only, expressed as JSON Patch RFC 6902
  operations); applies patches to graph; triggers recompile; evaluates screenshot; repeats
  up to `max_iterations` (default 3); returns final graph path + change summary string

- **Screenshot evaluation loop** (`mcp-server/src/tools/worldGenEvaluator.ts`) — after each
  WorldGenCompiler pass, calls `engine.screenshot` IPC and sends screenshot to Claude with a
  structured evaluation checklist: (1) plateau flatness score 0–10, (2) ramp visibility
  yes/no, (3) cliff contour naturalness score 0–10, (4) biome distribution match percentage,
  (5) overall MMORPG playability rating 0–10; Claude returns structured JSON evaluation; if
  any criterion scores below its threshold, the evaluator selects corrective parameter
  mutations and triggers another compile cycle; the checklist and thresholds are configurable
  per `WorldGenStyle` (mmorpg style has stricter plateau flatness requirement than survival)

- **Template matching** (`mcp-server/src/tools/worldGenTemplates.ts`) — `getClosestTemplate`
  function that scores each of the 6 built-in templates against the request parameters using
  a weighted cosine similarity: style match (weight 0.4), biome overlap (weight 0.3), num_levels
  similarity (weight 0.2), world_size proximity (weight 0.1); the highest-scoring template
  graph JSON is included in the system prompt as a "starting reference"; this dramatically
  reduces Claude's generation search space for common MMORPG zone types

- **6 MMORPG template files** (`res://worldgen/templates/`) — pre-built and validated
  `.uzworldgen` graphs for: `mmorpg_overworld` (3-tier elevation, mixed biomes, full MMORPG
  feature set), `mmorpg_island` (VoronoiIslands base, beach/jungle/highland, ruins),
  `mmorpg_dungeon_surface` (crater terrain, dark volcanic biome, dungeon entrance spawn),
  `mmorpg_city_district` (flat urban base, 2-tier city walls, road grid), `survival_wilderness`
  (extreme variation, heavy erosion, 5 biomes, no plateaus), `floating_islands` (layered
  Voronoi at multiple heights, bridge spawn markers); each template is independently tested
  and compiles successfully at 2048×2048

- **`world_list_templates` MCP tool** (`mcp-server/src/tools/worldGenTools.ts`) — returns
  all available templates with metadata: name, description, style, preview thumbnail path,
  biomes[], typical world_size_km, node_count; used by the WorldGen Panel AI Generate modal
  to show template previews to the user

- **Prompt engineering assets** (`mcp-server/src/tools/prompts/worldgen/`) — modular system
  prompt fragments that are composed per request: `nodesCatalogue.md` (auto-generated from
  current node list), `mmorpgConstraints.md` (walkability rules, splatmap limits, etc.),
  `styleGuide_mmorpg.md`, `styleGuide_survival.md`, `styleGuide_island.md`, etc.; fragments
  are reloaded from disk on each MCP server start (hot-reloadable as the node catalogue grows)

- **Progress streaming** — `world_generate` sends SSE-style progress events back to the
  caller: `{ step: "graph_generation", percent: 20 }`, `{ step: "compiling_preview",
  percent: 50 }`, `{ step: "evaluating", percent: 70 }`, `{ step: "refining", percent: 85,
  iteration: 2 }`, `{ step: "complete", percent: 100 }`; the WorldGen Panel displays these
  as a progress bar with step labels

- **Deterministic seed handling** — when `seed` is provided, the tool appends it to the
  system prompt and validates that the generated graph uses the same seed value; this ensures
  reproducible generation (same description + same seed = same terrain) for asset review
  pipelines and regression testing

- **Integration tests** (`mcp-server/src/test/worldGenTools.test.ts`) — mock Claude API
  (returns canned `.uzworldgen` JSON), mock WorldGenCompiler (returns canned heightmap PNG),
  mock engine.screenshot IPC; tests cover: `world_generate` produces valid graph JSON,
  validation failure triggers retry, screenshot evaluation triggers parameter adjustment
  iteration, `world_refine` applies JSON Patch correctly, template matching returns correct
  template for MMORPG style with 3 plateaus, `world_list_templates` returns all 6 templates

## Impact

- **Affected specs**: `docs/specs/engine/procedural-world-gen.md` (this task implements §§ 5
  and 7 fully — AI tools and template library), `docs/specs/engine/ai-native-features.md`
  (`world_generate` and `world_refine` added to the MCP tool registry alongside `scene_generate`,
  `shader_assist`, `script_generate`, `vfx_generate`)
- **Affected code**: `mcp-server/src/tools/worldGenTools.ts` (new MCP tool handlers),
  `mcp-server/src/tools/worldGenEvaluator.ts` (new screenshot evaluator),
  `mcp-server/src/tools/worldGenTemplates.ts` (new template matcher),
  `mcp-server/src/tools/prompts/worldgen/` (new prompt fragments),
  `res://worldgen/templates/` (6 new template `.uzworldgen` files),
  `mcp-server/src/test/worldGenTools.test.ts` (new test suite)
- **Breaking change**: NO — `world_generate` and `world_refine` are new MCP tools that do not
  modify any existing tool signatures; the template files are new assets; the MCP server gains
  new routes but existing routes are unchanged
- **User benefit**: Any designer — regardless of familiarity with the WorldGen node system —
  can describe a MMORPG zone in plain English and receive a playable terrain in under 60
  seconds; experienced designers gain an AI co-pilot that handles the repetitive work of
  translating design intent into node parameters while they focus on creative decisions; the
  screenshot feedback loop provides a quality gate that catches common generation mistakes
  before the designer even sees the result
