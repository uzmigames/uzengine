# Visual inventory and provenance

Visual revision 5, 2026-10-23. Eleven document/panel proposals plus one architecture diagram use 1920 x 1200 compositions, document-driven filename tabs, neutral gray/black shell surfaces (`R=G=B`) and Lucide 0.468.0 icons. Semantic colors and source imagery remain colored. The footer is `DESIGN STUDY 05`; manifest revision is 5. These are static GUI proposals around real sample runtime captures, not an implemented editor or measured host-performance result.

The original Uzmi mark comes from `icon.ico`; its orange is preserved. `sources/uzmi-icon.png` is the lossless RGBA extraction used by the report. Source/output hashes and dimensions are recorded in [manifest.json](manifest.json).

| File | Purpose | Visual source |
|---|---|---|
| 00-architecture.png | Host/service boundaries | Proposed architecture diagram |
| 01-scene-workspace.png | Scene document and contextual tools | Sponza sample runtime capture |
| 02-code-workspace.png | Code document and scoped agent dock | Independent OrbitPreview Lua example; services unconfigured |
| 03-agent-review.png | Diff and revision/permission gate | Illustrative proposal, not applied |
| 04-asset-lab.png | Asset dependencies and import recipe | DamagedHelmet sample runtime preview and actual source maps |
| 05-model-generation.png | Offline request and reference inspection | Four runtime camera views of one DamagedHelmet; zero remote candidates |
| 06-paint-roundtrip.png | Texture/channel recipe and external-tool dock | DamagedHelmet runtime image/maps; no paint session |
| 07-vfx-workspace.png | Effect document and graph | Synthetic particle illustration, not runtime evidence |
| 08-diagnostics-jobs.png | Capture/run inspection and jobs | Sample runtime captures and retained run records |
| 09-material-graph.png | Material document and compile controls | DamagedHelmet/maps; graph illustrative and not compiled |
| 10-control-rig.png | MushroomMonster.uzrig draft | Character sample runtime image; no rig solver evaluation |
| 11-animation-editor.png | Mushroom_Idle.uzanim draft document and timeline | Runtime AS_Mushroom_Idle.uzanim preview; displayed keys/curves not extracted |
| contact-sheet.png | Overview | Downscaled report compositions |

## Published runtime sources

Seven runs supply eight published frames. Runs used the pre-existing binary, DX12 and 1920 x 1080 output. Requested frame budgets are listed below; manifests also record preload/accounting details. No new build or native unit suite was run for this visual revision.

| Run | Sample and capture | Requested budget | Exit |
|---|---|---|---|
| Sponza | Sample `sponza.uzproject`, camera A, `sponza_680.png`; `r.TechPanel.Show=0` | 700 frames | 0 |
| Materials | Materials sample, `materials_240.png` | 250 frames | 0 |
| DamagedHelmet quarter | `damaged_helmet.uzproject`, `damaged-quarter_240.png` | 250 frames | 0 |
| DamagedHelmet front | Same sample, `damaged-front_240.png` | 250 frames | 0 |
| DamagedHelmet side | Same sample, `damaged-side_240.png` | 250 frames | 0 |
| DamagedHelmet back | Same sample, `damaged-back_240.png` | 250 frames | 0 |
| Character | `sample/projects/fbx_test.uzproject`, `character_120.png` and `character_240.png` | 250 frames | 1 |

Retained files are under `sources/`, with corresponding `*-manifest.json` and `*-run.log` records. These records identify exact project paths, CLI arguments, readiness and frame accounting. The runtime binary SHA-256 remains `2C866ADB624599DE524B14FD382C7A308A1F07BC87AAE02202F97B01CADCBD40`; captures do not certify concurrent source changes absent a rebuild.

DamagedHelmet asset and lighting were not changed. Only CLI camera views vary, all targeting `(0, 0, 105)`: quarter `(260, -273, 165)`, front `(0, -377, 165)`, side `(377, 0, 165)` and back `(0, 377, 165)`. This is the DamagedHelmet shown in the user reference, not the different SciFiHelmet asset used in rejected proposals. Earlier SciFiHelmet-derived captures and offline mannequin previews are not final revision-5 visual sources.

The character run produced both images but failed shutdown: D3D12MA reported three remaining allocations totaling 6,881,280 bytes. No engine fix was made. Image production and successful process cleanup are separate outcomes; this run is not a passing acceptance gate. Preserve all log warnings and inspect the relevant run record rather than carrying warnings from superseded captures into a new run. Manifest timing samples are diagnostic records, not a matched standalone/VSCode/native-host benchmark or Falcor comparison.

## Asset and authoring boundaries

DamagedHelmet uses `DamagedHelmetHQ.uzmesh` and four maps: `DH_HQ_BaseColor.jpg`, `DH_HQ_Normal.jpg`, `DH_HQ_ORM.png`, `DH_HQ_Emissive.jpg`. ORM packs R=occlusion, G=roughness, B=metallic; the sample scene emissive setting is 5. Preview images are real runtime rendering of the unchanged sample, while the material graph and import/export recipe controls remain proposals. The graph was not compiled to produce those images. Asset license/redistribution clearance is not established by local availability.

Assets, Generate, Paint and Material reuse one helmet for context continuity. Four camera views are not four generated candidates. No Meshy submission, upload or EZPainter operation occurred. Paint layers remain disconnected; source maps are not newly exported textures or an implemented UV editor.

The character sample supplies a 22-bone skeleton and a four-second, 30-fps source clip at `animations/AS_Mushroom_Idle.uzanim`, played through runtime controller alias `Mushroom_Idle`. The UI document `Mushroom_Idle.uzanim` is a draft referencing that source, not the source filename. Displayed timeline keys, curves and authoring markers are illustrative, not extracted clip data or proof of editing/baking. `MushroomMonster.uzrig` is a proposed rig document, not an executed IK/FK solver. Runtime animation preview does not validate the proposed rig graph or editor UI.

VFX remains a synthetic illustration. An attempted sample capture was not accepted for publication and supplies no positive rendering evidence here. Code, agent diff, permissions and job controls are illustrative unless explicitly linked to retained run evidence.

Navigation follows documents: opening a file selects its editor/layout. Agents, asset browsing, generation, jobs and Paint are contextual docks/actions. Scene Visual/Linked code requires an explicit source association, not generated C++ or a mandatory C++ counterpart.

## Reproduction and review

From the repository root, with the generator's Python dependencies installed:

~~~powershell
python -B docs/analysis/GUI/tools/render_visuals.py
python -B docs/analysis/GUI/tools/build_report.py
python -B docs/analysis/GUI/tools/validate_report.py
~~~

Composition consumes retained runtime captures and local map sources. It does not run an engine, provider or rig solver. Use the current generator and manifest for exact resources; old CPU helmet/mannequin generation paths are not the revision-5 reproduction contract. Report tooling paths do not belong in path-agnostic runtime code.

Static checks cover links, image dimensions/hashes, framing and obvious overlap. They do not validate docking, IME, screen readers, focus, language services or native presentation performance. The HTML report is a reading aid, not an interactive implementation.
