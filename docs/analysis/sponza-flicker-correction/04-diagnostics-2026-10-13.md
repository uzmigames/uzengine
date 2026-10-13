# Owed diagnostics, 2026-10-13

These are measurements, not correction selections. H-27 acceptance remains
reserved for the user. Each section below records a completed study.

## H-45 at right_shadow

Three interleaved records-off/on pairs, frames 237..364, fixed exposure 1,
jitter 8, full right_shadow rectangle (1472,464,256,256).
Report: `.screenshots/flicker-correction-20261013-h45/door_reading.json`.
Status complete; C0 instrumentation equivalence PASSED.

Largest relative region RGB energy differences were <=3.22e-6 against
the 1% margin. Output oscillation difference was <=2.36e-8 against
the same-study 2.02e-5 floor. The glass-mask output oscillation difference
was <=4.24e-7 against its 1.96e-3 margin.

G0: 558 pixels drawn at least once in every repeat. O1: no inconsistent
coverage/layer records. O2: no multiple layers, maximum layer count 1.
G1: 0 failures in 162,288 evaluated drawn pixel-frames; worst per-RGB-channel
relative reconstruction error 4.36e-4, below the 2e-3 threshold.

Coverage: 304 pixels always drawn, 254 partly drawn, 64,978 never drawn.
All 254 partly drawn pixels are jitter-locked in all repeats; coverage is
identical across repeats. Opacity, albedo, identifiers, emission, recorded
IBL diffuse, N.L and produced source RGB are constant at drawn pixels.
Shading normal varies at all 304 always-drawn pixels and 211 partly drawn
pixels, locked to jitter, but the produced source RGB does not vary.

Worst-repeat class oscillations, linear Rec.709 radiance:

| Class | Pixels | Raw input | TAA output | Share of output summed pixel std |
|---|---:|---:|---:|---:|
| Whole region | 65,536 | 0.01292 | 0.0004032 | 100% |
| Always drawn | 304 | 0 | 0.04988 | 57.4% |
| Partly drawn | 254 | 3.252 | 0.02659 | 25.6% |
| Never drawn | 64,978 | 0.0003230 | 0.00006935 | 17.1% |

At partly drawn pixels, raw luminance is 7.606 when drawn and 0.0003863
when not drawn. Thus coverage changes dominate the raw-region oscillation
(97.5% of summed pixel std), while 57.4% of output oscillation is at pixels
whose own raw sample is constant. This establishes the same broad
coverage/reconstruction distinction at right_shadow as at the door;
it does not identify a production correction.
