# 04. Proposed native desktop architecture

Status: proposed, not implemented or benchmarked. Date: 2026-10-22.
Read [current transport evidence](03-editor-and-performance.md) before treating this recommendation as an explanation of measured performance.

## Recommendation and decision boundary

Navigation is document-driven. There are no global Scene/Code/Assets/VFX mode tabs. Opening a `.uzscene`, C++/Python/Lua source file, material, rig or animation document selects its registered editor and contextual layout. Main tabs show file/document names, with identity and dirty state; editor type is context, not a separate global mode. Agent review, jobs, Meshy generation, asset browsing and Paint integration live in docks or contextual actions. Existing references to workspaces describe layouts and project scope, not global navigation categories.

A scene may expose Visual and Linked code views when an explicit source association exists. Linked code opens the associated source document with its own revision and save lifecycle. It does not generate C++ from a scene, imply every scene has C++, or treat scene serialization as executable source. Missing/ambiguous associations need explicit selection, not an invented target.

Build a Windows-first editor application using C++ and Qt 6 Widgets: `QMainWindow` for application layout, `QDockWidget` for tool panels, and a dedicated native child HWND for the existing DX12 renderer. Host Monaco in WebView2 only when a code workspace is opened. Keep the primary scene image out of browser transport. This is a proposed starting architecture, conditional on the prototype gates below, not an approved dependency or a claim that Qt is intrinsically faster than every alternative.

The editor should resemble Unreal in coherent workspaces, direct manipulation, dockable tools, contextual inspectors and diagnostic depth. It should achieve elegance through consistent density, restrained chrome, typography, predictable focus and progressive disclosure. Copying Unreal's internal Slate implementation is not necessary to deliver those qualities.

The first prototype must contain exactly one runtime viewport, a native hierarchy/inspector and one lazy code pane. It must measure the same scene used in the current VSCode baseline. A full visual redesign before that gate would leave the central performance assumption unresolved.

## F-015. Adopt mature desktop infrastructure while retaining existing engine investment

**Observed:** The native VFX overlay has composition, graphs, parameter widgets and undo behavior; the separate Slate framework has basic widgets and significant text/clipping limitations. See `runtime/engine/vfx/editor/vfx_editor_overlay.h:430`, `runtime/engine/vfx/editor/vfx_editor_composition.h:70`, `runtime/engine/ui/slate_editable_text.h:4` and `runtime/engine/ui/slate_renderer.cpp:68`.

**Implication:** Rebuilding every domain tool discards useful work. Expanding current Slate into a full desktop platform adds docking, Unicode editing, accessibility, clipboard, IME, multiple windows and browser integration to the critical path.

**Proposal:** Adopt Qt Widgets for shell infrastructure, retain renderer/editor domain models and specialized native overlays, and adapt commands and selection incrementally. Keep legacy VFX widgets usable during migration. A reusable curve or graph model does not automatically make its current drawing/input backend embeddable in a Qt widget; budget that adapter explicitly.

**Confidence:** High for the source distinction; medium for toolkit suitability until integration acceptance. This recommendation evaluates delivery risk, not benchmark rank.

## F-016. Direct presentation requires explicit ownership and capture policy

**Observed:** A parent-HWND route exists at `runtime/engine/app/runtime.cpp:340`; capture is attached to the frame path at `:724`, with readback/encoding at `:1410`. Passing a parent does not itself prove these stages are disabled.

**Implication:** A native shell can still retain unnecessary capture cost if it reuses current initialization without separating local display from streaming. Competing Present, resize or teardown owners can also introduce stalls and lifetime errors.

**Proposal:** One render owner controls one swapchain for the first viewport. Make capture/export/remote streaming explicit opt-in capabilities. Require no viewport CPU readback or video encode/decode during ordinary local editing. Do not describe this as universal zero-copy; desktop composition and GPU copies remain configuration-dependent.

**Confidence:** High for the existing path and ownership requirement; actual benefit remains unmeasured.

## F-017. New host must preserve asynchronous ownership and acknowledge lost crash isolation

**Observed:** The extension forks a runtime worker at `extension/src/runtime-client.ts:445`. Native owner commands, bounded input and asynchronous delivery already exist in `napi/src/render_thread.cpp:182`, `napi/src/render_thread.h:95` and `napi/src/napi_common.h:134`.

**Implication:** Loading the engine into the desktop process removes the current worker's native-crash boundary. A dedicated render thread does not isolate access violations or process termination. Direct calls from widgets into mutable runtime state would also regress existing ownership safeguards.

