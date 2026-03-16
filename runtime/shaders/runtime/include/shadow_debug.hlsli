/// Buffer Visualization Overlay — UE5-style diagnostic grid.
///
/// Renders a 3x3 grid where each tile shows a different buffer.
/// Each tile re-samples the G-Buffer at the remapped UV to show
/// the full scene in each tile (miniature view).
///
/// Grid layout:
///   ┌──────────┬──────────┬──────────┐
///   │ Albedo   │ Normals  │ Metallic │
///   ├──────────┼──────────┼──────────┤
///   │ Shadow   │  LIT     │ Depth    │
///   ├──────────┼──────────┼──────────┤
///   │ Cascades │ AO       │ Roughness│
///   └──────────┴──────────┴──────────┘

#ifndef SHADOW_DEBUG_HLSLI
#define SHADOW_DEBUG_HLSLI

static const uint kGridCols = 3;
static const uint kGridRows = 3;

/// Label bar fraction at the bottom of each tile.
static const float kLabelFrac = 0.08f;

/// Cascade color palette.
static const float3 kCascadeCol[8] = {
    float3(1, 0.15f, 0.15f), float3(0.15f, 1, 0.15f),
    float3(0.2f, 0.3f, 1),   float3(1, 1, 0.15f),
    float3(0.15f, 1, 1),     float3(1, 0.15f, 1),
    float3(1, 0.6f, 0.15f),  float3(0.6f, 0.15f, 1)
};

/// Label colors — one per tile for the colored bar at the bottom.
static const float3 kLabelCol[9] = {
    float3(0.85f, 0.25f, 0.25f),  // 0 Albedo
    float3(0.45f, 0.45f, 1.0f),   // 1 Normal
    float3(0.65f, 0.65f, 0.65f),  // 2 Metallic
    float3(0.15f, 0.15f, 0.15f),  // 3 Shadow
    float3(1.0f, 1.0f, 1.0f),     // 4 LIT
    float3(0.4f, 0.4f, 0.4f),     // 5 Depth
    float3(1.0f, 0.9f, 0.2f),     // 6 Cascade
    float3(0.25f, 0.75f, 0.25f),  // 7 AO
    float3(0.85f, 0.55f, 0.2f)    // 8 Roughness
};

/// Computes the buffer visualization grid.
///
/// Call this at the TOP of the main() function, BEFORE any G-Buffer reads.
/// It remaps the UV per-tile and returns a flag indicating whether the caller
/// should use the returned color (debug active) or continue normal rendering.
///
/// # Parameters
/// - screenUV: original fullscreen UV [0,1]
/// - outTileUV: remapped UV for G-Buffer sampling within the current tile
/// - outTileIdx: which tile this pixel belongs to (0-8)
/// - outIsLabel: true if this pixel is in the label bar
///
/// # Returns
/// true if debug grid is active (shadowDebugMode != 0).
bool GetBufferGridTile(float2 screenUV, out float2 outTileUV, out uint outTileIdx, out bool outIsLabel) {
    uint col = min((uint)(screenUV.x * float(kGridCols)), kGridCols - 1);
    uint row = min((uint)(screenUV.y * float(kGridRows)), kGridRows - 1);
    outTileIdx = row * kGridCols + col;

    // Local UV within tile [0,1]
    float2 localUV = float2(
        frac(screenUV.x * float(kGridCols)),
        frac(screenUV.y * float(kGridRows))
    );

    outIsLabel = (localUV.y > (1.0f - kLabelFrac));

    // Remap tile UV to full-screen UV for G-Buffer resampling
    outTileUV = localUV;
    // Scale Y to exclude label bar area
    outTileUV.y = localUV.y / (1.0f - kLabelFrac);
    outTileUV.y = saturate(outTileUV.y);

    return true;
}

/// Converts raw G-Buffer data into the visualization for a specific tile.
///
/// # Parameters
/// - tileIdx: 0-8, which tile
/// - albedo, N, metallic, roughness, ao: decoded G-Buffer values
/// - shadow: computed shadow factor
/// - viewDepth: camera distance
/// - depth: raw hardware depth
/// - litColor: fully computed lit+tonemapped color
/// - nearP, farP: camera planes
/// - cascadeCount_: cascade count
///
/// # Returns
/// Visualization color for this tile.
float3 GetTileColor(
    uint tileIdx,
    float3 albedo, float3 N, float metallic, float roughness, float ao,
    float shadow, float viewDepth, float depth, float3 litColor,
    float nearP, float farP, uint cascadeCount_
) {
    // Sky pixels (depth ≈ 0 in reversed-Z) show as dark background
    bool isSky = (depth <= 0.0001f) && (tileIdx != 4); // LIT tile shows sky normally
    if (isSky) return float3(0.05f, 0.05f, 0.08f); // Dark blue-gray for sky regions

    switch (tileIdx) {
        case 0: return albedo;                                          // Albedo
        case 1: return N * 0.5f + 0.5f;                                // Normals (remapped)
        case 2: return float3(metallic, metallic, metallic);            // Metallic
        case 3: return float3(shadow, shadow, shadow);                  // Shadow
        case 4: return litColor;                                        // LIT (center)
        case 5: {                                                       // Depth (linearized)
            float linZ = (nearP * farP) / (nearP + depth * (farP - nearP));
            float nZ = saturate(linZ / farP);
            return float3(nZ, nZ, nZ);
        }
        case 6: {                                                       // Cascade index
            uint ci = cascadeCount_ > 0 ? cascadeCount_ - 1 : 0;
            for (uint i = 0; i < cascadeCount_; i++) {
                if (viewDepth <= cascadeSplits(i)) { ci = i; break; }
            }
            return kCascadeCol[min(ci, 7u)] * lerp(0.3f, 1.0f, shadow);
        }
        case 7: return float3(ao, ao, ao);                              // AO
        case 8: return float3(roughness, roughness, roughness);         // Roughness
        default: return float3(1, 0, 1);                                // Magenta = error
    }
}

/// Renders the label bar for a tile.
float3 GetTileLabelColor(uint tileIdx) {
    return kLabelCol[min(tileIdx, 8u)];
}

/// Returns the border color if the pixel is on a tile border, or float3(-1,-1,-1) if not.
float3 GetBorderColor(float2 screenUV) {
    float2 localUV = float2(
        frac(screenUV.x * float(kGridCols)),
        frac(screenUV.y * float(kGridRows))
    );
    float b = 0.004f;
    if (localUV.x < b || localUV.x > (1.0f - b) ||
        localUV.y < b || localUV.y > (1.0f - b)) {
        return float3(0.1f, 0.1f, 0.1f);
    }
    return float3(-1.0f, -1.0f, -1.0f);
}

#endif // SHADOW_DEBUG_HLSLI
