/// Shadow/buffer debug grid visualization — 3x3 on-screen diagnostic overlay.
///
/// Provides a 3x3 grid showing 9 G-Buffer and shadow channels simultaneously
/// when activated via `--shadow-debug 1`. Each tile renders one channel:
///
/// ```
/// [Albedo]    [Normal]    [Shadow Atlas]
/// [Shadow]    [LIT]       [Depth]
/// [Cascade]   [AO]        [Roughness]
/// ```
///
/// # Functions
/// - GetBufferGridTile: maps screen UV to grid tile index and local UV
/// - GetTileLabelColor: per-tile accent color for label strips
/// - GetBorderColor: thin border lines between tiles
/// - GetTileColor: visualizes the selected channel for a given tile
///
/// # Usage
/// Include this header in deferred_lighting_ps.hlsl (or similar) and
/// call the functions when `shadowDebugMode != 0`.

#ifndef SHADOW_DEBUG_HLSLI
#define SHADOW_DEBUG_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Grid Constants
// ─────────────────────────────────────────────────────────────────────────────

static const uint GRID_COLS = 3;
static const uint GRID_ROWS = 3;
static const uint GRID_TILES = 9;

/// Border width as fraction of screen size (thin 1-pixel-ish lines).
static const float BORDER_FRAC = 0.002f;

/// Label strip height as fraction of tile height.
static const float LABEL_HEIGHT_FRAC = 0.06f;

// ─────────────────────────────────────────────────────────────────────────────
// Tile Indices (must match the grid layout documented above)
// ─────────────────────────────────────────────────────────────────────────────

static const uint TILE_ALBEDO      = 0;
static const uint TILE_NORMAL      = 1;
static const uint TILE_ATLAS       = 2;
static const uint TILE_SHADOW      = 3;
static const uint TILE_LIT         = 4;
static const uint TILE_DEPTH       = 5;
static const uint TILE_CASCADE     = 6;
static const uint TILE_AO          = 7;
static const uint TILE_ROUGHNESS   = 8;