**Proposal:** Retain typed asynchronous commands and bounded snapshots in a host-neutral service. Use in-process rendering for the first direct-presentation proof, with document recovery outside volatile runtime state. Keep provider processes and language servers isolated. Treat a separate renderer process with GPU sharing as a later, separately designed option requiring its own synchronization, adapter, device-loss, security and recovery contracts.

**Confidence:** High for process topology and tradeoff. No cross-process GPU-sharing implementation is claimed.

Process isolation limits ordinary process-crash propagation; it does not guarantee isolation from GPU device removal, driver resets or exhaustion of shared machine resources. Recovery must distinguish renderer-process failure from a device failure affecting more than one process.

## F-018. Toolkit integration, accessibility and distribution are release gates

**Observed:** Existing UI source and tests do not establish mature desktop text entry, accessibility or mixed-DPI behavior; chapter 03 identifies these limits. No proposed Qt/WebView2 editor build was executed for this report.

**Implication:** Native presentation alone cannot establish a usable workstation application. Browser hosting, docking, packaging, license compliance and updates can become product blockers even when a scene renders smoothly.

**Proposal:** Gate adoption on real HWND docking, screen-reader navigation, IME, mixed-DPI monitors, runtime deployment and browser-failure recovery. Select exact Qt modules/license terms and a supported compiler/runtime combination before implementation scope is approved.

**Confidence:** High that acceptance is missing; no claim that a particular toolkit fails these tests.

## Qualitative decision matrix

The weights express proposed product priorities, not measured timings. No synthetic total is presented: these alternatives have not been built against the same workload. Revisit weights with the owner before committing to a framework.

| Criterion | Suggested weight | Meaning |
|---|---:|---|
| Direct renderer integration and ownership | 30% | Own HWND, swapchain policy, predictable frame and input boundaries |
| Desktop authoring completeness | 25% | Docking, text, model/view controls, multiple windows, accessibility |
| Delivery and maintenance risk | 20% | Existing expertise, build integration, long-term surface area |
| Code and external-tool integration | 15% | Browser editor, process supervision, typed adapters |
| Distribution and future portability | 10% | Runtime dependencies, licenses, updates, platform strategy |

| Candidate | Best fit | Main cost or uncertainty | Recommendation |
|---|---|---|---|
| Qt 6 Widgets + native viewport + WebView2 | Mature desktop shell with direct Windows rendering and optional browser tools | HWND airspace, Qt toolchain/deployment, WebView2 lifecycle, licenses | Preferred prototype |
| Extend own Slate framework | Full visual and render-loop ownership; shared engine primitives | Large desktop-platform backlog beyond existing VFX tools | Retain domain primitives; do not make full shell completion first dependency |
| Dear ImGui | Diagnostics, internal tools and GPU-oriented inspectors | Product docking branch choice, text/IME and accessibility validation, document conventions | Strong diagnostic layer; compare for focused specialist tools |
| Qt Quick / QQuickRenderControl | Highly customized animated UI with application-managed scene-graph rendering | Graphics-device/resource synchronization, render-loop ownership and composition complexity | Separate experiment if QWidget product limits become concrete |
| Qt Widgets + QQuickWidget | Select QML islands in a Widgets shell | Extra render pass and disabled threaded Qt Quick render loop are documented tradeoffs | Avoid making this default for the primary viewport |
| WinUI 3 | Windows UI conventions and accessibility integration | Docking system, C++/WinRT interop, XAML/native composition and packaging design | Credible Windows alternative; prototype if team expertise favors it |
| Avalonia | Cross-platform managed desktop productivity | Managed/native boundary, renderer hosting and platform-specific behavior | Consider only with an explicit managed-shell ownership decision |
| Electron | Existing TypeScript/UI ecosystem and process tooling | Chromium/Node lifecycle and custom native surface composition; streamed viewport risk remains if repeated | Useful companion/remote UI; not first local-render prototype |
| Tauri | Web UI with smaller bundled browser responsibility on supported platforms | System webview differences and custom native-child integration still need design | Not a performance proof merely because shell is smaller |
| CEF custom shell | Extensive browser embedding control | Chromium distribution/update responsibility; docking and desktop UX still application work | Higher infrastructure cost than needed for initial Windows scope |

`QQuickRenderControl` is not equivalent to `QQuickWidget`: it gives an application control over Qt Quick rendering and requires deliberate integration. Its existence does not mean UzEngine can reuse a DX12 device or composition path without work. Likewise, a native host can coexist with Electron/Tauri; the critical question is where the viewport is presented and who owns its lifecycle.

## Application and runtime boundaries

