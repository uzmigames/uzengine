#ifndef OCTAHEDRAL_HLSLI
#define OCTAHEDRAL_HLSLI

/// Encode a unit normal to octahedral [0,1]² representation.
///
/// Projects the normal onto the L1 unit sphere, then folds the lower
/// hemisphere into the outer edges of the [−1,1]² square. The result is
/// remapped to [0,1]² for storage in an R8G8 render target.
///
/// Reference: "Survey of Efficient Representations for Independent Unit Vectors"
/// (Cigolle, Donow, Evangelakos, Mara, McGuire, Meyer — JCGT 2014)
///
/// # Parameters
/// - n: unit-length world-space normal.
///
/// # Returns
/// float2 in [0,1]² suitable for R8G8_UNORM storage.
float2 OctEncode(float3 n)
{
    // Project onto L1 norm sphere: sum(|xyz|) = 1
    float3 p = n / (abs(n.x) + abs(n.y) + abs(n.z));

    // Wrap lower hemisphere — fold the four triangular flaps outward.
    // sign(0) == 0 in HLSL, which would collapse the edge to zero.
    // Use the conditional form to map zero to +1, preserving the octahedron seam.
    float2 wrap = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0,
                                               p.y >= 0.0 ? 1.0 : -1.0);
    float2 oct = (n.z >= 0.0) ? p.xy : wrap;

    // Remap [-1,1] to [0,1]
    return oct * 0.5 + 0.5;
}

/// Decode octahedral [0,1]² back to a unit normal.
///
/// Reverses the encoding: remaps to [−1,1]², reconstructs Z from the L1
/// constraint, unfolds the lower hemisphere, and normalizes.
///
/// # Parameters
/// - f: float2 in [0,1]² read from the G-Buffer RG channels.
///
/// # Returns
/// Unit-length world-space normal (float3).
float3 OctDecode(float2 f)
{
    // Remap [0,1] to [-1,1]
    f = f * 2.0 - 1.0;

    // Reverse octahedral projection — Z from L1 constraint
    float3 n = float3(f.xy, 1.0 - abs(f.x) - abs(f.y));

    // Unwrap lower hemisphere (same conditional sign as encode)
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0 ? -t : t,
                    n.y >= 0.0 ? -t : t);

    return normalize(n);
}

#endif // OCTAHEDRAL_HLSLI
