# 06 - Agents, asset creation and production workflows

Date: 2026-10-22. Scope: source inspection of `mcp/`, `runtime/engine/asset/`, `runtime/tools/uzcook.cpp`, selected extension code, build scripts and existing task checklists. No engine build, import, provider request or DCC session was executed for this chapter. External documentation was consulted for proposed integration contracts. Source locations refer to this working tree, not an immutable release.

## Executive conclusion

UzEngine already has substantial asset-processing code and an implemented documentation MCP server with protocol tests. It does not yet demonstrate an integrated human-facing asset-production system with provider jobs, provenance, review, credential management and transactional publication. A native editor should reuse the importers, file formats, VFS and documentation search while adding explicit application services around them.

The distinction matters: an MCP tool available to the developer's assistant is not automatically a capability shipped to users. Similarly, a document describing a Meshy workflow is not a Meshy client, and a VSCode layout preserving a chat panel is not an agent-session integration.

## Evidence-based inventory

| Capability | Observed implementation | Present limitation or qualification |
|---|---|---|
| Documentation MCP | `mcp/src/server.ts:44` registers `search_docs`; `:76`, `:98`, `:134`, `:154`, `:167` register entity, relationship, data-flow, subsystem and reference queries | These six tools operate on documentation. No scene mutation or provider-generation tool is registered in this server |
| MCP transport and tests | `mcp/src/server.ts:220` starts a server with injected transport; `:236` uses stdio; `mcp/tests/server.test.ts:79` checks six tools over real in-memory protocol transport | Existing protocol tests are not evidence of a native editor agent client |
| AI project tooling | Repository has `.claude/` and `.codex/` rules/hooks; `.codex/hooks/validate-commit.sh:4` and `.claude/statusline.sh:2` identify developer tooling | Development environment configuration must not be counted as product integration |
| Meshy | `docs/specs/3.0-mcp-integration.md:86` describes provider selection; `:104` describes generated mesh flow | No matching provider implementation found in inspected product source; spec is marked Planned at `docs/specs/README.md:37` |
| EZPainter | No matching implementation or contract located in the scoped repository searches | Treat as an external project named by the owner, with its implementation and interoperability still to audit; absence here says nothing about another repository |
| Claude Code / Codex | AI-oriented documentation and development hooks exist | No provider-specific client, durable session broker or approval/diff UI located in inspected extension, tools or MCP source |
| Blender / Substance | Importers consume interchange assets; tangent documentation mentions DCC compatibility at `runtime/engine/asset/mesh/mikktspace_tangents.h:21` | Import compatibility is not a live DCC bridge; no product-side DCC adapter found in inspected source |
| glTF / GLB | fastgltf parser, material extraction, primitive/submesh handling, mesh optimization, LOD work and SDF sidecars in `runtime/engine/asset/importers/mesh_importer.cpp:367`, `:390`, `:431`, `:683`, `:752` | Importer selects `gltf.meshes[0]` at `:383`; this is not arbitrary scene/hierarchy import |
| FBX | ufbx import with skeleton, skinning data and animations in `runtime/engine/asset/importers/fbx_importer.cpp:1247`, `:1922`, `:1933`; animation-only output at `:1298` | Selects first mesh at `:1242`; full project-wide DCC roundtrip is not established |
| Texture import | PNG/JPG/JPEG/TGA extensions at `runtime/engine/asset/importers/texture_importer.cpp:215`; compression includes BC1/3/4/5/7 at `:166` | Uses etcpak and `.uztexture`; do not describe the implemented path as ispc_texcomp to DDS |
| Asset cook | `runtime/tools/uzcook.cpp:359` registers shader, mesh, FBX and texture importers; source hash and sidecar checks at `:429`; thread-pool processing at `:514` | CLI orchestration has correctness gaps described below; not yet a durable application job system |
| Sidecars and cache | `.uzimport` stores importer, version, source hash, options, outputs and dependencies; `runtime/engine/asset/import_sidecar.h:56` | This differs from older `.import.json` descriptions; settings invalidation and atomic publication need work |
| Packs and manifests | `runtime/tools/uzcook.cpp:677` emits `.uzpak` and `.uzmanifest`; asset code contains split/streaming archive facilities | `PakCompressBlob` uses LZ4 at `runtime/engine/asset/formats/pak_format.cpp:259`; do not equate every writer with this compression path |
| VFS | Loose files, pak loading, lazy reads and layered mounts in `runtime/engine/asset/vfs.h:139`, `:222`, `:385` | Streaming infrastructure is reusable; current throughput and contention require measurement |
| Python | Large collection of provisioning, diagnostics and reference-test scripts under `scripts/` | No embedded Python runtime found in scoped engine/extension code search; external automation, code editing and runtime scripting are separate features |
| Native dependencies | Zig build consumes fastgltf, meshoptimizer, ufbx, LZ4 and etcpak; `runtime/build.zig:445`, `:608`, `:690`, `:697`, `:734`; lock file at `scripts/native-dependencies.lock.json` | Dependency sourcing is mixed. Do not replace actual build facts with older vcpkg-only architectural assumptions |

