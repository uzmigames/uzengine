# Indoor Lighting & Shadow Propagation — Analysis

Complete analysis of how production engines handle indoor lighting, shadow propagation
in enclosed spaces, and GI solutions for semi-enclosed environments like the Sponza atrium.

## The Problem

UzEngine's current pipeline: directional sun → CSM shadow maps → deferred lighting.
Inside a building, the CSM correctly shadows everything (walls/ceiling block the sun),
but without GI or supplemental lighting, the interior is uniformly dark with no shadows
from interior geometry (columns, arches, etc.).

## Documents

| File | Engine(s) | Focus |
|------|-----------|-------|
| [ue5-indoor-lighting.md](ue5-indoor-lighting.md) | UE5 | Lumen, Lightmass, SkyLight, DFAO, VSM |
| [ue5-csm-vs-vsm.md](ue5-csm-vs-vsm.md) | UE5 | CSM vs VSM for mixed indoor/outdoor |
| [frostbite-cryengine-gi.md](frostbite-cryengine-gi.md) | Frostbite, CryEngine | LPV, SVOGI, Enlighten, DDGI |
| [threejs-godot-analysis.md](threejs-godot-analysis.md) | Three.js, Godot 4 | VoxelGI, SDFGI, HemisphereLight, practical patterns |
| [parameter-reference.md](parameter-reference.md) | All | Side-by-side parameter comparison table |
| [implementation-guide.md](implementation-guide.md) | UzEngine | 3-phase implementation roadmap |
| [sponza-quick-fix.md](sponza-quick-fix.md) | UzEngine | Immediate 2-hour fix for Sponza |

## Key Insight

**Every production engine uses a layered approach — no single system solves indoor lighting:**

| Layer | System | Indoor Role |
|-------|--------|-------------|
| 1 | CSM/VSM | Direct sun (correctly = 0 indoors) |
| 2 | **Sky Light / Ambient** | Base fill from visible sky |
| 3 | **Artist fill lights** | 3-5 point/spot lights per room |
| 4 | **SSAO/GTAO** | Contact shadows, depth perception |
| 5 | **GI (optional)** | Bounced light color/direction |

The **most impactful** and **cheapest** solution: Sky Light ambient + artist-placed fill lights.
GI (Lumen, SVOGI, VoxelGI) adds realism but is expensive and not strictly required.

## UzEngine Current State

| Feature | Status | Notes |
|---------|--------|-------|
| CSM shadows | Working | 4 cascades, 4096x4096 |
| Sky Light (SH) | Partial | SH coefficients exist but only from sky, not from scene probes |
| SSGI | Exists | 5-pass screen-space GI |
| GTAO | Working | Contact shadows |
| Light probes | Missing | No irradiance probe grid |
| Fill lights | Working | Point/spot lights in scene |
| VoxelGI | Missing | Not implemented |
| DDGI | Missing | Requires DXR (infrastructure ready) |

## Implementation Priority

1. **Phase 1 (2h)**: Add point lights to Sponza scene + boost SkyLight intensity
2. **Phase 2 (1w)**: Light probe grid system (CPU-side SH interpolation)
3. **Phase 3 (2-3w)**: DDGI via DXR (RT infrastructure already in place)