// ─────────────────────────────────────────────────────────────────────────────
// GetBufferGridTile
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a full-screen UV coordinate to a grid tile.
///
/// Returns the tile-local UV (for sampling the scene at that position),
/// the tile index (0-8), and whether the pixel falls in the label strip
/// at the top of the tile.
///
/// # Parameters
/// - uv:      Full-screen UV [0,1]^2.
/// - tileUV:  (out) UV within the tile [0,1]^2.
/// - tileIdx: (out) Linear tile index (row * 3 + col), 0-8.
/// - isLabel: (out) True if the pixel is in the label strip.
void GetBufferGridTile(float2 uv, out float2 tileUV, out uint tileIdx, out bool isLabel)
{
    // Compute which cell this UV falls into
    float2 cellF = uv * float2(GRID_COLS, GRID_ROWS);
    uint2 cell = uint2(cellF);
    cell = min(cell, uint2(GRID_COLS - 1, GRID_ROWS - 1));

    // UV within the tile [0,1]
    tileUV = frac(cellF);

    // Linear index
    tileIdx = cell.y * GRID_COLS + cell.x;

    // Label strip at the top of each tile
    isLabel = (tileUV.y < LABEL_HEIGHT_FRAC);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetTileLabelColor
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a distinct accent color for each tile's label strip.
///
/// Colors are chosen for contrast against both light and dark backgrounds.
float3 GetTileLabelColor(uint t)
{
    // Each tile gets a unique hue for quick identification.
    switch (t) {
    case TILE_ALBEDO:    return float3(0.9f, 0.9f, 0.9f); // White
    case TILE_NORMAL:    return float3(0.5f, 0.5f, 1.0f); // Light blue
    case TILE_ATLAS:     return float3(1.0f, 0.8f, 0.2f); // Gold
    case TILE_SHADOW:    return float3(0.3f, 0.3f, 0.3f); // Dark grey
    case TILE_LIT:       return float3(1.0f, 1.0f, 0.6f); // Light yellow
    case TILE_DEPTH:     return float3(0.6f, 1.0f, 0.6f); // Light green
    case TILE_CASCADE:   return float3(1.0f, 0.4f, 0.4f); // Salmon
    case TILE_AO:        return float3(0.8f, 0.8f, 0.8f); // Light grey
    case TILE_ROUGHNESS: return float3(0.7f, 0.5f, 1.0f); // Lavender
    default:             return float3(1.0f, 1.0f, 1.0f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GetBorderColor
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the border color if the UV is on a grid line, or float3(-1,0,0)
/// if not on a border.
///
/// The caller should check result.x < 0 to determine if the pixel is NOT
/// on a border (and should render tile content instead).
float3 GetBorderColor(float2 uv)
{
    // Check proximity to any grid line
    float2 cellF = uv * float2(GRID_COLS, GRID_ROWS);
    float2 fractional = frac(cellF);

    // Near the edge of any cell?
    bool onBorderX = (fractional.x < BORDER_FRAC * GRID_COLS) ||
                     (fractional.x > 1.0f - BORDER_FRAC * GRID_COLS);
    bool onBorderY = (fractional.y < BORDER_FRAC * GRID_ROWS) ||
                     (fractional.y > 1.0f - BORDER_FRAC * GRID_ROWS);

    if (onBorderX || onBorderY)
        return float3(0.15f, 0.15f, 0.15f); // Dark grey border

    // Not on a border — return sentinel
    return float3(-1.0f, 0.0f, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetTileColor
// ─────────────────────────────────────────────────────────────────────────────

/// Visualizes a specific G-Buffer/shadow channel for the given tile.
///
/// # Parameters
/// - t:   Tile index (0-8).
/// - a:   Albedo RGB (linear).
/// - n:   World normal (decoded, [-1,1]).
/// - m:   Metallic [0,1].
/// - r:   Roughness [0,1].
/// - ao:  Ambient occlusion [0,1].
/// - sh:  Shadow factor [0,1] (0=fully shadowed, 1=fully lit).
/// - vd:  View-space linear depth.
/// - d:   Raw hardware depth [0,1].
/// - c:   Final lit color (HDR).
/// - np:  Near plane distance.
/// - fp:  Far plane distance.
/// - cc:  Cascade count (for cascade index visualization).
float3 GetTileColor(uint t, float3 a, float3 n, float m, float r, float ao,
    float sh, float vd, float d, float3 c, float np, float fp, uint cc)
{
    switch (t) {
    case TILE_ALBEDO:
        return a;

    case TILE_NORMAL:
        // Remap [-1,1] to [0,1] for display
        return n * 0.5f + 0.5f;

    case TILE_ATLAS:
        // Raw shadow value as grayscale (useful for seeing atlas coverage)
        return sh.xxx;

    case TILE_SHADOW:
        // Shadow factor: white = lit, black = shadowed
        return sh.xxx;

    case TILE_LIT:
        // Final lit color (may be HDR — viewer does tonemap separately)
        return c;

    case TILE_DEPTH: {
        // Linearized depth normalized [0,1], near=white, far=black
        float range = max(fp - np, 0.001f);
        float normalized = saturate(1.0f - (vd - np) / range);
        return normalized.xxx;
    }

    case TILE_CASCADE: {
        // Cascade index visualization: color-code by depth band.
        // Divides view depth into cc equal bands and assigns a distinct color.
        float range = max(fp - np, 0.001f);
        float normalizedDepth = saturate((vd - np) / range);
        uint cascadeIdx = min(uint(normalizedDepth * float(cc)), cc - 1u);

        // Cascade colors: red, green, blue, yellow (standard cascade debug)
        if (cascadeIdx == 0) return float3(1.0f, 0.2f, 0.2f);
        if (cascadeIdx == 1) return float3(0.2f, 1.0f, 0.2f);
        if (cascadeIdx == 2) return float3(0.2f, 0.2f, 1.0f);
        return float3(1.0f, 1.0f, 0.2f);
    }

    case TILE_AO:
        return ao.xxx;

    case TILE_ROUGHNESS:
        return r.xxx;

    default:
        return c;
    }
}

#endif