| Layer | Owns | Must not own |
|---|---|---|
| Desktop shell | Windows, docking, focus, menus, shortcuts, accessibility, workspace layout | Scene mutation through arbitrary widget callbacks |
| Editor application services | Documents, project/session identity, command transactions, undo, asset catalog, selection, jobs | GPU resource access from provider or UI callbacks |
| Runtime adapter | Versioned commands, snapshots, capability discovery, viewport lifecycle handoff | Qt widgets, browser DOM objects or vendor authentication |
| Runtime/render owner | Engine state, render scheduling, resource lifetime, presentation | Monaco, Claude/Codex SDKs, cloud credentials or project-specific paths |
| Tool processes | LSP, debugger adapters, agents, DCC automation, optional import workers | Unvalidated direct mutation of the live world |

Keep Qt and WebView2 dependencies in the editor application target, not the runtime library. All filesystem locations must arrive through CLI/project configuration, asset manifests or explicit application APIs. Executable-relative discovery uses the existing `uz::GetExeDir()` convention. No fixed project content paths, provider configuration or development-machine paths belong in `runtime/`.

The current C API is a migration starting point, not a complete multi-session editor contract. Do not advertise multiple independent views merely because the shell can create multiple dock panels. Per-view cameras, temporal history, picking, size, render targets and lifecycle must be explicit before adding a second live viewport.

## HWND, composition and rendering contract

Create and destroy the host window on the GUI thread; assign renderer operations to the documented render owner. Native embedding requires a reviewed ownership agreement: which side creates the child HWND, who reparents it, who destroys it, and when GPU work is retired. A wrapper such as `QWindow::fromWinId` does not grant permission for two systems to destroy the same window.

Qt's `createWindowContainer` documents native child-window stacking and focus limitations. A native viewport behaves as a distinct window surface; ordinary QWidget/browser content cannot be assumed to blend above it. Place application toolbars and inspectors beside its bounds. Render gizmos and in-scene overlays inside the engine pass. Validate native popup/menu behavior explicitly rather than depending on browser CSS z-index across HWND boundaries.

Dock/undock changes parentage and sometimes top-level ownership. Suspend presentation as needed, coalesce resize requests, apply the newest physical-pixel extent on the render owner, and resume only after the surface is valid. Zero-area/minimized windows must not allocate zero-sized targets or spin at full frame rate. Avoid recreating GPU resources for every intermediate resize event.

Use per-monitor DPI awareness consistently. Qt logical coordinates, Win32 physical pixels, WebView2 bounds and scene picking must agree through explicit conversions. Moving between monitors must update scale, framebuffer extent, pointer mapping and text metrics without accumulating rounding errors. Test negative desktop coordinates and monitor removal.

Choose flip-model presentation where the renderer/backend supports it and validate synchronization, tearing, refresh rate and frame latency against the baseline. DXGI policy belongs with the swapchain owner; the GUI timer must not issue a competing Present. UI responsiveness and rendering are separate metrics, both required.

## Threading, commands and state publication

WebView2 requires its owning STA thread and message pump; use the GUI thread for its controller and callbacks. Do not access WebView2 COM objects from the render thread or block its message pump awaiting a render result. Return results asynchronously to the owning thread. Avoid nested modal loops inside WebView2 callbacks.

Proposed commands carry request ID, project/session ID, target identity, expected revision and typed payload. Validate at the application boundary and apply at the runtime owner's safe mutation point. Return a result with new revision or a specific conflict/error. Preserve existing bounded-command concepts; do not serialize entire scene JSON every render tick.

Publish immutable snapshots or revisioned deltas for hierarchy, selection, properties and diagnostics. Slow panels consume the latest valid state, not an unbounded history. Coalesce superseded pointer-move/resize/preview events; preserve transactional edits and key/button release semantics. Queue overflow must be observable and recover input state on focus loss.

Undo belongs to document/application transactions, not to whichever panel initiated a change. A drag may update preview repeatedly but commit one undoable edit. Agent edits, property inspectors and keyboard commands must use the same validation and transaction service. Existing VFX undo can remain scoped to its document until a deliberate cross-document transaction contract exists.

Shutdown stops new work, settles pending commands, detaches observers, stops tool processes as appropriate, retires GPU work, destroys renderer resources and finally releases host surfaces. Browser crash, runtime failure and external-agent failure need different recovery paths. Autosaved documents must not depend on a successful GPU teardown.

## Native, web and GPU surface allocation

