# 03. Existing editor, viewport transport and performance

Audit date: 2026-10-22. Scope: source inspection of the extension, N-API bindings, runtime viewport/encoding path and native UI. No application, benchmark, build or test suite was executed for this chapter. Line references identify the inspected working tree and can move after subsequent edits.

## Executive finding

The current VSCode viewport is a streamed 3D image, not a visible native child window embedded in the editor. The extension forks a worker, the worker loads the N-API addon, the runtime renders to a hidden HWND, and the resulting image travels through GPU readback, CPU staging, H.264 encoding, base64, extension IPC, webview messaging, WebCodecs and a canvas. This is a source-confirmed transport cost, not a measured explanation of every reported slowdown.

The project also has a substantial native VFX editor and a separate, less complete Slate-style widget framework. These must not be collapsed into one maturity claim. A desktop migration can reuse engine/editor behavior while replacing image transport and choosing a shell on evidence.

## Findings register

| ID | Finding | Evidence | Impact | Confidence |
|---|---|---|---|---|
| F-009 | Current process and viewport topology differs from historical in-process-extension/native-HWND guidance | `extension/src/runtime-client.ts:445`; `extension/src/panels/viewport-panel.ts:192`; `runtime/engine/app/runtime.cpp:354` | Architecture decisions based on the old description would optimize the wrong path | High: executable calls inspected |
| F-010 | The interactive viewport incurs GPU readback, CPU copying/conversion and encoded web transport | `runtime/engine/app/runtime.cpp:741`; `runtime/engine/app/runtime.cpp:1413`; `runtime/engine/ipc/h264_encoder.cpp:450`; `runtime/engine/ipc/h264_encoder.cpp:659`; `extension/src/runtime-worker.ts:408` | Adds bandwidth, latency stages and failure modes that direct native presentation can avoid | High for existence; unmeasured for cost share |
| F-011 | Queue bounds and recovery exist, but do not establish end-to-end responsiveness | `extension/src/runtime-worker.ts:375`; `extension/src/runtime-worker.ts:396`; `extension/src/panels/viewport-panel.ts:230`; `extension/src/panels/viewport-panel.ts:735`; `napi/src/render_thread.cpp:238` | Prevents repeating already-fixed unbounded-queue claims; requires frame-age/input instrumentation to judge UX | High for mechanisms; performance unmeasured |
| F-012 | Native VFX editing is materially more complete than the declarative Slate editor prototype | `runtime/engine/vfx/editor/vfx_editor_overlay.h:326`; `runtime/engine/vfx/editor/vfx_editor_overlay.h:430`; `runtime/engine/vfx/editor/vfx_editor_slate.cpp:133` | Existing domain/UI investment can be reused; neither wholesale rewrite nor a claim of a finished desktop shell is justified | High for inspected composition and prototype |
| F-013 | Current Slate text and clipping primitives are insufficient as a general desktop authoring foundation without more work | `runtime/engine/ui/slate_editable_text.cpp:112`; `runtime/engine/ui/slate_editable_text.cpp:146`; `runtime/engine/ui/slate_renderer.cpp:68`; `runtime/engine/ui/slate_renderer.cpp:83` | Unicode editing, IME, selection and precise nested clipping need acceptance before broad human-facing use | High for inspected primitives; not a claim about every UI path |
| F-014 | Existing automated contracts and manual streaming harness do not prove full editor performance | `extension/src/tests/panels.test.ts:294`; `extension/src/tests/runtime-client.test.ts:386`; `extension/src/tests/viewport-streaming-test.ts:9` | Toolkit selection still needs matched-scene measurement and human workflow acceptance | High: test bodies and boundaries inspected |

## Actual execution path

