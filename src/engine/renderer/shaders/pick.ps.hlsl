// pick.ps.hlsl -- Pick buffer pixel shader
// Outputs node ID encoded as RGB color.
//
// Root constants:
//   b1: MaterialCB -- nodeId packed into first DWORD

cbuffer MaterialCB : register(b1) {
    uint nodeId;
};

float4 PSMain() : SV_TARGET {
    // Encode nodeId as RGB: R=bits[0:7], G=bits[8:15], B=bits[16:23]
    float r = float(nodeId & 0xFF) / 255.0;
    float g = float((nodeId >> 8) & 0xFF) / 255.0;
    float b = float((nodeId >> 16) & 0xFF) / 255.0;
    return float4(r, g, b, 1.0);
}