Searches excluded credentials, generated dependency trees and personal directories. No neighboring EZPainter or Meshy project was inspected because no concrete source-project location was established in the reviewed material.

## Findings

### F-023 - Documentation MCP is not the proposed engine-control MCP

**Evidence:** The complete tool-registration block at `mcp/src/server.ts:44` through the registration beginning at `:167` exposes six documentation tools. `mcp/tests/server.test.ts:79` explicitly verifies that count. `docs/specs/3.0-mcp-integration.md:53` describes broader server work and `:124` lists future deliverables.

**Impact:** A native editor cannot obtain scene editing, capture, build, import or asset generation merely by connecting to the current MCP server. The report must not present those capabilities as already shipped.

**Recommendation:** Retain documentation search as its own capability. Expose validated editor commands through a separate, versioned automation surface that shares transaction and validation code with human actions.

**Confidence:** High for registered tools; no claim about external MCP servers outside this repository.

### F-024 - Provider and agent integration is documented or environmental, not demonstrated as a product workflow

**Evidence:** Meshy appears in `docs/specs/3.0-mcp-integration.md:86` and `:104`; that spec is Planned at `docs/specs/README.md:37`. Searches of product source found no Meshy/EZPainter client or Claude/Codex session adapter. `extension/src/engine-mode.ts:50` implements a mode toggle, and `:60` opens/focuses editor panels; it does not implement the AI extension detector described by `docs/engine/32-engine-mode/01-overview.md:37`.

**Impact:** Agent sessions, auth, provider jobs, usage, artifacts and approvals are application work still to implement. A redesigned shell alone does not deliver the requested integrated workflow.

**Recommendation:** Build provider adapters and human review around durable application services. Audit the external EZPainter and model-generation repositories before selecting their integration mechanisms.

**Confidence:** High within inspected source scope; external-project maturity remains unknown.

### F-025 - Incremental cook and publication do not yet form a reliable build contract

**Evidence:** On a cache hit, `runtime/tools/uzcook.cpp:469` iterates cached outputs and `:472` constructs a shader suffix; `:475` assigns `AssetType::Shader` irrespective of the actual importer. Registration includes meshes, FBX and textures at `:364`. Pack/manifest write failures are logged at `:683` and `:690`, while the final exit code at `:694` only reflects import failure count.

**Impact:** A warm cook can describe cached non-shader assets differently from a clean cook. A failed packaging step can leave automation observing a successful process exit. These defects would become misleading green jobs in a new editor.

**Recommendation:** Preserve typed output descriptors across clean and cached imports, include companion outputs, propagate publication failures to the job result, and compare clean versus incremental package manifests and runtime resolution.

