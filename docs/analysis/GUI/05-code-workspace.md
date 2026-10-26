# 05. Code workspace and language tooling

Status: proposed, with current-source boundaries identified below. Date: 2026-10-22.
This chapter specifies the human coding workflow for the [native editor proposal](04-native-architecture.md). It does not claim a Monaco host, language-server supervisor or debugger adapter already ships with UzEngine.

## Recommendation

Code opens by document type, not by a global Code tab. Opening a C++, Python or Lua file selects the code editor; its main tab shows the filename. Opening scene/material/rig/animation documents selects their respective editors with contextual layouts. Project/worktree scope remains shared, while each document retains its own revision, dirty state and view state. Agent review, terminal, diagnostics and jobs are docks attached to this document-oriented shell.

A scene's optional Visual/Linked code control follows an explicit source reference. The linked source remains an independent document; it is not automatically generated C++, and a scene need not have any linked C++ file. Show missing or multiple associations explicitly. Scene text serialization, when exposed, is a separate representation requiring validated roundtrip semantics.

Host Monaco in an optional WebView2 code workspace while keeping project/document ownership in native application services. Start language support with C++ through clangd, Python through Pyright and a selected interpreter, and Lua through Lua Language Server configured for the project's LuaJIT runtime. Add debugging through separate DAP adapters with verified launch/attach capabilities.

Keep VSCode available as an external companion until the embedded workspace passes real authoring and debugging gates. The existing extension can remain useful for navigation and optional preview while direct native presentation becomes the proposed primary local scene experience. Supporting an external editor is a product capability, not evidence that the new shell has failed.

Monaco is a browser-based editor library, not a native widget and not the VSCode extension host. Native hosting means a desktop application embeds its web surface. It does not make TypeScript/DOM execution native C++, eliminate browser overhead or make arbitrary VSCode extensions compatible.

## F-019. Embedding Monaco supplies an editor component, not a complete IDE

**Observed:** Current code editing is supplied by VSCode. The inspected extension integrates panels and scene documents through `extension/src/extension.ts:101` and `:111`; engine mode at `extension/src/engine-mode.ts:50` does not establish an independent IDE. Monaco's official project describes the standalone browser editor separately from VSCode.

**Implication:** A replacement shell needs document management, language-server transport, debugger adapters, terminal/build integration, workspace trust, settings, shortcuts and recovery. Syntax coloring alone cannot satisfy Python/C++/Lua authoring.

**Proposal:** Budget and validate these as application services. Integrate a small supported language/toolchain matrix before attempting extension compatibility. Do not market arbitrary VSCode extensions as supported without a separate host design and license/API review.

**Confidence:** High for the distinction and current extension dependency; implementation effort remains a proposal.

## F-020. Unsaved documents and concurrent edits require a shared authority

**Observed:** Current scene/editor commands and VFX undo provide useful scoped behavior, but the inspected inventory does not establish a cross-language native DocumentService with recovery, worktree identity and agent conflict handling. This is a missing acceptance contract, not a claim that every existing save path is absent.

**Implication:** Monaco models, filesystem watchers, agents and external editors can each hold a different version of the same file. Blind reload or agent patch application can discard human work even when the UI appears responsive.

**Proposal:** Introduce one document authority with canonical URI, workspace/worktree identity, revision, dirty state, disk baseline and recovery records. Apply edits against explicit versions, and surface conflicts before replacement. Keep document lifetime independent of pane/browser lifetime.

**Confidence:** High for the concurrency requirement; current full implementation was not located in the inspected scope.

## F-021. Language intelligence depends on actual build and execution environments

**Observed:** `runtime/build.zig:53` selects `-std=c++23`; the build also sets platform macros and includes. `runtime/engine/scripting/lua_vm.cpp:53`, `:139` and `:156` implement Lua VM behavior, and `script_system.cpp:274` integrates script lifecycle. Python tooling is not equivalent to an embedded Python gameplay runtime.

**Implication:** A generic C++ parser, arbitrary Python installation or default Lua version can produce misleading diagnostics and invalid completions. UI language support cannot be inferred from syntax highlighting or the presence of scripting APIs.

