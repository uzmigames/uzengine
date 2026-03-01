## 1. AI Configuration (`project.uzproj`)

- [ ] 1.1 Parse `[ai]` block: `provider`, `api_key`, `code_model`, `analysis_model`, `ollama_endpoint`, `context_screenshots`
- [ ] 1.2 `UZ_AI_API_KEY` environment variable overrides `api_key` from file
- [ ] 1.3 Git safety: warn if non-empty `api_key` is found in a git-tracked `project.uzproj`
- [ ] 1.4 `LLMClient` class: abstracts Anthropic / OpenAI / Ollama API; `complete(system, user)` → string
- [ ] 1.5 `provider = "none"` mode: return formatted prompt without calling any API (offline/manual use)

## 2. C++ IPC Handler: `ai.context`

- [ ] 2.1 `AiContextHandler::handle(args)` — builds comprehensive JSON response
- [ ] 2.2 Include: `project.name`, `project.version`, `project.current_scene`
- [ ] 2.3 Include: full `scene.getTree()` output under `scene_tree`
- [ ] 2.4 Include: `assets` object — list all `.glb`, `.png`, `.uzmat`, `.lua`, `.uzfx` files under `res://`
- [ ] 2.5 Include: `node_types` — list of all registered node type names
- [ ] 2.6 Include: `engine_state` — `playing`, `fps`, `selected_node_id`
- [ ] 2.7 Include: `format_specs.uzscene` — condensed format spec string (first 2000 chars of scene-system spec)
- [ ] 2.8 Optional: if `context_screenshots = true` in `[ai]`, include `engine.screenshot` output as base64

## 3. C++ IPC Handler: `editor.analyzeScene`

- [ ] 3.1 `SceneAnalyzer::analyze(scene)` — walks scene tree, builds analysis JSON
- [ ] 3.2 Count nodes by type; list all lights with properties
- [ ] 3.3 List all cameras with `fov`, `is_main` flag
- [ ] 3.4 List all mesh nodes with mesh path and triangle count (from ResourceManager)
- [ ] 3.5 List all script attachments (node_id + script path)
- [ ] 3.6 Performance warnings: nodes with > 20k triangles + no LOD; nodes sharing materials (instancing opportunity)
- [ ] 3.7 Asset summary: total mesh/texture/script/effect counts

## 4. MCP Tool: `scene_generate`

- [ ] 4.1 Tool definition in `mcp-server/tools/scene_generate.ts`: `description`, `base_scene?`, `output_path?`
- [ ] 4.2 Call `ai.context` IPC to get project + asset context
- [ ] 4.3 Build LLM system prompt: uzscene format spec + available assets + output rules (only output INI block)
- [ ] 4.4 Call `LLMClient.complete(system, "Generate .uzscene for: " + description)`
- [ ] 4.5 `extract_code_block(response, 'ini')` — strip markdown fencing
- [ ] 4.6 `validate_uzscene_syntax(text)` — check `[uzscene]` header + at least one `[node]` section
- [ ] 4.7 Write scene file to `output_path`
- [ ] 4.8 IPC: `scene.load { path: output_path }`
- [ ] 4.9 IPC: `engine.screenshot {}` → base64
- [ ] 4.10 Return `{ scene_path, screenshot, node_count }`

## 5. MCP Tool: `shader_assist`