**Confidence:** High from control-flow inspection; not reproduced by executing the cooker during this analysis.

### F-026 - Sidecar persistence and invalidation need stronger guarantees

**Evidence:** `runtime/engine/asset/import_sidecar.cpp:142` checks source hash, importer version and output presence, but not changed import options or dependency contents. The cooker creates fresh default options at `runtime/tools/uzcook.cpp:482` and records selected values at `:592`. `WriteSidecarFile` calls `FileWriteAllText` at `runtime/engine/asset/import_sidecar.cpp:113`; the Windows implementation writes with `CREATE_ALWAYS` at `runtime/platform/windows/file_system.cpp:407`, despite the sidecar header's atomic-write description at `runtime/engine/asset/import_sidecar.h:104`.

**Impact:** Artist setting changes need a defined invalidation path. Interrupted writes can leave invalid metadata. UI import settings cannot safely be treated as persistent source-of-truth merely because an options object exists in a sidecar.

**Recommendation:** Version the import recipe, include settings and dependencies in staleness decisions, write temporary files then atomically replace, and publish source/compiled outputs/metadata as an explicit transaction.

**Confidence:** High for the inspected Windows path and current skip predicate.

### F-027 - Interchange import is valuable but narrower than an artist asset-production system

**Evidence:** glTF selects the first mesh at `runtime/engine/asset/importers/mesh_importer.cpp:383`; FBX does the same at `runtime/engine/asset/importers/fbx_importer.cpp:1242`. Skeleton and clip output exist at `:1922` and `:1933`. `.uzimport` fields at `runtime/engine/asset/import_sidecar.h:56` cover import metadata but do not establish provider job, prompt, source-image rights, approval or source-version lineage. The importer registry at `runtime/engine/asset/importers/asset_registry.h:29` is a registry of importer implementations, not a complete asset catalog.

**Impact:** Multi-object generated scenes, stable identities through DCC roundtrip, model provenance, variant review and safe replacement remain distinct requirements. Existing resource formats should not be discarded, but neither should they be mistaken for a complete asset database.

**Recommendation:** Add an application asset catalog with stable identity, typed outputs, dependency edges, source provenance and version history. Explicitly reject or explain unsupported multi-object features during import.

**Confidence:** High for inspected importer/sidecar contracts; full runtime asset ownership is covered in the engine inventory.

### F-028 - Existing tests and task metadata do not prove end-to-end production readiness

**Evidence:** `runtime/tests/engine/asset/test_cook_pipeline.cpp:3` defines its end-to-end path around shaders; tests skip without DXC at `:97` and on non-Windows at `:373`. glTF tests use a small valid GLB at `runtime/tests/engine/asset/test_mesh_importer.cpp:159`; texture tests cover small PNGs and determinism at `runtime/tests/engine/asset/test_texture_importer.cpp:145` and `:332`. Multi-material task item at `.rulebook/tasks/phase5-5.1.0-multi-material-multi-uv/tasks.md:40` remains unchecked despite primitive-boundary code at `runtime/engine/asset/importers/mesh_importer.cpp:431`.

**Impact:** Test-file presence is not fresh passing evidence. Stale unchecked tasks can also exaggerate missing work. Neither counts nor status labels establish artist workflows, provider failures or recovery behavior.

**Recommendation:** Reconcile source and checklists, then add acceptance fixtures that exercise actual cooker CLI, mixed assets, cache hits, missing companions, changed settings, failure exit codes, restart recovery and DCC roundtrip.

**Confidence:** High for test scope and metadata mismatch. No fresh pass/fail or coverage percentage is claimed here.

## Proposed application service contracts

These services are recommendations, not assertions about existing modules. Keep them above the path-agnostic runtime. Project locations enter through the application/project configuration and explicit APIs.