**Proposal:** Generate clangd's compilation database from the real configured build, bind Python to an explicit environment, and configure LuaLS for LuaJIT plus generated engine API declarations. Treat embedded Python execution as separate product scope unless explicitly designed and implemented.

**Confidence:** High for current C++23/Lua evidence; proposed language-server integration is not validated here.

## F-022. Debugging and hot reload are independent capabilities

**Observed:** Existing runtime script execution and editor play/stop commands do not prove a DAP debugger, native breakpoint workflow or C++ code replacement. The inventory contains Lua lifecycle/hot-reload work, not evidence of universal native live coding.

**Implication:** An editor can offer completion without debugging, and debugging without safe hot reload. Treating these as one checkbox would overstate product readiness and obscure compiler/debug-symbol compatibility.

**Proposal:** Introduce debug configurations and independently verified adapters. Support build/relaunch for C++ initially; only advertise hot reload for explicitly implemented paths with state/lifetime tests. Python debugpy and a selected Lua adapter need their own environment, attach and lifecycle acceptance.

**Confidence:** High for capability separation; no new debugging session was executed in this report.

## DocumentService contract

The proposed service owns content and lifecycle; Monaco is one view and edit producer. Multiple panes may show the same document model without creating independent copies. Scene/asset documents may have specialized representations, but share identity, dirty/conflict and recovery conventions where appropriate.

| Field or operation | Required meaning |
|---|---|
| Document ID and URI | Stable application identity plus normalized file/custom URI; preserve display path separately |
| Workspace/worktree identity | Disambiguates identical relative paths in different checkouts and agent worktrees |
| Content revision | Monotonic per-document version used by bridge, language service and edits |
| Disk baseline | Last known disk content/hash and relevant metadata; mtime alone is insufficient for conflict detection |
| Dirty state | Current buffer differs from saved baseline; not equivalent to Git modified status |
| Encoding and newlines | Preserve intended encoding, BOM and line ending policy; show conversion explicitly |
| Edit transaction | Expected revision, edit ranges, origin, undo group and resulting revision |
| Save | Check disk baseline, write through application service, update baseline only after success |
| Autosave | Configured policy with clear distinction between file save and recovery checkpoint |
| Recovery | Persist recoverable buffer and identity independently of browser state; restore after process restart |
| External modification | Reload clean buffers safely; offer comparison/merge when dirty; never silently overwrite |
| Rename/delete | Track pending operations and watchers; preserve recoverable unsaved buffers |
| Close/dispose | Ask for unresolved human document decisions when needed; detach models/listeners without losing recovery |

Canonicalization must respect Windows case behavior, symlinks/junctions, UNC paths, Unicode and project roots. Do not conflate different worktrees by normalizing only a relative filename. Authorization resolves the actual target and allowed workspace, not just a string prefix. Virtual/generated/read-only documents need explicit schemes and write policy.

For agent modifications, retain the base document/disk revision used to propose the change. A clean current target can accept a validated patch. A dirty or changed target requires a three-way comparison or explicit conflict resolution. Showing a Git diff after overwriting unsaved text is not recovery. Agents in isolated worktrees must propose promotion to the active workspace as a separate operation.

Save failure leaves the buffer dirty and shows the error. Permission loss, disk full, unavailable network paths and interrupted writes require distinct handling. Prefer temporary write plus reviewed platform replacement semantics; do not promise durability from rename alone. Recovery snapshots and explicit save operations have separate retention and cleanup policies.

Monaco may render an edit before the native service acknowledges it. Sequence edits, acknowledge the accepted document revision, and distinguish browser-local changes, native-accepted changes and durably checkpointed changes. A browser crash can lose edits not yet accepted; a host crash can lose changes not yet checkpointed. Specify and test that recovery window rather than promising lossless recovery from every keystroke. Echoed native updates must not create duplicate edits or undo entries.

## Language-service matrix

