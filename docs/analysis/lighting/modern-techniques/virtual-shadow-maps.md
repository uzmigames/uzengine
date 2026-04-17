# Virtual Shadow Maps (VSM)

UE5's VSM replaces CSM (cascaded shadow maps) with a **sparse, virtualized,
page-backed** shadow map system. Every directional light effectively has
a 32768² shadow map, but only the tiles that pixels on screen use are
actually rendered. Same concept as virtual texturing, applied to shadows.

---

## 1 · Why VSM exists (CSM problems)

Classical CSM:

- 4 cascade levels × 2048² per cascade = ~67 MB
- Uniform resolution within each cascade = **blocky edges at cascade transitions**
- Fixed distance partitioning = **over-resolution up close, under-resolution far away**
- Peter-panning and shadow acne require manual bias tuning
- Alpha-tested foliage broken (masked shadows)
- Resolution scales with view frustum, not with what's visible

VSM fixes every one of these.

---

## 2 · Core idea

Logical shadow map: 32K × 32K (8 bits / texel for Hi-Z form), per
directional light, per clipmap level (16 levels for a huge view range).

Logical size = 8 GB per light. Unaffordable.

Physical page atlas: ~2048 × 2048 = 4 MB of actual allocated shadow pages
(at 128² per page).

A **page table** maps logical tile → physical page (or "not allocated").
Only the pages covered by on-screen pixels are rendered.

---

## 3 · Page table

From Stephano's write-up:

| Field | Bits | Purpose |
|-------|------|---------|
| Frame marker | 4 | Track usage: 0 = unused, 1 = this frame, 2+ = stale |
| Physical page X | 12 | Physical atlas column |
| Physical page Y | 12 | Physical atlas row |
| Memory pool idx | 2 | Which of multiple atlases |
| Residency flag | 1 | Backed by physical memory? |
| Valid / dirty flag | 1 | Needs re-render? |

A 32K logical with 128² pages = 256 × 256 page table entries per clipmap
level. 16 levels = 64K entries × 4 B = 256 KB per light. Very cheap.

---

## 4 · Frame pipeline

### 4.1 Mark phase

For every pixel on screen, compute:

```
shadowUV = transform pixel world-pos into shadow-map clip-space
clipmapLevel = log2(pixelFootprint / shadowTexelSize)
pageCoord = shadowUV / pageSize
```

Mark that page in the page table as "wanted this frame".

### 4.2 Allocate phase

A compute pass walks the page table. For each marked page:
- If already resident → nothing to do
- If not resident → allocate a physical page from the free list
- Mark the allocation as dirty (needs render)

### 4.3 Render phase

For each dirty page, render the scene geometry into that page. This is
where it integrates with **Nanite**:

- Nanite's cluster LOD evaluates at shadow-resolution for the target page
  (not at the camera LOD)
- Each page renders only the geometry whose bounds overlap that page's
  frustum
- HZB culling skips geometry occluded by already-rendered pages

### 4.4 Sample phase

Per pixel, sample the shadow:

```
page = PageTable[shadowUV / pageSize]
if (!page.resident) return unshadowed;   // fallback
shadowTexel = PhysicalAtlas[page.physicalCoord + localUV]
return shadowTest(shadowTexel, pixelDepth)
```

Standard PCF or PCSS on top.

---

## 5 · HZB traversal

During the Mark phase, VSM uses a hierarchical Z-buffer to efficiently
compute per-pixel clipmap levels. Same HZB used for occlusion culling.

---

## 6 · Nanite interaction

This is the critical win. Without Nanite, rendering a 32K shadow map would
require re-rasterizing the scene at that resolution — infeasible. With
Nanite, rendering a 128² shadow page requires reasonable tri counts because
Nanite picks appropriate LOD per page. 

Even without Nanite, VSM works — just with traditional mesh-based LOD and
higher tri counts per page. Fortnite BR Ch4 showed that VSM gets ~80% of
the benefit on non-Nanite content. Relevant for UzEngine since we don't
have Nanite yet.

---

## 7 · Local lights (point, spot)

VSM extends to local lights via:

- Point lights: 6 cubemap faces, each a VSM. Most faces are empty if the
  light only illuminates one side.
- Spot lights: single face, standard VSM.

Local-light VSMs share the same physical atlas. A light that illuminates
only a small area consumes only a few pages.

---

## 8 · Alpha-tested / masked geometry

VSM handles masked foliage cleanly because each page is independently
rendered — the page rasterization can do per-pixel alpha test without
affecting other pages. CSM struggled with this because one cascade
frustum covers many objects and alpha-test noise was unstable temporally.

---

## 9 · Memory

| Asset | Size |
|-------|------|
| Physical atlas (4K²×R8) | 16 MB |
| Page tables (1 dir + 16 local) | ~5 MB |
| HZB mip chain | ~2 MB |
| Total per scene | ~25 MB (vs ~67 MB CSM + per-light caches) |

Less memory than CSM, dramatically better quality.

---

## 10 · Performance

Fortnite BR Chapter 4 public numbers:

- PC GTX 1080: 1.8 ms → 2.3 ms (slightly slower than CSM)
- RTX 3080: 0.9 ms (unchanged from CSM)
- Scene complexity up 10× due to Nanite; VSM stays flat

Performance is comparable to CSM at equivalent quality, but VSM delivers
dramatically higher quality in the same budget.

---

## 11 · UzEngine relevance

We currently have CSM for directional (in `runtime/engine/renderer/shadows/shadow_cascade.{cpp,h}`) and a local-light atlas for point/spot. Replacing CSM with VSM is **feasible without Nanite** and is a big quality upgrade. Steps:

1. Build a page-table infrastructure (sparse 2D + 3D).
2. Build a physical atlas allocator (free list in SSBO).
3. Mark pass (compute over G-Buffer to mark wanted pages).
4. Allocate pass (CPU/GPU dispatch).
5. Render pass (render geometry to dirty pages with tiny frustums).
6. Sample pass (replace CSM sample in lighting shader).

~4 weeks of engineering. Drops CSM artifacts across all directional-light
scenes. Rework of our shadow shader to page-table-sample pattern.

Also a good interim step before HWRT sun shadows (which VSM still beats
on perf in many cases).

---

## 12 · References

- Epic docs, "Virtual Shadow Maps in Unreal Engine" (dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine).
- Epic, "Virtual Shadow Maps in Fortnite Battle Royale Chapter 4" (unrealengine.com/tech-blog/virtual-shadow-maps-in-fortnite-battle-royale-chapter-4) — numbers, tuning, gotchas.
- Stephano, "Sparse Virtual Shadow Maps" (ktstephano.github.io/rendering/stratusgfx/svsm) — self-contained reimplementation write-up.
- Olsson & Sintorn, "Efficient Virtual Shadow Maps for Many Lights" (2014) — the academic paper that predated UE5.
- `lighting/shadows/ue5-csm-vs-vsm.md` (this repo) — our existing analysis.
