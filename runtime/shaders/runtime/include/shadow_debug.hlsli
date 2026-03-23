/// Shadow debug grid — stub. Will be implemented later.
#ifndef SHADOW_DEBUG_HLSLI
#define SHADOW_DEBUG_HLSLI

void GetBufferGridTile(float2 uv, out float2 tileUV, out uint tileIdx, out bool isLabel) {
    tileIdx = 4; tileUV = uv; isLabel = false;
}
float3 GetTileLabelColor(uint t) { return float3(1,1,1); }
float3 GetBorderColor(float2 uv) { return float3(-1,0,0); }
float3 GetTileColor(uint t, float3 a, float3 n, float m, float r, float ao,
    float sh, float vd, float d, float3 c, float np, float fp, uint cc) { return c; }

#endif