- [ ] 5.1 Tool definition: `description`, `target`, `existing_code?`
- [ ] 5.2 If `existing_code` omitted: read target file from disk (or empty if file doesn't exist yet)
- [ ] 5.3 Build LLM system prompt: HLSL conventions, UzEngine shader system spec, output rules (only output HLSL code)
- [ ] 5.4 Call `LLMClient.complete` with existing code in context
- [ ] 5.5 `extract_code_block(response, 'hlsl')` — strip fencing
- [ ] 5.6 Write HLSL to `target` path
- [ ] 5.7 IPC: `shader.reload { path: target }` — triggers DXC compile + hot-reload
- [ ] 5.8 Wait 800ms for hot-reload to complete
- [ ] 5.9 Read `shader.reload` response for DXC errors; if errors → return `{ shader_path, errors[] }` without screenshot
- [ ] 5.10 IPC: `engine.screenshot {}` → base64
- [ ] 5.11 Return `{ shader_path, screenshot, errors: [] }`

## 6. MCP Tool: `ai_set_property`

- [ ] 6.1 Tool definition: `description`, `node_id?`, `apply?` (default true)
- [ ] 6.2 Call `ai.context` IPC (or use cached context from last call < 5s ago)
- [ ] 6.3 If `node_id` omitted: use `engine_state.selected_node_id` from context
- [ ] 6.4 Build LLM system prompt: scene context, node properties, IPC `scene.setProperty` format
- [ ] 6.5 LLM response format: JSON array of `{ method, args }` objects
- [ ] 6.6 Parse JSON array; validate each entry is a known IPC method
- [ ] 6.7 If `apply = true`: execute each IPC call in sequence
- [ ] 6.8 Return `{ applied_calls: [...], screenshot }` (screenshot taken after last call)

## 7. MCP Tool: `scene_analyze`

- [ ] 7.1 Tool definition: `topic?` (default "all"), `suggest?` (default true)
- [ ] 7.2 Call `editor.analyzeScene {}` IPC → raw analysis JSON
- [ ] 7.3 If `suggest = false`: return raw analysis JSON only
- [ ] 7.4 If `suggest = true`: build LLM prompt with analysis JSON + topic focus
- [ ] 7.5 LLM returns JSON array of suggestions: `{ priority, category, description, ipc_actions[] }`
- [ ] 7.6 Return `{ analysis, suggestions }`

## 8. MCP Tool: `script_generate`

- [ ] 8.1 Tool definition: `node_id`, `description`, `output_path?`, `attach?` (default true)
- [ ] 8.2 Call `ai.context` to get scene tree and node properties for `node_id`
- [ ] 8.3 Find nearby nodes (children, siblings, parent) for additional context
- [ ] 8.4 Build LLM system prompt: Lua scripting spec, node type/components, nearby node names, available assets
- [ ] 8.5 Output rules: only output Lua code block
- [ ] 8.6 `extract_code_block(response, 'lua')`
- [ ] 8.7 Lua syntax validation: spawn `lua -e "loadfile(path)"` or use embedded LuaJIT `luaL_loadstring`
- [ ] 8.8 Derive `output_path` from node name if omitted: `res://scripts/<node_name_snake_case>.lua`
- [ ] 8.9 Write Lua file to `output_path`
- [ ] 8.10 If `attach = true`: IPC `scene.setProperty { id: node_id, key: "script", value: output_path }`
- [ ] 8.11 Return `{ script_path, preview: <first 20 lines>, attached }`

## 9. MCP Tool: `ai_suggest`

- [ ] 9.1 `ai.suggest { topic }` IPC handler — returns topic-scoped JSON context subset
- [ ] 9.2 MCP tool: `ai_suggest` — calls `ai.suggest`, passes to LLM, returns `{ topic, suggestions[] }`
- [ ] 9.3 Topics: `performance`, `lighting`, `composition`, `scripts`

## 10. VSCode Commands

- [ ] 10.1 `uzengine.generateScene` command — opens input box, calls `scene_generate` MCP tool
- [ ] 10.2 `uzengine.aiSetProperty` command — opens input box, uses selected node, calls `ai_set_property`
- [ ] 10.3 `uzengine.generateScript` command — uses selected Scene Tree node, opens input box, calls `script_generate`
- [ ] 10.4 `uzengine.analyzeScene` command — opens Webview panel showing analysis + suggestions
- [ ] 10.5 `uzengine.shaderAssist` command — uses active editor file (must be `.hlsl`), opens input box
- [ ] 10.6 Commands registered in `contributes.commands` in `package.json`
- [ ] 10.7 Right-click context menu: Scene Tree node → "Generate Script", "AI Set Property"

## 11. LLM System Prompt Templates

- [ ] 11.1 `prompts/scene_generate.txt` — uzscene spec + output rules, loaded at MCP server start
- [ ] 11.2 `prompts/shader_assist.txt` — HLSL conventions + shader system spec + output rules
- [ ] 11.3 `prompts/script_generate.txt` — Lua API spec + engine event table + output rules
- [ ] 11.4 `prompts/scene_analyze.txt` — analysis JSON schema + improvement heuristics + output format

## 12. Testing

- [ ] 12.1 `scene_generate`: "a room with a point light, a box, and a camera" → valid `.uzscene` loads in engine
- [ ] 12.2 `scene_generate`: LLM hallucination (invalid node type) → `validate_uzscene_syntax` returns error, file not saved
- [ ] 12.3 `shader_assist`: "solid red color" → HLSL compiles clean, viewport shows red material
- [ ] 12.4 `shader_assist`: DXC compile error → returned in `errors[]`, no screenshot (no crash)
- [ ] 12.5 `ai_set_property`: "make the light blue" on DirectionalLight node → `scene.setProperty` called with correct RGB
- [ ] 12.6 `scene_analyze`: identifies a node with 50k triangles as a performance warning
- [ ] 12.7 `script_generate`: "patrol between waypoints" → valid Lua with `ctx.nav:set_destination` calls, attaches to node
- [ ] 12.8 `provider = "none"`: all tools return formatted prompt string without calling external API
- [ ] 12.9 API key from `UZ_AI_API_KEY` env variable overrides empty `project.uzproj` key
- [ ] 12.10 git safety: warning logged when non-empty `api_key` detected in tracked `project.uzproj`
