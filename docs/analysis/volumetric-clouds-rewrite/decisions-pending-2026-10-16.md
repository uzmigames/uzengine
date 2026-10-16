# Decisions pending — autonomous lighting run (2026-10-16)

The user is away ~10h and asked me to run the lighting (volumetric-clouds) tasks
in sequence, saving any decision that needs them and skipping non-blocking items.
This file records decisions left for the user. Each entry: what, why it needs you,
the default I took (if non-blocking) or why I skipped (if blocking).

Status: in progress. HEAD at start: 27a21d36.

## Decisions / notes

### D1 (2026-10-16) — clouds M4b / item 4.10: how much cloud-ground-shadow silhouette polish?

**Context.** The near-black ground under cloud ground-shadows (gate r.Clouds.GroundShadows, default OFF) was a SCENE issue, not a shader bug: the old m4-ground scene was fully overcast with no skylight, so a sun-blocked ground went to pure black. The deferred_lighting cloud-shadow term already correctly scales ONLY the direct sun (renderer source unchanged; default-off DXIL byte-matches HEAD). On a scattered-cloud scene WITH a skylight (m4-scattered.uzscene), the ground now renders LIT grey (not black) with GroundShadows=1; GBV exit 0, gtests 7661 pass.

**What still needs a call.** From the current horizon framing the cloud-SHAPED shadow silhouettes are NOT readable on the ground (it's uniform lit grey; see .screenshots/cloud-m4/scattered-gbv/frames/f_15.png). Making them readable needs a top-down camera looking at the ground plane + denser scattered clouds + a lower sun angle — iterative scene/tuning work on an OPT-IN feature (default OFF, so the shipped product is unaffected either way).

**Default I took (non-blocking):** committed the verified improvement (ground is now LIT not black; feature is correct by construction + default-safe), marked 4.10 as "ground-black fixed; readable silhouettes deferred", and moved on to M6.

**Decision for you:** (a) accept as-is (ground-lit + correct, silhouettes an opt-in known-limit), or (b) you want full silhouette polish (I author a top-down scene + tune density/sun until shadows read clearly). Tell me (b) and I'll finish it.