| Stage | Current implementation | Source |
|---|---|---|
| Extension lifecycle | Lazy `cp.fork`, message correlation, crash reporting and restart | `extension/src/runtime-client.ts:445` |
| Native entry | Worker calls addon `createViewport` | `extension/src/runtime-worker.ts:345` |
| Viewport request | Generic panel requests parent 0, initially 1280 by 720 | `extension/src/panels/viewport-panel.ts:192` |
| Scene document viewport | Custom scene editor also requests parent 0 with pending dimensions | `extension/src/panels/scene-editor-provider.ts:264` |
| Window ownership | Runtime creates HWND; parent branch reparents and shows it; null-parent branch stays hidden | `runtime/engine/app/runtime.cpp:340` |
| Render cadence | Panel requests 60 FPS; dedicated native loop renders, collects encoded output and sleeps remaining budget | `extension/src/panels/viewport-panel.ts:206`; `napi/src/render_thread.cpp:191`; `napi/src/render_thread.cpp:238` |
| Capture | Backbuffer including editor overlays transitions to CopySrc, is captured, then returns to Present | `runtime/engine/app/runtime.cpp:724` |
| CPU handoff | Ready readback is mapped, submitted to asynchronous encoder, unmapped and advanced | `runtime/engine/app/runtime.cpp:1410` |
| Encoder | FFmpeg codec selection, CPU RGBA-to-NV12 conversion and background encoding | `runtime/engine/ipc/h264_encoder.cpp:114`; `runtime/engine/ipc/h264_encoder.cpp:450` |
| N-API delivery | Encoded bytes copied into a vector and delivered by nonblocking thread-safe callback | `napi/src/render_thread.cpp:219` |
| Worker transport | H.264 bytes become base64 and are sent to extension host | `extension/src/runtime-worker.ts:408` |
| Webview transport | Extension posts frame messages | `extension/src/panels/viewport-panel.ts:237` |
| Display | VideoDecoder output is drawn into a 2D canvas and frame is closed | `extension/src/panels/viewport-panel.ts:612` |

The addon remains native code inside the worker process; it is not currently inside the extension-host process. This describes existing code, not a recommendation to preserve that topology in the future desktop editor. The proposed native desktop host should own its in-process runtime and direct viewport presentation.

Historical comments need careful interpretation. The null-parent comment still says PNG streaming, but the inspected active panel uses H.264/WebCodecs. The encoder implementation lists NVENC, AMF, QSV and Media Foundation candidates; the old Media Foundation-only PoC is not the current complete implementation. `docs/architecture.md:82` records the 854 by 480 encoding comparison and `docs/specs/2.0-napi-foundation.md:120` records 120 FPS with 50K spinning cubes. These are historical reported results, not independently reproduced here, and do not establish current full-editor performance at 1080p/4K.

## Mechanisms already implemented

Current source includes bounded queues, deadlines and recovery. They are assets to preserve, not missing features:

- Native callbacks are nonblocking and bounded (`napi/src/render_thread.cpp:83`). Queue-full encoded delivery arms a gate and requests a keyframe (`napi/src/render_thread.cpp:224`).
- Worker stats have a four-message bound; encoded frames have a two-message bound and channel-backlog tracking (`extension/src/runtime-worker.ts:375`, `extension/src/runtime-worker.ts:396`).
- Panel delivery has a two-frame bound. Decoder submissions stop at queue size four and request recovery (`extension/src/panels/viewport-panel.ts:230`, `extension/src/panels/viewport-panel.ts:735`).
- Client permits at most 64 pending requests and applies per-command deadlines (`extension/src/runtime-client.ts:58`, `extension/src/runtime-client.ts:573`).
- Replaced-worker messages and exit events are ignored; pending operations settle on worker failure (`extension/src/runtime-client.ts:455`).
- Input is routed through a native owner queue, with a 256-entry ring and dropped-input counter (`napi/src/render_thread.h:95`). Owner commands are drained in the native render loop (`napi/src/render_thread.cpp:182`).
- Native bindings move command arguments/results into owned storage, rather than retaining the caller's stack across timeouts (`napi/src/napi_common.h:134`).
- Resize is applied on the render thread; stream gates reset around resize (`napi/src/render_thread.cpp:172`). Hidden panels stop their render loops.
- GPU readback is recorded in the render submission. The old post-Present path and double-WaitIdle stall are explicitly described as removed (`runtime/engine/app/runtime.cpp:717`).

These mechanisms do not guarantee responsiveness. A transport queue bound limits backlog, but message-send completion does not mean a frame reached the monitor. Decoder queue length does not include every compositor/display delay. The current `sleep_for` cadence is not a measured frame-latency-waitable presentation policy. CPU/GPU work and long owner commands can still dominate a frame.

