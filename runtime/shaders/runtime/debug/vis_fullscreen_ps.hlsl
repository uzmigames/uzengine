/// Vis Fullscreen pixel shader — visualize any intermediate render target.
///
/// Used by the PassInspector system to display a selected target fullscreen.
/// Supports 6 display modes: Color, Grayscale, Depth, HDR, Normal, Velocity.
///
/// SM6.6 (UZ_BINDLESS): texture fetched via ResourceDescriptorHeap[srvIndex].
/// SM5 (!UZ_BINDLESS): register-bound Texture2D at t0.
///
/// Root constants at b0 (8 DWORDs):
///   [0] textureSrv     — SRV descriptor heap index for the target texture
///   [1] displayMode    — VisDisplayMode enum value (0-5)
///   [2] width          — texture width in texels
///   [3] height         — texture height in texels
///   [4] nearPlane      — camera near plane (as uint bits for float)
///   [5] farPlane       — camera far plane (as uint bits for float)
///   [6] _pad0
///   [7] _pad1

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants
// ─────────────────────────────────────────────────────────────────────────────

cbuffer VisRootCB : register(b0) {
    uint   textureSrv;
    uint   displayMode;
    uint   texWidth;
    uint   texHeight;
    float  nearPlane;
    float  farPlane;
    uint   _pad0;
    uint   _pad1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Display mode constants (must match VisDisplayMode enum in pass_inspector.h)
// ─────────────────────────────────────────────────────────────────────────────

static const uint VIS_MODE_COLOR     = 0;
static const uint VIS_MODE_GRAYSCALE = 1;
static const uint VIS_MODE_DEPTH     = 2;
static const uint VIS_MODE_HDR       = 3;
static const uint VIS_MODE_NORMAL    = 4;
static const uint VIS_MODE_VELOCITY  = 5;

// ─────────────────────────────────────────────────────────────────────────────
// SM5 fallback: register-bound texture
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float4> VisTexture : register(t0);
#endif

SamplerState PointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Input structure from fullscreen vertex shader
// ─────────────────────────────────────────────────────────────────────────────

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Octahedral decode (matches octahedral.hlsli)
// ─────────────────────────────────────────────────────────────────────────────

/// Decode an octahedral-encoded normal from [0,1] range.
float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

// ─────────────────────────────────────────────────────────────────────────────
// Linearize reversed-Z depth
// ─────────────────────────────────────────────────────────────────────────────

/// Convert reversed-Z hardware depth [1=near, 0=far] to linear [0,1] (near=white, far=black).
float LinearizeDepth(float rawDepth, float near, float far) {
    // Reversed-Z: depth=1 at near, depth=0 at far.
    // Linear depth = near / (near + rawDepth * (far - near)) when reversed-Z.
    // But we want 0-1 normalized: linearDepth / far.
    float linearZ = near * far / (far - rawDepth * (far - near));
    return saturate(linearZ / far);
}

// ─────────────────────────────────────────────────────────────────────────────
// HDR false-color visualization
// ─────────────────────────────────────────────────────────────────────────────

/// Maps luminance to a rainbow false-color for HDR visualization.
/// Black → blue → cyan → green → yellow → red → white for values 0 → 1 → 2+.
float3 HdrFalseColor(float3 hdr) {
    float lum = dot(hdr, float3(0.2126, 0.7152, 0.0722));

    // Tonemap the luminance to [0,1] using Reinhard for display.
    float mapped = lum / (1.0 + lum);

    // Generate a rainbow gradient based on the original (non-tonemapped) luminance.
    // Values > 1.0 push into red/white.
    float t = saturate(lum * 0.5); // 0-2 range → 0-1

    float3 color;
    if (t < 0.25) {
        // Black → Blue
        color = lerp(float3(0, 0, 0), float3(0, 0, 1), t * 4.0);
    } else if (t < 0.5) {
        // Blue → Green
        color = lerp(float3(0, 0, 1), float3(0, 1, 0), (t - 0.25) * 4.0);
    } else if (t < 0.75) {
        // Green → Yellow
        color = lerp(float3(0, 1, 0), float3(1, 1, 0), (t - 0.5) * 4.0);
    } else {
        // Yellow → Red
        color = lerp(float3(1, 1, 0), float3(1, 0, 0), (t - 0.75) * 4.0);
    }

    // For very bright values (lum > 2.0), push toward white.
    if (lum > 2.0) {
        float excess = saturate((lum - 2.0) * 0.25);
        color = lerp(color, float3(1, 1, 1), excess);
    }

    return color;
}

// ─────────────────────────────────────────────────────────────────────────────
// Velocity visualization
// ─────────────────────────────────────────────────────────────────────────────

/// Visualize motion vectors as color. RG → direction+magnitude.
/// Red = rightward, Green = downward, Blue = magnitude.
float3 VelocityColor(float2 vel) {
    float mag = length(vel);
    // Scale for visibility: 1 pixel of motion = visible color change.
    float scaledMag = saturate(mag * 50.0);
    float2 dir = (mag > 0.0001) ? vel / mag : float2(0, 0);
    return float3(
        saturate(dir.x * 0.5 + 0.5) * scaledMag,
        saturate(dir.y * 0.5 + 0.5) * scaledMag,
        scaledMag
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target0 {
    float2 uv = input.texcoord;

    // Sample the target texture.
    float4 raw;
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[textureSrv];
    raw = tex.SampleLevel(PointSampler, uv, 0);
#else
    raw = VisTexture.SampleLevel(PointSampler, uv, 0);
#endif

    float3 color = float3(0, 0, 0);

    switch (displayMode) {
        case VIS_MODE_COLOR:
            color = raw.rgb;
            break;

        case VIS_MODE_GRAYSCALE:
            color = raw.rrr;
            break;

        case VIS_MODE_DEPTH: {
            float linear = LinearizeDepth(raw.r, nearPlane, farPlane);
            // Invert so near = white, far = black (more intuitive).
            color = (1.0 - linear).xxx;
            break;
        }

        case VIS_MODE_HDR:
            color = HdrFalseColor(raw.rgb);
            break;

        case VIS_MODE_NORMAL: {
            // The normal GBuffer stores octahedral in RG, metallic in B, roughness in A.
            float3 n = OctDecode(raw.rg);
            color = n * 0.5 + 0.5; // Map [-1,1] → [0,1] for display.
            break;
        }

        case VIS_MODE_VELOCITY:
            color = VelocityColor(raw.rg);
            break;

        default:
            color = raw.rgb;
            break;
    }

    // Draw a small label in the top-left corner showing the target name.
    // (Text rendering is not available here — the target name is displayed
    // via the stats overlay or console log instead.)

    // Border indicator: thin 2-pixel magenta border to show vis is active.
    float2 pixelPos = uv * float2(texWidth, texHeight);
    bool isBorder = (pixelPos.x < 2.0) || (pixelPos.y < 2.0) ||
                    (pixelPos.x > (float)texWidth - 2.0) || (pixelPos.y > (float)texHeight - 2.0);
    if (isBorder) {
        color = float3(1.0, 0.0, 1.0); // Magenta border signals "vis mode active"
    }

    return float4(color, 1.0);
}