| Service | Minimum contract | Why the human editor needs it |
|---|---|---|
| Job service | Durable local ID, provider ID, operation type, parent dependencies, stage, progress, timestamps, errors, retry policy, resource budget and cancel capability | Closing a panel must not lose work or start another paid generation |
| Provider adapter | Versioned capability discovery, parameter schema, submission, status, artifact enumeration and supported cancellation semantics | Different providers and model versions do not support identical controls |
| Artifact staging | Download state, checksums, byte limits, format validation and durable temporary ownership before promotion | Generation success is distinct from successful local import |
| Asset catalog | Stable asset ID, source/output mapping, dependencies, variants, import recipe and version history | Renames and reimports must not silently break references |
| Provenance | Provider, job ID, model/version, prompt, reference hashes, parameters, creation time, rights/attribution and approval | Teams need traceability when an asset is reused or published |
| Credential store | OS-managed secret reference; renderer/UI receive connection status, not secret values | Project files, logs and screenshots should not contain API keys |
| Agent session service | Provider session ID, project scope, capabilities, streamed events, permissions, cost, artifacts, cancellation and reconnect state | Chat is only one view of an execution session |
| Command service | Typed request, preconditions, dry-run/preview when meaningful, transaction, result and audit event | Human and agent edits must obey the same world/document ownership rules |
| DCC adapter | Tool/version handshake, readiness, document identity, supported export preset, file result and failure status | A process that exists is not necessarily ready to accept work |

Recommended local job stages: queued, submitted, running, downloading, validating, awaiting-review, importing, publishing, completed, failed and abandoned. Provider status remains separate. A cancel request is an event whose outcome depends on capabilities; it is not an unconditional promise of remote cancellation. Persist requests before dispatch, reconcile ambiguous submissions, and do not retry a paid POST blindly after a network timeout. A local deduplication key does not imply that a provider supports server-side idempotency.

Keep provider networking, import and indexing outside the render thread. Limit concurrent CPU/GPU jobs. Expose queue/resource use so an artist can retain interactive viewport performance during compression, baking and model downloads. Do not apply imported content to the active scene from a worker callback; hand over a validated command at the application's mutation boundary.

## Meshy integration design

Provider behavior, retention and distribution policies below reflect documentation reviewed on 2026-10-22. Revalidate these contracts against the selected provider and client versions before shipping; they are not permanent guarantees.