One input risk deserves a targeted acceptance test: `RuntimeClient.fireAndForget` drops all input while backlogged (`extension/src/runtime-client.ts:600`), including the categories used for key/button release. The native ring can also reject input. Establish explicit state reconciliation on focus loss and overflow before claiming stuck-key immunity; this inspection did not reproduce a stuck key.

## Quantitative transport model

The following are calculations, not workstation measurements. Assume tightly packed RGBA8, one full frame per tick and decimal MB. Real row pitch/alignment, dropped frames, compression and allocator behavior change traffic.

`RGBA bytes/frame = width * height * 4`

`RGBA payload bytes/second = width * height * 4 * frames/second`

| Resolution and cadence | RGBA payload/frame | One full-frame payload stream |
|---|---:|---:|
| 854 x 480 at 120 FPS | 1,639,680 bytes | 196.76 MB/s |
| 1280 x 720 at 60 FPS | 3,686,400 bytes | 221.18 MB/s |
| 1920 x 1080 at 60 FPS | 8,294,400 bytes | 497.66 MB/s |
| 3840 x 2160 at 60 FPS | 33,177,600 bytes | 1,990.66 MB/s |

A CPU memcpy of that payload reads and writes memory, so its nominal memory traffic is twice the payload, before color conversion or other accesses. Do not add these figures blindly to PCIe throughput: readback placement, GPU architecture and caching matter. A three-slot packed RGBA ring alone would contain approximately 11.06 MB at 720p, 24.88 MB at 1080p or 99.53 MB at 4K; actual allocations include alignment and other buffers.

Packed NV12 is nominally `1.5 * width * height` bytes, subject to even dimensions and stride. Converting to it still reads source pixels and writes another representation. Hardware H.264 encoding does not make the current CPU readback/conversion stages disappear.

For encoded payload of N bytes, base64 is `4 * ceil(N / 3)` characters, plus message framing. If a hypothetical stream were 20 Mbit/s, binary payload would be 2.5 MB/s and base64 about 3.33 million ASCII characters/s. This is an illustration, not a measured active bitrate or a claim that JS always stores each character in two bytes.

Frame budgets are 16.67 ms at 60 Hz, 8.33 ms at 120 Hz and 6.94 ms at 144 Hz. Input-to-display latency includes input delivery, owner scheduling, simulation/render, GPU completion/readback, encoding, transport, decoding, composition and display scanout. Some stages overlap, so adding independent average timings is not a valid measurement of total latency. Timestamp the same frame and input sequence across stages.

At 60 FPS a hypothetical queue containing two full frame intervals corresponds to 33.33 ms of age. That is a warning about accumulated buffering, not a measured delay of the current two-message gate. Bounds at several different stages cannot be interpreted as one fixed latency number.

## Existing editor capabilities and migration boundaries

### VSCode integration

Activation registers the hierarchy and custom scene editor (`extension/src/extension.ts:101`, `extension/src/extension.ts:111`), provides viewport/open-scene commands, routes runtime logs and handles engine mode. The hierarchy provider is explicitly read-only (`extension/src/panels/scene-tree-panel.ts:39`). Scene setup sets content root before initialization and viewport creation (`extension/src/panels/scene-editor-provider.ts:193`).

The runtime client exposes scene loading/saving, entity creation/destruction, component properties, selection, play/stop, screenshot and input operations. These are useful behavioral contracts for a desktop adapter. A method existing does not prove a polished human-facing inspector, document transaction, multi-selection or undo workflow.

Preserve serializable commands and explicit errors across adapters. Separate engine-state ownership from VSCode panel lifecycle. Use engine identifiers with lifetime validation, revisioned snapshots/deltas, transactional mutations and a shared undo model. Do not transplant webview event handlers as the native editor's domain model.

### Native VFX editor

`VfxEditorOverlay` has a composition, emitter tab strip, splitter and specialized widgets (`runtime/engine/vfx/editor/vfx_editor_overlay.h:326`, `runtime/engine/vfx/editor/vfx_editor_overlay.h:430`, `runtime/engine/vfx/editor/vfx_editor_overlay.h:452`). The source tree contains curve, node-graph, parameter-inspector, stack, content-browser, menu, keybinding, save-pipeline and undo components. The VFX application uses the overlay (`runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp:421`).

Composition supports Niagara and horizontal-split layouts (`runtime/engine/vfx/editor/vfx_editor_composition.h:70`). Timeline widget presence must not be presented as an enabled workflow: the default timeline height is zero (`runtime/engine/vfx/editor/vfx_editor_composition.h:88`). This is an existing native editing base, not proof of a general-purpose multi-document desktop shell.