| Human surface | Preferred implementation | Reason |
|---|---|---|
| Main menu, file/document tabs, docking and command palette | Native shell | Consistent focus, window behavior, keyboard navigation |
| Hierarchy, inspector, job queue, asset metadata | Native model/view controls | Virtualized large lists, shared selection, accessibility |
| Scene, mesh/material preview and picking | Engine GPU surface | Direct presentation and engine-consistent appearance |
| Gizmos, selection outline, camera guides, render diagnostics | Engine overlay | Correct depth and viewport-coordinate ownership |
| Code and text diff | Monaco/WebView2 | Mature editor features with separate language services |
| Agent transcript and formatted tool results | Native rich text or bounded web panel | Choose from actual formatting needs; never drive render cadence |
| VFX graph/curves | Retained domain models with existing native rendering or reviewed adapter | Preserve working authoring behavior and undo |
| Paint and UV tools | Existing tool integration or dedicated GPU tool after audit | Do not invent EZPainter capabilities from mockups |

Lazy browser creation saves startup work only if measured. Reusing a controlled browser environment can reduce duplicate state, but every controller/model still needs disposal. Hiding a panel must pause unnecessary work without losing unsaved documents. Do not create one heavyweight process per tiny tool by default.

## Packaging, licensing and operational risks

Qt integration requires a pinned module/version set, supported compiler ABI, deployment plugins and a repeatable editor build. Current Zig C++23 runtime builds do not automatically establish Qt binary compatibility. Validate runtime-library, exception/RTTI, allocator and DLL-boundary assumptions; prefer explicit ownership at ABI boundaries.

If the Qt host and runtime use different compiler/runtime configurations, prefer a versioned C ABI with fixed-layout values, opaque handles and explicit release functions. Do not pass Qt types, standard-library containers, exceptions or cross-module allocation ownership through that boundary. A C ABI still requires matching architecture, calling convention, layout and lifetime rules; it is not automatic binary compatibility. Building a Qt host with its supported toolchain while preserving the Zig runtime build is an option to validate, not an already working configuration.

Review commercial versus open-source Qt terms for the selected modules and distribution model. LGPL/GPL obligations differ by module and linkage; do not assume all Qt add-ons share one permissive license. Track notices and corresponding-source/relink obligations where applicable. This report does not select a legal distribution model.

Choose WebView2 Evergreen or Fixed Version intentionally. Evergreen shifts browser patch delivery to the runtime update channel; Fixed Version needs application-owned servicing and increases package responsibility. Test clean-machine installation, offline startup, proxy behavior, runtime absence and rollback. Pin Monaco assets separately from browser runtime.

Accessibility acceptance includes UI Automation exposure, keyboard-only docking, screen-reader labels, high contrast, scalable text, visible focus and reduced motion. Custom graph/canvas tools need a semantic alternate representation for essential selection and property operations. Platform widgets help but do not certify the finished application.

## Prototype acceptance and stop conditions

1. Load the same project and camera path as standalone/current extension; record build, GPU, driver, physical resolution and presentation policy.
2. Verify direct local display has capture/readback/encode disabled; collect PresentMon/ETW and GPU timing evidence without changing render quality.
3. Open Monaco and exercise typing, diagnostics and continuous agent output while recording UI latency and render p50/p95/p99.
4. Dock, undock, resize, minimize and cross mixed-DPI monitors; verify picking, focus, input release and memory/resource stability.
5. Inject browser/tool failure and recover documents; explicitly record that an in-process native crash terminates the host.
6. Exercise a large hierarchy and import load with bounded jobs; verify no synchronous external work blocks frame or UI ownership.
7. Validate keyboard/IME/accessibility and clean-machine deployment before expanding the shell into all workspaces.

Choose quantitative overhead and interaction budgets before measurements, using the baseline and target workstation class. A failed prototype calls for root-cause evidence and an alternative host/composition experiment; it does not justify claiming the whole engine must be rewritten. No gate above was executed in this documentation task.

## Primary references

- [Qt QWidget and createWindowContainer](https://doc.qt.io/qt-6/qwidget.html#createWindowContainer): native container ownership, stacking and focus caveats.
- [Qt QDockWidget](https://doc.qt.io/qt-6/qdockwidget.html): docking primitives and lifecycle surface.
- [Qt QQuickWidget](https://doc.qt.io/qt-6/qquickwidget.html): documented rendering overhead and threading tradeoffs.
- [Qt QQuickRenderControl](https://doc.qt.io/qt-6/qquickrendercontrol.html): application-controlled Qt Quick rendering.
- [WebView2 threading model](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model): STA, callback and message-pump constraints.
- [DXGI flip-model guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model): presentation and composition behavior.
- [Qt licensing](https://www.qt.io/licensing/): distribution decision input; verify selected module terms.
- [WebView2 distribution](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution): runtime deployment choices.

Related chapters: [code workspace](05-code-workspace.md), [agent and asset workflows](06-agents-and-asset-workflows.md).
