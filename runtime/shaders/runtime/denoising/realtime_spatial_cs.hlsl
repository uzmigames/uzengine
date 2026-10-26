#include "realtime_common.hlsli"
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p=int2(id.xy);
    if(!InBounds(p)) return;
    Texture2D<float4> raw=ResourceDescriptorHeap[inputSrv];
    RWTexture2D<float4> output=ResourceDescriptorHeap[outputUav];
    float4 current=raw.Load(int3(p,0));
    Receiver r=CurrentReceiver(p);
    if(!r.surface || !Eligible(current.a)) { output[p]=0.0; return; }
    if(r.roughness<=mirrorRoughness) { output[p]=current; return; }
    static const float b3[5]={1.0,4.0,6.0,4.0,1.0};
    float3 sum=0.0;
    float weights=0.0;
    int stride=max(1,int(round(r.roughness*spatialRadiusScale)));
    for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) {
        int2 q=p+int2(x,y)*stride;
        if(!InBounds(q) || !Eligible(raw.Load(int3(q,0)).a)) continue;
        Receiver s=CurrentReceiver(q);
        if(!AcceptReceiver(r,s)) continue;
        float w=b3[x+2]*b3[y+2]*ReceiverWeight(r,s);
        sum+=raw.Load(int3(q,0)).rgb*w; weights+=w;
    }
    output[p]=float4(sum/max(weights,0.00001),current.a);
}
