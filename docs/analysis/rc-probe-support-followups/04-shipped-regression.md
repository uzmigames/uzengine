# Item 1.1: shipped regression measurement

2026-10-13. PASS, process exit 0. Twenty-four sequential reference captures
(two rules, six cameras, two repeats), four S2 captures and two S3 captures.

Command from repository root:

```bash
python scripts/rendering/test_probe_shipped.py --out .screenshots/rc-probe-support-followups/item11
```

Report: `.screenshots/rc-probe-support-followups/item11/probe_shipped.json`.
Every capture's manifest, log and stamped region dumps remain beside it.
Executable identity inherited from each manifest:
`runtime/build/bin/uzengine.exe`, `fnv1a64:534537d32570707f`.
Reference render/output 640x360; Sponza 1920x1080. Reference frames 200-201;
S2 frames 200-202 (full-frame image at 200); S3 frames 90-290.

The existing closing sweep now invokes this harness as `probe_shipped`,
writing to `sweep-b/test-probe-shipped`. `bash -n scripts/rendering/sweep_block_b.sh`
and Python syntax checking passed. The entire closing sweep was not rerun:
the delegated scope is this measurement and its integration, not other GPU studies.

## Registered reference gates

R3r, P1, P3, C and the N1 negative control all pass.

| Camera | Valid origins | In true component | Relocated |
|---|---:|---:|---:|
| SOLID/EXT | 3928 | 100% | 102 |
| SOLID/CORE | 16 | 100% | 12 |
| SOLID/NEAR_OUT | 16 | 100% | 12 |
| SOLID/NEAR_IN | 20 | 100% | 14 |
| SHELL/INT_E | 22 | 100% | 18 |
| SHELL/INT_W | 22 | 100% | 18 |

P1 NEAR_OUT lit-face mean / Lref = **1.0215496471**; unsupported = **0%**.
Shipped Lref = 0.3273041255014968, control Lref = 0.31069930023177317.
N1 rule-0 NEAR_OUT unsupported = **100%**, so the defect is exercised.
Every one of the 42 rule/label repeat means has zero spread.

| Sealed label | Rule-0 mean | Shipped mean |
|---|---:|---:|
| CORE:dark_all | 0.0003165523276955764 | 7.181925364977111e-7 |
| NEAR_OUT:dark_all | 0.00032553432739191977 | 5.95278052320765e-7 |
| NEAR_IN:dark_all | 0.004314546580431318 | 4.0295867611527633e-7 |
| SHELL:all_e | 0.1766962632109648 | 5.030745828656131e-8 |
| SHELL:all_w | 0.14045638376163413 | 4.99100456737429e-8 |

P3 tolerance = 0.0003106993002317732 for each label. These are small nonzero
values, not the historical exact-zero result.

P2 against rule 0 remains reported, not an oracle verdict:
CORE ratio **1.0536989088**, NEAR_IN **1.0531877743**. Both exceed the old 5%
band. The control-premise issue remains; this harness does not relabel them pass.

## Sponza

S2 is reported, as registered; it is not a new pass/fail threshold.

| ROI | Rule-0 diffuse mean | Shipped diffuse mean | Shipped unsupported |
|---|---:|---:|---:|
| shadow_wall | 0.0006690146629088163 | 0.0006697369416963193 | 0.10986328125% |
| lit_wall | 0.00034427070850724764 | 0.00034631990779240813 | 0% |
| centre | 0.0006495386832505889 | 0.0006504621139968013 | 0% |

`item11/sponza/S2_cameraC_project.jpg` was inspected: both views are populated,
correctly framed and nearly identical; the difference panel contains localized
red/blue changes rather than a blank source view. This is qualitative review,
not a pixel-equality claim.

S3 passes on all four registered ROIs. Both rules log recentres **113,157,201**.

| ROI | Rule-0 p99 step | Shipped p99 step | Rule-0 max recentre step | Shipped max recentre step |
|---|---:|---:|---:|---:|
| rear_door | 0.008131194417222116 | 0.008162297167136952 | 0.000018920759071058574 | 0.00001988378965192268 |
| right_shadow | 0.07742875387355863 | 0.07742311441068449 | 0.000005321307645058448 | 0.000005266183491053962 |
| lit_floor | 0.03721739906942197 | 0.03721652815296598 | 0.0018172301540692326 | 0.0018173828064241004 |
| left_wall | 0.08475614745053611 | 0.08475626239478186 | 0.01787361953007212 | 0.017873903724108714 |

Captured sweep GPU-frame mean 13.2336 ms control / 14.3263 ms shipped;
CPU-frame mean 13.3028 / 14.4426 ms. These are one run each, reported rather
than claimed as a repeatable performance regression or improvement.

## Validation and judgment

Four deterministic CPU oracle/geometry controls pass, including constant
environment and sealed-reference zero irradiance. All six new Python modules
had no language-server diagnostics when checked. Shader and frame-stamp checks
are enforced by the existing runner/reader, not bypassed.

The only gate-scope judgment is the closure's existing exclusion of rule-0 P2
and retired QA S1 from the shipped PROJECT regression. N1 was retained as a
negative control. No runtime behavior or adoption decision was changed.