The documented Text-to-3D workflow separates geometry preview from refinement. Use that distinction in the UI: approve shape before starting a texture-producing stage. Persist both task IDs, their relationship and the local artifacts. The API exposes SSE status streams as well as GET status queries. [Meshy Text-to-3D documentation](https://docs.meshy.ai/en/api/text-to-3d).

Cancellation must remain truthful. Deleting an `IN_PROGRESS` Text-to-3D task returns `409` and processing continues; stopping local observation does not stop the cloud task. Label local actions accordingly and offer remote deletion only when supported by the provider state. [Text-to-3D deletion contract](https://docs.meshy.ai/en/api/text-to-3d), [task lifecycle](https://docs.meshy.ai/en/api/task-lifecycle).

Download completed artifacts promptly. Current documentation states retention of at most three days for API-generated files, with an Enterprise exception. Signed URLs expire at the task expiry and GET does not refresh them. Store the source artifact locally, not merely its URL. [Asset retention](https://docs.meshy.ai/en/api/asset-retention).

For a standalone desktop application, prefer SSE plus GET reconciliation initially. Webhooks require a publicly reachable HTTPS endpoint and need duplicate/out-of-order handling; use them when a team backend actually exists. [Webhooks](https://docs.meshy.ai/en/api/webhooks).

Model options must come from a versioned catalog rather than permanent UI constants. Provider model retirement and parameter changes affect reproducibility. Store the resolved model version in provenance. Display usage and cost as provider-reported or estimated, explicitly distinguished.

Record license conditions and reference-image rights separately. Meshy documentation distinguishes free-plan CC BY 4.0 output and paid private ownership; source images still require appropriate rights. This is provenance evidence, not a guarantee of every downstream marketplace's acceptance. [Ownership](https://help.meshy.ai/en/articles/10137554-what-is-the-ownership-of-the-generated-models), [marketplace and reference rights](https://help.meshy.ai/en/articles/16102951-can-you-publish-or-sell-meshy-models-on-marketplaces).

## EZPainter, Blender and Substance workflow

EZPainter requires a dedicated source audit before committing to embedding its UI, sharing a renderer or operating it as a sidecar. Establish ownership, license, executable/API, project format, layers, masks, baking, texture export, automation, crash recovery and tested platform support. Do not infer any of these from the product name.

The first integration should be a traceable send/edit/receive workflow. Send an asset ID plus a specific source version; receive exported artifacts and an explicit version association; compare material/mesh output before replacing the approved asset. Preserve the original editable DCC project alongside compiled outputs.

Substance Painter provides Python scripting and remote control when started with `--enable-remote-scripting`; readiness must be checked before sending work. Its export API exposes explicit success, cancellation, warning and error results. Use supported APIs and capture export status rather than assuming that a newly created file means the entire export succeeded. [Remote control](https://experienceleague.adobe.com/en/docs/substance-3d-dev/painter-python/tutorials/remote-control), [export API](https://experienceleague.adobe.com/en/docs/substance-3d-dev/painter-python/api/substancepainter-package/export).

Define an UzEngine interchange preset: coordinate system and scale, tangent basis, normal-map convention, sRGB versus linear channels, ORM packing, material-slot IDs, UV channels, UDIM policy, texture limits, skeleton identity and supported animation data. Blender glTF supports a defined material model and extensions; arbitrary DCC shader graphs do not automatically transfer. [Blender glTF manual](https://docs.blender.org/manual/en/5.2/addons/scene_gltf2.html).

## Agent interaction and human review

Run agents as supervised processes/services separated from renderer lifetime. The Claude Agent SDK exposes Claude Code capabilities through Python/TypeScript, with sessions, hooks, MCP and permission controls; CLI subprocess integration is another documented option. Use API authentication for a third-party product unless the vendor has explicitly approved another route; do not assume an embedded product can offer ordinary claude.ai subscription login. [Claude Agent SDK overview](https://code.claude.com/docs/en/agent-sdk/overview).

Store explicit session IDs per workspace/task. A resumed conversation restores conversational history, not a filesystem snapshot. Maintain code/asset changes, source versions and review state in the editor's own version-control and document services. [Session semantics](https://code.claude.com/docs/en/agent-sdk/sessions).

The UI should distinguish text, tool execution, permission requests, generated artifacts, diagnostics and final result. Tool policies and interactive permission callbacks belong in the adapter; renderer commands remain validated in the application. [SDK permissions](https://code.claude.com/docs/en/agent-sdk/permissions).

For Codex, use an app-server process adapter for an interactive custom editor. The documented interface supports JSON-RPC over stdio JSONL, initialization, thread start/resume, turn start/interrupt, streamed item events and server-initiated approval requests scoped to thread/turn identity. Pin the CLI and generate matching schemas. The SDK serves programmatic automation; it is a distinct integration choice. The current SDK documentation states that `codex mcp-server` was removed. [Codex app-server](https://learn.chatgpt.com/docs/app-server), [Codex SDK](https://learn.chatgpt.com/docs/codex-sdk).

The generic agent service should normalize only common concepts while preserving provider-specific capabilities. Do not parse terminal decoration or assume all providers have identical approval, resume or cancellation behavior. Product branding is also a distribution consideration: the Claude SDK documentation permits names such as Claude or Claude Agent, rather than using Claude Code as the name of a third-party SDK integration. Use Claude in proposed product mockups. [Claude Agent SDK overview](https://code.claude.com/docs/en/agent-sdk/overview).

Suggested review surface: task and provider identity; selected scene/entities/files; current operation; pending permission; changed files and asset variants; tests/build results; usage; accept/discard actions with exact scope. Avoid sending the entire project or screenshots automatically when the user chose a smaller context. Preserve unsaved human edits and detect conflicting changes before application.

## Human-facing workflow screens

| Workspace | Primary human task | Required surfaces |
|---|---|---|
| Asset Lab | Inspect and select an asset | Searchable catalog, native mesh/material preview, metadata, dependencies, versions and import recipe |
| Generation | Produce and compare candidate assets | References, prompt, model/options, usage estimate, persistent jobs, variant comparison, shape approval and import approval |
| Paint | Refine material appearance | 3D/UV view, layers/masks where supported, PBR channels, bake/export status and source DCC link |
| Code | Edit scripts and engine/game code | Monaco or equivalent, language services, diagnostics, project targets, environment selection and separate debug/terminal surfaces |
| Agent Review | Inspect and approve automation output | Session timeline, context scope, permissions, diff, generated assets and test evidence |
| Jobs | Monitor long-running work | Queue, dependency stages, provider/local status, resource budget, retries and truthful cancellation |
| Publish | Produce a validated package | Dependency closure, license/provenance gaps, import errors, content validation, package result and logs |

Scene selection, document identity and asset ID should persist across these workspaces. A mode switch should change task-oriented presentation, not create another independent copy of the project state. Proposed screenshots elsewhere in this report illustrate these workflows; they are concepts, not existing product captures.

## Existing task mapping and new scope

| Work | Existing owner or planning status |
|---|---|
| Multi-material and multi-UV import correctness | `.rulebook/tasks/phase5-5.1.0-multi-material-multi-uv/tasks.md:39`; reconcile implemented primitive handling before treating every unchecked entry as missing |
| FBX skeleton/animation pipeline and validation | `.rulebook/tasks/phase3-3.0.0-fbx-skeletal-animation-pipeline/tasks.md:46`; real-asset test item remains at `:105` |
| Audio importer | `.rulebook/tasks/phase4-4.4.0-audio-core/tasks.md:11` proposes registration; no audio importer is registered by the inspected cooker |
| Current lifecycle/ownership and measured VFS improvements | `.rulebook/PRIORITIES.md` and scoped milestones in `phase0_project-stabilization-audit`; a GUI plan must not erase these dependencies |
| Runtime-control MCP and AI content pipeline | `docs/specs/3.0-mcp-integration.md`; reconcile scope against the documentation-only server |
| Provider job persistence, provenance, credentials, DCC adapters and agent review | Proposed application scope from this analysis; no implementation task was created by this chapter |
| Cook issues F-025/F-026 | Concrete candidates for scoped stabilization tasks; audit existing owners before creating overlapping work |

## Acceptance evidence required before calling the workflow production-ready

1. A clean and cached mixed-asset cook produce equivalent logical outputs, types, dependency closure and runtime resolution.
2. Changing import settings or a dependency invalidates the correct outputs; interrupted publication preserves the last usable asset.
3. Pack or manifest write failure produces a failed CLI and failed UI job, with the actual error visible.
4. Restarting the editor recovers provider tasks without duplicate paid submissions; a completed cloud result is downloaded and verified once.
5. Expired URLs, quota/rate limits, malformed assets, provider errors and unsupported remote cancellation produce distinct actionable states.
6. A real DCC edit returns to the same asset identity, with material channels, normal convention, transforms and dependencies verified in-engine.
7. Agent permission, cancellation, session recovery and conflicting human edits are exercised through the actual UI adapter.
8. Import/bake/indexing load is measured alongside viewport input latency and frame time; jobs respect resource budgets.

These are acceptance requirements, not tests executed during this report. Existing source and historical test records remain useful evidence, but should not be relabeled as fresh validation.
