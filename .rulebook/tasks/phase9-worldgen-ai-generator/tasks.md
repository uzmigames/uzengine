# Tasks: phase9-worldgen-ai-generator

## 1. Prompt Engineering Assets

- [ ] 1.1 Create directory `mcp-server/src/tools/prompts/worldgen/` for modular system prompt fragments
- [ ] 1.2 Implement `nodesCatalogue.md` auto-generation: build-time script that reads current node list from `worldgen.listNodes()` IPC output and writes formatted markdown fragment
- [ ] 1.3 Author `mmorpgConstraints.md`: walkability rules, splatmap layer limits (≤4), ramp slope max (≤35°), overhang probability limit (≤0.3), SpawnpointOutput requirement
- [ ] 1.4 Author `styleGuide_mmorpg.md`, `styleGuide_survival.md`, `styleGuide_island.md` style guide fragments
- [ ] 1.5 Implement hot-reload of prompt fragments: reload all `.md` files in the prompts directory on MCP server start

## 2. Template Library

- [ ] 2.1 Author and validate 6 MMORPG template `.uzworldgen` files in `res://worldgen/templates/`: `mmorpg_overworld`, `mmorpg_island`, `mmorpg_dungeon_surface`, `mmorpg_city_district`, `survival_wilderness`, `floating_islands`
- [ ] 2.2 Verify all 6 templates compile successfully at 2048×2048 resolution with zero validation errors
- [ ] 2.3 Include template metadata in each file: `name`, `description`, `style`, `preview_thumbnail_path`, `biomes[]`, `typical_world_size_km`, `node_count`

## 3. Template Matching

- [ ] 3.1 Create `mcp-server/src/tools/worldGenTemplates.ts` implementing `getClosestTemplate(request) -> TemplateGraph`
- [ ] 3.2 Implement weighted cosine similarity scoring: style match (weight 0.4), biome overlap (weight 0.3), `num_levels` similarity (weight 0.2), world_size proximity (weight 0.1)
- [ ] 3.3 Return the highest-scoring template's full JSON graph for injection into the Claude API system prompt

## 4. `world_generate` MCP Tool

- [ ] 4.1 Create `mcp-server/src/tools/worldGenTools.ts` implementing `world_generate` handler
- [ ] 4.2 Accept parameters: `description`, `world_size_km`, `style`, `num_plateaus`, `num_levels`, `biomes[]`, `seed`, `resolution`, `template`
- [ ] 4.3 Build two-part Claude API prompt: system prompt = node catalogue + MMORPG constraints + closest template JSON; user prompt = description + extracted parameters
- [ ] 4.4 Send to Claude API; receive `.uzworldgen` JSON; run `WorldGenValidator`; on failure inject error + retry (max 2 retries)
- [ ] 4.5 Write validated graph to `res://worldgen/{sanitized_name}.uzworldgen`
- [ ] 4.6 Emit SSE-style progress events: `{step: "graph_generation", percent: 20}`, `{step: "compiling_preview", percent: 50}`, `{step: "evaluating", percent: 70}`, `{step: "refining", percent: 85, iteration: N}`, `{step: "complete", percent: 100}`
- [ ] 4.7 When `seed` is provided, validate that the generated graph uses the same seed value for reproducible generation
- [ ] 4.8 Register `world_generate` in the MCP server tool registry

## 5. `world_refine` MCP Tool

- [ ] 5.1 Implement `world_refine` handler in `mcp-server/src/tools/worldGenTools.ts`
- [ ] 5.2 Accept parameters: `graph_path`, `feedback`, `screenshot_base64`, `max_iterations` (default 3)
- [ ] 5.3 Read existing `.uzworldgen` graph; build Claude API prompt with current graph JSON, screenshot, feedback text, and parameter mutation reference table (spec § 5.3)
- [ ] 5.4 Claude returns JSON Patch (RFC 6902) operations; apply patches to graph; validate; trigger recompile; evaluate screenshot; repeat up to `max_iterations`
- [ ] 5.5 Return final graph path + change summary string listing changed node/parameter pairs
- [ ] 5.6 Register `world_refine` in the MCP server tool registry

## 6. Screenshot Evaluation Loop

- [ ] 6.1 Create `mcp-server/src/tools/worldGenEvaluator.ts` implementing `evaluateScreenshot(screenshot_base64, style) -> EvaluationResult`
- [ ] 6.2 After each `WorldGenCompiler` pass, call `engine.screenshot` IPC and send screenshot to Claude with structured evaluation checklist: (1) plateau flatness score 0–10, (2) ramp visibility yes/no, (3) cliff contour naturalness score 0–10, (4) biome distribution match percentage, (5) overall MMORPG playability rating 0–10
- [ ] 6.3 Claude returns structured JSON evaluation; if any criterion scores below threshold, select corrective parameter mutations and trigger another compile cycle
- [ ] 6.4 Make thresholds configurable per `WorldGenStyle`: MMORPG style requires plateau flatness ≥ 7 (stricter than survival)

## 7. `world_list_templates` MCP Tool

- [ ] 7.1 Implement `world_list_templates` handler in `mcp-server/src/tools/worldGenTools.ts`: return all 6 templates with metadata
- [ ] 7.2 Include per-template: `name`, `description`, `style`, `preview_thumbnail_path`, `biomes[]`, `typical_world_size_km`, `node_count`
- [ ] 7.3 Register `world_list_templates` in the MCP server tool registry

## 8. Testing (≥95% line + branch coverage)

- [ ] 8.1 Write `mcp-server/src/test/worldGenTools.test.ts`: mock Claude API (returns canned `.uzworldgen` JSON), mock `WorldGenCompiler` (returns canned heightmap PNG), mock `engine.screenshot` IPC; assert `world_generate` produces valid graph JSON
- [ ] 8.2 Write validation failure retry test: mock Claude API to return invalid JSON first call, valid second; assert error injected into conversation and retry succeeds
- [ ] 8.3 Write `world_refine` test: mock Claude API returning RFC 6902 JSON Patch; assert patches applied correctly and graph recompiled
- [ ] 8.4 Write template matching test: `getClosestTemplate` with MMORPG style + 3 plateaus; assert returns `mmorpg_overworld` template
- [ ] 8.5 Write `world_list_templates` test: assert all 6 templates returned with required metadata fields
- [ ] 8.6 Write screenshot evaluation test: mock Claude evaluator returning plateau flatness score 4 (below threshold); assert another compile iteration is triggered
- [ ] 8.7 Verify ≥95% line and branch coverage across all `mcp-server/src/tools/worldGenTools.ts`, `worldGenEvaluator.ts`, and `worldGenTemplates.ts` using Jest --coverage
