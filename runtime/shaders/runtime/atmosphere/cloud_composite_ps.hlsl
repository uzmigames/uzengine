/// Cloud composite — STUB (reimplementing from scratch)
struct FSInput { float4 positionCS : SV_Position; float2 texcoord : TEXCOORD0; };
float4 main(FSInput input) : SV_Target { return float4(0,0,0,1); }