| Language | Proposed intelligence | Configuration authority | Missing acceptance |
|---|---|---|---|
| C++23 | clangd LSP: diagnostics, completion, symbols, navigation and rename | Active configured build and generated `compile_commands.json` | Correct platform macros, includes, generated headers, target selection and index load |
| Python | Pyright LSP: types, completion, imports and navigation | Workspace interpreter/venv, Python version and project settings | Environment switching, editable packages, generated sources and diagnostics parity |
| Lua | Lua Language Server: diagnostics, symbols, completion and annotations | LuaJIT version/compatibility and generated UzEngine declarations | Bound API parity, project module paths, sandbox exclusions and reload diagnostics |
| HLSL | Separate evaluated shader service or supported tooling | Shader includes, defines, entry points and target profiles | Future scoped integration; do not imply C++ tooling covers shader compilation |

### C++23 and build truth

Generate each compilation command from the selected build target, source, working directory, defines, include directories and language standard. Current Zig flags at `runtime/build.zig:53` explicitly request C++23. Do not use a generic C++20 fallback and then treat false diagnostics as engine errors. Generated headers must exist or produce a clear configuration/build prerequisite.

clangd consumes compilation commands; it does not replace Zig as build authority. Investigate a reproducible database-export step appropriate to the pinned Zig version and current build scripts. This report does not claim an existing exporter or prescribe an unverified flag. Distinct debug/release, DLL/static and target configurations need distinguishable databases and index invalidation.

Pin a clangd version compatible with the toolchain's language/library features. If compiler-driver querying is needed, allowlist only intended trusted toolchain executables and paths. Do not let a project-supplied command become an unrestricted process-launch mechanism. Run indexing with resource limits so opening the engine source does not monopolize CPU and disk while editing a scene.

Show active target and build configuration beside build/debug actions. Problems should retain source URI, version, severity, tool and target identity. A diagnostic against an old revision is stale, not proof that the current text fails. Compiler output and language-server diagnostics can differ and should be labeled by source.

### Python environments and tool execution

Offer an explicit interpreter/venv selector per workspace or tool configuration. Pyright settings must describe the same import roots and Python version used for the selected scripts. Environment discovery may suggest candidates but must not silently install packages or switch the interpreter for existing jobs.

Use a supervised Python process for project automation and external-tool scripts. Working directory, environment variables, arguments, stdout/stderr and cancellation belong to a run configuration. A shell terminal remains optional; structured process launch avoids requiring every task to be encoded as shell text.

Debugpy supports Python debug sessions through a debugger adapter. Bind it to the selected environment and test launching and attaching independently. Debug access must remain scoped to the intended local process; do not expose a debugger listener on arbitrary interfaces by default.

This workflow does not add Python to the game runtime. The inspected runtime embeds Lua/LuaJIT. Adding Python-driven ECS callbacks, interpreter lifecycle or GPU access would be a separate engine architecture decision with explicit threading and performance requirements.

### LuaJIT and generated engine APIs

Configure LuaLS using the actual LuaJIT language/runtime expectations, require paths and project roots. Expose only APIs available to the selected runtime mode. Lua's general standard library and a sandboxed UzEngine script environment are not interchangeable.

Generate versioned declaration stubs from an authoritative binding schema or validated extraction pipeline. The stubs need function signatures, overloads, parameters, return types, enum values, ownership/lifetime notes and availability. Validate representative declarations against real bindings; a hand-maintained file that drifts would create misleading completion.

Generated declarations are editor metadata, not executable replacements for missing engine functions. Link documentation and errors to real binding/source locations. Surface Lua VM errors with script URI, line/column where available, entity context and play-session ID; distinguish compile, runtime and reload errors.

Choose a Lua debugger adapter only after verifying LuaJIT and sandbox compatibility. Breakpoint resolution, coroutine behavior, pause semantics and engine frame interaction require a prototype. Do not imply that adding LuaLS provides breakpoints or that arbitrary debugger hooks are permitted in every sandbox.

## DAP and process control

LSP handles language intelligence; Debug Adapter Protocol handles debug sessions. They use distinct servers, lifecycle and capabilities. Maintain separate session IDs and state machines, and report unsupported operations rather than rendering inert controls.

