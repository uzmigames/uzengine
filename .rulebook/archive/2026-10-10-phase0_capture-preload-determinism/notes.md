# Notes

## Result (2026-10-09)

Done; docs/analysis/rendering-arch/flicker/capture-preload-determinism.md. The gate passed: defaults and a 30 ms worker delay both
pre-load in 38 frames and agree within one code value, while the delay without the wait gives 45; six
processes, three of them beside 32 CPU busy loops, all give 38. followup-09 then passed 23/23 and
followup-14 11/11 (its default configuration's process spread 0.192 -> 1.19e-07). Bit-identity across
processes is not complete (one code value at a few pixels), which goes with the allocation-layout dependence
to phase0_frame-allocation-layout-dependence.