### Slate-style widget framework

`runtime/engine/ui/` implements Widget/Attribute, horizontal/vertical boxes, overlay, button, combo, scroll, editable text, style, focus, tooltip, drag/drop, input routing and draw-element submission. It has focused test files under `runtime/tests/engine/ui/`. Invalidation flags and parent propagation exist (`runtime/engine/ui/slate_widget.h:169`).

The separate `VfxEditorSlate` root is not equivalent to the mature overlay: its viewport and graph are labels, details contain fixed values, curve is a label, and toolbar callbacks select an integer tool index (`runtime/engine/vfx/editor/vfx_editor_slate.cpp:124`, `runtime/engine/vfx/editor/vfx_editor_slate.cpp:133`, `runtime/engine/vfx/editor/vfx_editor_slate.cpp:149`, `runtime/engine/vfx/editor/vfx_editor_slate.cpp:175`).

EditableText currently manipulates bytes and casts input characters to `char`; it is single-line and its header explicitly excludes selection, clipboard, IME and multiline (`runtime/engine/ui/slate_editable_text.h:4`). General code editing belongs in a complete editor such as Monaco, with separate language services, rather than extending this input field into an IDE.

SlateRenderer maintains a CPU scissor stack, rejects fully outside rectangles, but forwards partially overlapping geometry without transmitting the active clip rectangle to TextRenderer (`runtime/engine/ui/slate_renderer.cpp:68`, `runtime/engine/ui/slate_renderer.cpp:83`). Nested pushes also do not visibly intersect parent clips in this function. This is a concrete primitive limitation; it is not evidence that every legacy VFX widget clips incorrectly. Test exact scroll boundaries before selecting it for arbitrary panels.

The framework decision must compare the cost of completing docking, multi-window lifecycle, accessibility, text input, clipboard, IME, precise clipping and browser integration against adopting a desktop toolkit. Preserve specialized native rendering and editing models regardless of the shell choice. The detailed toolkit decision belongs in chapter 04.

## Direct-presentation requirements

A native viewport should have one explicit swapchain/Present owner. Keep render cadence independent from Monaco/DOM painting, AI response rendering and asset indexing. A host-provided viewport target must not automatically activate the readback/encoder path: the current C API initializes capture resources and attaches capture when initialized, so merely passing a parent HWND is not enough to prove capture has been removed.

Use the precise target phrase: **no viewport CPU readback or video encode/decode in the ordinary local editing path**. Do not promise universal zero-copy. Desktop composition and GPU presentation can still involve copies depending on hardware and window configuration. Microsoft documents these distinctions in the [DXGI flip-model guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model).

Keep capture/export/remote preview as explicit capabilities, activated when needed. Validate parent HWND lifetime, DPI mode, resize, minimize, monitor transitions, capture/focus loss and device removal. The current parent branch shows that an embedding route exists; it does not prove a modern docking shell has already passed those tests.

## Test evidence and remaining acceptance

| Existing test source | What it covers | What it does not prove |
|---|---|---|
| `extension/src/tests/panels.test.ts:225` | Panel startup, input forwarding, hide/resume and cleanup with test doubles | Live VSCode compositor behavior or GPU timing |
| `extension/src/tests/panels.test.ts:294` | Two-frame delivery bound and recovery | Input-to-photon latency |
| `extension/src/tests/panels.test.ts:317` | Resize/extradata protocol | DPI correctness during real monitor transitions |
| `extension/src/tests/panels.test.ts:403` | Exact base64 decoding bytes and decoder contracts | Hardware decoder throughput or rendered image fidelity |
| `extension/src/tests/runtime-client.test.ts:287` | Replaced worker lifecycle isolation | Recovery from every native driver failure |
| `extension/src/tests/runtime-client.test.ts:386` | Request bound/backpressure | Human responsiveness under a complex scene |
| `extension/src/tests/runtime-worker.test.ts:381` | Shutdown operation order | GPU resource lifetime under real docking stress |
| `runtime/tests/engine/ui/test_slate_*.cpp` | Widget/layout/input/style/invalidation contracts | Full desktop accessibility and production authoring UX |
| `extension/src/tests/viewport-streaming-test.ts:9` | Manual addon NAL delivery and aggregate stats | End-to-end displayed FPS, percentiles and input-to-photon |