| Operation | Required contract |
|---|---|
| Build before launch | Selected target, cancellation, exit status, output paths and structured diagnostics |
| Native launch/attach | Tested adapter, matching executable/symbols, compiler/runtime compatibility and process identity |
| Breakpoints | Source identity, pending/verified/rejected status, relocation and condition support |
| Pause/step | Thread/frame selection, actual adapter capabilities, clear engine paused state |
| Variables/watch | Scoped evaluation, unavailable/optimized-out values, bounded expansion |
| Stop/disconnect | Distinguish terminate-owned-process from detach-from-external-process |
| Session failure | Preserve documents and last diagnostics; settle pending requests and allow restart |

Select a Windows C++ adapter through a toolchain spike. Validate PDB/debug information and license/distribution terms; neither clangd nor Monaco supplies this automatically. Build/relaunch is the honest initial C++ workflow. DLL reload requires explicit object ownership, active-call, allocator and state-migration contracts; no generic live-coding guarantee follows from having a DLL build option.

Separate debugging a launched game/tool process from debugging the native editor itself. A debugger that stops the host process also stops its UI threads, so an in-process code pane cannot reliably operate that paused session. Use an external debugger for editor/runtime code executing inside the host, or launch a separate debug target controlled by the editor. The latter is a debug execution mode, not proof that the primary viewport already supports out-of-process rendering. Language-specific pause behavior must also be tested against the engine frame owner.

Audit redistribution and permitted-host terms for the exact debugger binaries. Open protocol support does not grant rights to bundle an arbitrary VSCode extension, proprietary debugger or marketplace package in another editor. Likewise, select Pyright explicitly; do not assume Microsoft's Pylance extension can be embedded as an equivalent unrestricted component.

## Browser host and message bridge

Bundle Monaco, its workers and language assets for offline operation. Serve them through a reviewed local virtual-host/resource mapping with a stable origin; do not rely on CDN availability or permissive `file://` behavior. Test worker loading and source maps against the shipped configuration, not only a development server.

Apply a restrictive Content Security Policy compatible with the exact bundled worker strategy. Limit navigation and new-window behavior; open approved external links through an explicit host action. Treat project files, diagnostics, markdown and agent output as untrusted display content. Do not interpolate them as executable HTML or JavaScript.

Expose a small typed bridge: open/bind model, apply versioned edits, request save, select location, publish diagnostics and invoke allowlisted commands. Validate message schema, size, origin/session identity and document revision. Browser code must not receive a generic filesystem, shell or engine-call escape hatch. Large content needs bounded/chunked transport and cancellation rather than unbounded JSON messages.

On browser restart, restore models from DocumentService, reconnect diagnostics by version and recover focus without duplicating language servers. Dispose Monaco models and subscriptions when the last view closes, subject to recovery policy. Keep renderer state and GPU presentation independent of browser frames, crashes or garbage collection.

## Input, focus and human workflow

Use a single application command registry with contextual routing. Text-edit shortcuts go to Monaco when its editor owns focus; scene navigation shortcuts go to the viewport when it owns focus. Global save/build/search commands have an explicit precedence policy. Never forward viewport movement keys while a user types into code, a search field or an IME composition.

Test key-up delivery across focus changes, modifier release, dead keys, AltGr, non-US layouts, clipboard, multi-cursor and IME composition. Text input and physical-key navigation are different event streams. Native shortcuts must not commit or cancel composition unexpectedly. WebView2 ownership follows its GUI/STA thread requirements described in chapter 04.

The code document editor should contain a compact file/symbol sidebar, filename tabs and splits, Problems/Output/Terminal/Debug docks, target/environment selector and optional agent review pane. Keep build state and unsaved/conflict indicators visible. A scene preview can remain beside code through explicit document association, but opening a source file must not instantiate a second independent runtime viewport in the first milestone.

Example complete workflow: select an entity's Lua component, open its script at the correct URI, edit with API completion, save through DocumentService, validate/reload through an explicit runtime action, observe errors tied to the current play session, and return to the same selection. If reload is unsupported or fails, preserve the previous usable session and show the actual failure rather than claiming success from file save alone.

For C++, choose target, navigate with clangd, edit, build, inspect real compiler errors, launch with matching symbols and stop/relaunch after changes. For Python, select environment, run a tool against explicit project inputs, inspect generated artifacts and promote them through the asset/document workflow. These workflows require separate acceptance even when they share one editor surface.

