# Neural Radiance Cache (NRC)

Online-trained neural network that caches indirect radiance in world
space. Developed by NVIDIA (Müller, Rousselle, Novák, Keller — SIGGRAPH
2021). Shipped in some titles but still research-tier for most production
engines.

---

## 1 · The idea

A small MLP (multi-layer perceptron, ~6 layers × 64 neurons) learns to
output the "incoming radiance at a world-space position + direction" from
the current frame's path-traced ground truth. The MLP weights are updated
live from the game's actual ray tracer — **training during rendering**.

Input:  `(worldPos, direction, normal, roughness)` → 7D vector
Output: HDR radiance (3D)

### 1.1 Why online training

Offline-trained denoisers (DLSS-RR) are general-purpose. An online-trained
NRC specializes to **this specific scene right now**, including today's
lighting conditions, moving lights, and whatever weird art direction the
scene has.

Training cost: ~0.5 ms/frame on Tensor cores (hundreds of weight updates).

Inference cost: ~0.3 ms/frame at 1440p (one forward pass per pixel).

Net: ~0.8 ms/frame for a denoiser that beats classical SVGF on many
scenes.

---

## 2 · Where it fits in a pipeline

NRC is a **terminal gather** — instead of continuing a path after bounce
N, you query the NRC for "radiance from this position in this direction,"
terminating the path.

```
path origin → surface 1 → bounce 1 → NRC(pos1, dir) = estimated incoming L
```

This replaces 3+ additional bounces with one inference.

---

## 3 · Strengths

- Dramatically reduces path length → fewer rays → faster.
- Scene-adaptive (unlike DLSS-RR which is generic).
- No offline training pipeline needed.
- Output is smooth (MLP interpolates gracefully).

---

## 4 · Weaknesses

- Requires Tensor cores (NVIDIA Turing+) for real-time.
- Training during rendering is a scheduling headache.
- Cold-start: first 5–10 frames of a new scene are wrong while the MLP
  trains up.
- Specular / high-frequency detail is smoothed out by the MLP's
  inductive bias.

---

## 5 · Industry adoption

- Cyberpunk 2077 Overdrive uses NRC as a "path termination cache."
- Portal RTX uses NRC for GI.
- Most other RTX games use offline-trained denoisers only.

Adoption has been slow because NRC is hard to integrate robustly. A
classical DDGI + NRD stack is easier and "close enough" in quality.

---

## 6 · UzEngine relevance

Not a near-term target. Worth considering in 2+ years once:

1. We have a full RT pipeline.
2. We have robust Tensor-core integration (NGX SDK).
3. We have an offline path-tracer for training validation.

For now, use DDGI + NRD (or RR plug-in). NRC is research capital for
future investigation.

---

## 7 · References

- Müller, Rousselle, Novák, Keller, "Real-time Neural Radiance Caching for Path Tracing" (SIGGRAPH 2021). tom94.net/data/publications/mueller21realtime/mueller21realtime.pdf
- NVIDIA Falcor's RTNRC sample.
- Keller et al., "Online-Trained Neural Radiance Caches" (SIGGRAPH Asia 2023).