The manual streaming harness sums render stats but divides by encoded-frame count when calculating average frame time; those callback counts can differ. Its memory helper samples process memory, not a complete sum of extension host, worker, Chromium, GPU allocations and VRAM. Treat these as diagnostic helpers, not an acceptance dashboard. The harness is explicitly manual; file presence is not execution evidence.

The underlying runtime statistic also needs care: `runtime/engine/app/runtime.cpp:693` computes inter-frame `dt`, replaces values above 0.1 seconds with 0.016 seconds at `:696`, then derives `stats.frame_ms` and FPS from that value at `:812`. This is neither an isolated GPU duration nor an unmodified hitch distribution. Using it for p99 or stall acceptance would hide long intervals. Capture uncapped monotonic timestamps and separate CPU/GPU/presentation measurements; preserve the simulation clamp independently.

No performance acceptance is marked complete by this document. Existing tests were inspected, not rerun. Historical closure reports may describe earlier builds and fixed defects; correlate their commit/build and configuration before carrying numbers forward.

## Performance validation matrix

Run matched scenes on the same workstation, driver, build configuration, camera path, cvars, physical viewport dimensions and presentation settings. Record both warm and cold behavior. Build a direct-presentation baseline before investing in full UI migration.

| Scenario | Required observations | Acceptance purpose |
|---|---|---|
| Standalone runtime versus current VSCode stream | CPU/GPU frame time, render FPS, delivered/decoded/displayed frames, process memory, VRAM | Attribute shell/transport overhead without changing scene quality |
| Minimal native viewport host | Same scene and resolution, PresentMon/ETW, GPU timestamps, frame age | Establish whether removing transport addresses reported latency |
| Native host with Monaco idle and active | Typing latency, large-file open, semantic diagnostics, render p95/p99 | Verify code services cannot stall render/input |
| Continuous AI output | UI queue age, text layout cost, cancellation, CPU use, render pacing | Keep chat/agent progress out of critical frame work |
| Asset import and thumbnail generation | Job concurrency, GPU contention, memory peaks, editor event latency | Bound background work and preserve interaction |
| Hierarchy/property stress | Large scene, search/filter, multi-selection, transaction duration | Validate model scaling and meaningful feedback |
| Rapid resize/dock/undock | Device/resource lifetime, resize frequency, hitch distribution, image correctness | Prevent expensive recreate storms and stale surfaces |
| Two monitors with different DPI/refresh | Physical pixel size, cursor/picking alignment, focus, swapchain transitions | Validate actual human desktop use |
| Hidden/minimized editor | CPU/GPU idle use, restart latency, input-state reset | Avoid unnecessary rendering without broken resume |
| Input congestion/focus loss | Press/release ordering, stuck-key checks, overflow counters | Verify resilient navigation under load |
| Worker/browser/native failure | Document preservation, pending-request settlement, restart behavior | Make failure recoverable and visible |
| Long editing session | Memory slope, handle counts, VRAM, model/listener disposal, file handles | Detect accumulation beyond a short smoke test |

Record p50, p95 and p99, not only average FPS. Add correlated timestamps for input receipt, owner execution, frame submission, GPU completion, capture, encode, transport, decode and presentation where applicable. A screen recording can expose glitches; it is not a substitute for timestamped latency or a photodiode/high-speed-camera input-to-photon experiment when that precision is needed.

Suggested decision gates are proposals, not measured results: no ordinary local viewport encode/readback; bounded queues with visible counters; no lost input state after congestion/focus transitions; stable memory after repeated document open/close; and a measured p95/p99 overhead budget relative to the standalone baseline that the team agrees before choosing the shell. Report absolute frame times alongside relative overhead, since a percentage can hide an already slow renderer.

## Conclusion for the GUI decision

The strongest current evidence favors testing direct native presentation before blaming or replacing every editor layer. The project already owns enough native rendering and specialized VFX editing to make that test meaningful. The missing evidence is not another mockup: it is a matched workload demonstrating responsive input, predictable presentation and reliable document workflows with Monaco, AI output and asset jobs active. The final shell choice should follow that result and the cost of completing human-facing desktop capabilities.