## Alternatives and migration strategy

| Editor component | Advantage | Cost and decision |
|---|---|---|
| Monaco/WebView2 | Familiar code and diff editing; strong browser editor API | Browser host, LSP/DAP adapters and document services remain application work; preferred initial code pane |
| Scintilla/Lexilla | Native editing component with mature lexical infrastructure | Build completion, semantic tooling, diff, accessibility and UX integration; evaluate if browser cost fails measured budgets |
| CodeMirror | Modular browser editor with flexible extension composition | Still web-hosted; requires language/debug/workspace integration; compare on concrete desired features |
| External VSCode | Existing full workbench, extensions and established user configuration | Separate application/focus boundary; retain while native workspace reaches parity |

Use common document URIs, diagnostics and open-at-location commands across embedded and external editing. Do not copy user VSCode settings indiscriminately into Monaco; only import explicitly supported settings with clear mapping. Keep the application language-provider API independent of the selected editor component so a later component change does not replace build/document services.

## Acceptance cases

1. Open the same file in two panes; edits, undo and save remain consistent, and closing either pane preserves the other.
2. Edit unsaved text while an agent or external editor changes disk content; show a recoverable conflict without overwriting either version.
3. Open identical relative paths in two worktrees; diagnostics, patches, build targets and saves remain scoped to the correct checkout.
4. Crash/restart the web surface and then the application; recover dirty buffers according to documented policy without relying on renderer survival.
5. Open C++23 runtime sources with the configured database; verify representative templates/includes and target-specific diagnostics against actual builds.
6. Switch Python environments; imports, launch and debug use the selected interpreter and do not leak another project's environment.
7. Validate Lua completion against real APIs and sandbox restrictions; reproduce script errors and supported reload behavior through the running engine.
8. Execute native, Python and supported Lua debug workflows separately; verify breakpoint state, symbols, stop/detach and unavailable capabilities.
9. Test Unicode paths, mixed line endings, read-only files, external rename/delete, disk-full saves, large files and remote-path interruption.
10. Test keyboard-only operation, screen-reader use, IME, focus transfer and shortcuts across code, viewport, dialogs and agent review.
11. Load all bundled editor assets/workers offline; reject untrusted bridge commands and navigation; restore after browser failure.
12. Measure typing latency, completion latency, large-file open, index CPU/RAM and render p95/p99 with code, agents and background jobs active.
13. Stop a separate native debug target at a breakpoint and verify the editor remains usable; debug the editor process with an external tool and document its expected UI pause.
14. Interrupt the browser/host at edit submission, native acknowledgment and checkpoint boundaries; verify the declared recovery window and absence of duplicated edits.

These are proposed acceptance requirements, not executed tests. Choose supported file-size and workspace-size targets before measuring; communicate degraded large-file features explicitly. Do not claim complete IDE parity from a successful syntax-highlighting demonstration.

## Primary references

- [Monaco Editor](https://microsoft.github.io/monaco-editor/) and [project FAQ](https://github.com/microsoft/monaco-editor#faq): standalone editor and VSCode-extension distinction.
- [clangd getting started](https://clangd.llvm.org/installation) and [compile commands](https://clangd.llvm.org/design/compile-commands): actual build configuration is language-service input.
- [Pyright](https://github.com/microsoft/pyright) and [configuration](https://github.com/microsoft/pyright/blob/main/docs/configuration.md): type checking and environment configuration.
- [debugpy](https://github.com/microsoft/debugpy): Python debugger implementation and supported launch/attach interfaces.
- [Lua Language Server](https://luals.github.io/) and [settings](https://luals.github.io/wiki/settings/): runtime and workspace configuration.
- [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) and [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/): distinct protocol responsibilities.
- [WebView2 local content](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/working-with-local-content) and [security guidance](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security): origin, resource hosting and bridge constraints.
- [Scintilla](https://www.scintilla.org/), [Lexilla](https://www.scintilla.org/Lexilla.html) and [CodeMirror](https://codemirror.net/): alternative editing components.

Continue with [agent and asset workflows](06-agents-and-asset-workflows.md) for provider sessions, work review, generated models and DCC roundtrip.
