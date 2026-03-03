# Tasks: phase9-vfx-ai-generator

## 1. VfxSchemaValidator

- [ ] 1.1 Create `mcp-server/src/vfx/VfxSchemaValidator.ts` using AJV to validate Claude API output against the `.uzfx` v2 JSON schema
- [ ] 1.2 Implement `VfxSchemaValidator.validate(json: unknown) -> ValidationResult`: return `{valid, errors: AJVErrorObject[]}`
- [ ] 1.3 Implement `VfxSchemaValidator.formatErrors(errors) -> string`: produce human-readable error messages for Claude conversation retry injection
- [ ] 1.4 Embed the full `.uzfx` v2 JSON schema in `mcp-server/src/vfx/uzfx-schema.json` as the AJV schema source

## 2. VfxSession Context Store

- [ ] 2.1 Create `mcp-server/src/vfx/VfxSession.ts` defining `VfxSession` interface: `{effect_path, original_description, style, color_palette, claude_reasoning, generated_at, nano_banana_prompts}`
- [ ] 2.2 Implement in-memory session store keyed by `effect_path` with 10-minute TTL
- [ ] 2.3 Implement `VfxSession.get(effect_path) -> VfxSession | null` and `VfxSession.set(effect_path, session)` with TTL refresh on access
- [ ] 2.4 Implement TTL eviction: use `setTimeout` to clean expired sessions; prevent memory leaks for effects that are never refined

## 3. NanaBananaClient

- [ ] 3.1 Create `mcp-server/src/external/NanaBananaClient.ts` as a typed TypeScript wrapper around the Nano Banana REST API
- [ ] 3.2 Implement `NanaBananaClient.generate_sprite(prompt, resolution) -> Buffer` with 30-second timeout
- [ ] 3.3 Implement `NanaBananaClient.generate_flipbook(prompt, frames, resolution) -> Buffer`
- [ ] 3.4 Implement `NanaBananaClient.generate_normal_map(prompt, resolution) -> Buffer`
- [ ] 3.5 Implement exponential backoff on 429/503 responses: max 3 retries with 1s/2s/4s delays
- [ ] 3.6 Store generated PNG files under `res://textures/vfx/ai_generated/{effect_name}/{slot}_color.png` layout
- [ ] 3.7 Trigger `AssetPipeline` import via IPC (`asset.import`) after saving PNG for mip + BC7 processing

## 4. `vfx_generate` MCP Tool

- [ ] 4.1 Create `mcp-server/src/tools/vfx_generate.ts` implementing the 5-step pipeline: Claude API graph generation → HLSL module extraction → Nano Banana sprite generation → IPC hot-reload → screenshot confirmation
- [ ] 4.2 Accept parameters: `description`, `style`, `color_palette`, `reference_image`, `max_emitters`, `budget_particles`
- [ ] 4.3 Build Claude API system prompt: include full `.uzfx` v2 JSON schema, `ParticleState` HLSL interface, all module schemas from `vfx_list_modules`, and style/color hints
- [ ] 4.4 Call Claude API with JSON mode (structured output); on receipt, run `VfxSchemaValidator.validate`
- [ ] 4.5 On validation failure: inject error messages back into Claude conversation and retry (max 2 retries); return error to caller after exhausting retries
- [ ] 4.6 Write validated `.uzfx` to `res://vfx/{sanitized_effect_name}.uzfx` with `_ai_metadata` root key
- [ ] 4.7 Include `_ai_metadata` in generated JSON: `{original_description, style, color_palette, generated_at, model_version, nano_banana_prompts}`
- [ ] 4.8 Register `vfx_generate` in the MCP server tool registry

## 5. `vfx_refine` MCP Tool

- [ ] 5.1 Create `mcp-server/src/tools/vfx_refine.ts` implementing the refinement pipeline
- [ ] 5.2 Accept parameters: `effect_path`, `feedback`, `screenshot_base64?`
- [ ] 5.3 Load existing `.uzfx` from disk and retrieve matching `VfxSession` context
- [ ] 5.4 Build refinement prompt: include current graph JSON + optional screenshot base64 + feedback text + Claude's prior reasoning from session
- [ ] 5.5 Receive targeted patch from Claude; validate the patched graph; write back to disk and call `vfx.hotReload` IPC
- [ ] 5.6 Register `vfx_refine` in the MCP server tool registry

## 6. `vfx_list_modules` MCP Tool

- [ ] 6.1 Create `mcp-server/src/tools/vfx_list_modules.ts` that reads `module_registry.json` and returns `ModuleSchema[]`
- [ ] 6.2 Define `module_registry.json` format: auto-generated from `src/vfx/modules/` HLSL includes during engine build by scanning doc-comment headers
- [ ] 6.3 Implement build-time `generate_module_registry` tool that parses `/// @module`, `/// @param`, and `/// @description` doc-comment tokens from HLSL files
- [ ] 6.4 Register `vfx_list_modules` in the MCP server tool registry

## 7. `ai://generate/` URL Resolution

- [ ] 7.1 Implement URL scheme resolver in `mcp-server/src/vfx/VfxSchemaValidator.ts`: recognize `ai://generate/{url_encoded_prompt}` texture URLs in generated JSON
- [ ] 7.2 Before writing `.uzfx` to disk, resolve all `ai://generate/` URLs to real `res://` paths by calling `NanaBananaClient.generate_sprite`
- [ ] 7.3 Ensure no `ai://` URLs remain in the final `.uzfx` file written to disk

## 8. Testing (≥95% line + branch coverage)

- [ ] 8.1 Write `mcp-server/src/test/vfx_generate.test.ts`: mock Claude API (returns canned `.uzfx` JSON) and Nano Banana API (returns 1×1 PNG); assert full pipeline produces schema-valid `.uzfx` and `vfx.hotReload` IPC is called
- [ ] 8.2 Write `mcp-server/src/test/vfx_generate_retry.test.ts`: mock Claude API to return invalid JSON on first call, valid on second; assert `VfxSchemaValidator` error is injected and retry succeeds
- [ ] 8.3 Write `mcp-server/src/test/vfx_refine.test.ts`: mock Claude API returning a JSON Patch diff; assert patch is applied correctly and session context injected in prompt
- [ ] 8.4 Write `mcp-server/src/test/VfxSchemaValidator.test.ts`: feed 5 known-invalid `.uzfx` structures; assert each returns correct AJV error with human-readable message
- [ ] 8.5 Write `mcp-server/src/test/NanaBananaClient.test.ts`: mock REST API with 429 then 200 responses; assert exponential backoff retries; assert PNG saved to correct path
- [ ] 8.6 Write `mcp-server/src/test/VfxSession.test.ts`: create session, retrieve within TTL, assert eviction after TTL expiry
- [ ] 8.7 Verify ≥95% line and branch coverage across all `mcp-server/src/tools/vfx_*.ts` and `mcp-server/src/vfx/` files using Jest --coverage
