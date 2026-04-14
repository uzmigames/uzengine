/// Flipbook debug PS — samples a sprite atlas with time-based frame cycling.
///
/// This is an independent visualization pass that proves the flipbook atlas
/// sampling + UV math works. If the quad cycles through all atlas frames
/// visibly, the issue with particle flipbook is in the particle data flow
/// (age, lifetime, per-particle CB), not the shader math.
///
/// Entry point: main (mandatory).

struct PSIn {
    float4 positionCS : SV_Position;
    float2 uv         : TEXCOORD0;
};

cbuffer FlipbookDebugCB : register(b0) {
    uint  spriteTextureIdx;   ///< Bindless SRV index for the sprite atlas.
    uint  flipbookRows;       ///< Atlas rows (e.g., 6).
    uint  flipbookCols;       ///< Atlas columns (e.g., 6).
    uint  totalFrames;        ///< Total frames (rows*cols typically).
    float totalTime;          ///< Global time in seconds.
    float fps;                ///< Playback rate (frames/sec).
    uint  _pad0;
    uint  _pad1;
};

SamplerState linearClamp : register(s2);

float4 main(PSIn input) : SV_Target {
    if (totalFrames <= 1u || flipbookCols == 0u || flipbookRows == 0u) {
        return float4(1, 0, 1, 1); // magenta — misconfigured
    }

    // Cycle frame index with global time.
    float frameF = totalTime * fps;
    uint  frameI = (uint)frameF % totalFrames;

    // Atlas tile for this frame.
    uint col = frameI % flipbookCols;
    uint row = frameI / flipbookCols;
    float invCols = 1.0f / (float)flipbookCols;
    float invRows = 1.0f / (float)flipbookRows;

    // Remap [0..1] quad UV into the chosen tile's sub-rect of the atlas.
    float2 atlasUV = float2(
        ((float)col + input.uv.x) * invCols,
        ((float)row + input.uv.y) * invRows
    );

    if (spriteTextureIdx == 0xFFFFFFFFu) {
        return float4(input.uv.x, input.uv.y, 0.5f, 1.0f); // no texture — gradient
    }

    Texture2D<float4> atlas = ResourceDescriptorHeap[spriteTextureIdx];
    float4 samp = atlas.SampleLevel(linearClamp, atlasUV, 0);
    // Show green channel only (matches --channel 2 convention for RGB fire atlases).
    return float4(samp.ggg, 1.0f);
}
