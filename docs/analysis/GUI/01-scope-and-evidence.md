# 01. Scope and evidence

Survey date: 2026-10-22. Scope: current UzEngine capabilities, remaining work,
and a high-performance desktop editor integrating code, agents, model generation,
and painting workflows.

Inspected HEAD: `259ae89623473fd5c1694e2e5a4a77d1c7597182`. Existing math changes
and reference-document moves make this a working-tree survey, not a clean
commit-pinned certification. They are outside this documentation change.

Additional Slang third-party and agent-memory changes were visible during final
review. They were not made or audited by this report task. The report is not a
certification of that concurrent dependency update or its resulting builds.

## Evidence policy

Implementation, runtime integration, editor usability, persistence, and tests are
separate dimensions. A source file, checked task, and working workflow are not
interchangeable evidence.

| Label | Meaning |
|---|---|
| Observed | Source or artifact inspected during this survey. |
| Recorded | Result from an existing report; not rerun here. |
| Partial | Some code exists, but complete workflow or acceptance is missing. |
| Planned | Specification or task describes intended work. |
| Unverified | Insufficient evidence; does not mean absent. |
| Proposed | Recommendation, not a shipped capability. |

The actual layout is `runtime/engine`, `runtime/editor`, `extension`, `napi`,
`mcp`, `scripts`, and `tools`. Historical references to `src/engine` do not
describe the current tree.

## F-001. Task counts are not product-completion metrics

- Evidence: `.rulebook/PRIORITIES.md:9` distinguishes checklist status from
  verification; its reconciliation section identifies overlapping scene, fog,
  material, animation, and render-bridge tasks. The later stabilization ledger
  records fixes that older audit prose still presents as missing.
- Impact: checklist counting can classify existing code as absent or overstate
  readiness. Old findings must be reconciled with current source and later gates.
- Confidence: high for metadata drift; individual capabilities still need evidence.
- Action: reconcile residual scope before scheduling duplicate implementations.
  This report proposes work but does not reorder the active Rulebook queue.

## Verification limits

No new engine build, full suite, live-editor benchmark, GPU debugger capture or
matched Falcor matrix was executed. Visual revision 5 (2026-10-23) uses seven sample runs and eight published frames from the pre-existing binary: Sponza, Materials, four DamagedHelmet cameras and character frames 120/240. Six runs exited 0; character images were produced but shutdown exited 1 with three D3D12MA allocations totaling 6,881,280 bytes. No engine fix was made. See [capture provenance](images/README.md). These are image-production runs, not matched acceptance; historical test results retain their original scope.

Sandboxed execution was unavailable; terminal reads outside the sandbox restored
access. An earlier browser-based file-reading attempt was abandoned after
inconsistent TypeScript reads. Those inconsistent results are excluded. Source
conclusions use direct terminal inspection.

Integration research uses official documentation. Assistant-session tools for
Blender, Substance Painter, Claude Code, or Codex do not prove these integrations
ship with UzEngine. Capabilities of external projects remain unverified unless
local code or an explicit contract supplies evidence.

## Visual provenance

Screen images depict proposed workflows, not a working native editor. Example
jobs, modifications, assets, and telemetry are illustrative. Actual renderer
captures used as source material are identified in the image inventory.

Documentation is in English under the repository rule. The requested directory
is preserved exactly as `docs/analysis/GUI`.

The visual inventory is `images/README.md`; `images/manifest.json` records local
source/output hashes and dimensions. Eleven document/panel proposals and one architecture diagram are composed offline around real sample runtime imagery. DamagedHelmet appears across related views for continuity, not as generated candidates. Mushroom_Idle runtime preview is real, but rig graphs and timeline keys are draft authoring UI. VFX remains a synthetic illustration; details and failed-run evidence are centralized in the image inventory.

Document checks cover chapter links, finding/gap identifiers and image artifacts.
Static image inspection does not establish interactive desktop accessibility,
docking, focus/IME or performance. The offline HTML report is generated from the
Markdown chapters and does not implement the proposed editor.
