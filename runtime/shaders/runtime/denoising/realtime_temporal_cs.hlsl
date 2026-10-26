#include "realtime_common.hlsli"
// RGB/count and moments are pre-spatial histories. Producer alpha is never history.
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    if (!InBounds(p)) return;
    Texture2D<float4> input = ResourceDescriptorHeap[inputSrv];
    Texture2D<float2> velocity = ResourceDescriptorHeap[velocitySrv];
    Texture2D<float4> history = ResourceDescriptorHeap[historySrv];
    Texture2D<float4> moments = ResourceDescriptorHeap[momentsSrv];
    Texture2D<float4> geometry = ResourceDescriptorHeap[geometrySrv];
    Texture2D<float2> guides = ResourceDescriptorHeap[guideSrv];
    RWTexture2D<float4> outHistory = ResourceDescriptorHeap[historyUav];
    RWTexture2D<float4> outMoments = ResourceDescriptorHeap[momentsUav];
    RWTexture2D<float4> outGeometry = ResourceDescriptorHeap[geometryUav];
    RWTexture2D<float2> outGuide = ResourceDescriptorHeap[guideUav];
    RWTexture2D<float2> diagnostics = ResourceDescriptorHeap[diagnosticsUav];
    RWTexture2D<float4> reconstruction = ResourceDescriptorHeap[outputUav];
    Receiver r = CurrentReceiver(p);
    outGeometry[p] = float4(r.position,r.surface ? PackNormal(r.normal) : -1.0);
    outGuide[p] = float2(r.roughness,r.linearDepth);
    float4 current = input.Load(int3(p,0));
    diagnostics[p] = 0.0;
    // Current eligibility always owns the output, including valid-black samples.
    if (!r.surface || !Eligible(current.a)) {
        outHistory[p]=0.0; outMoments[p]=0.0; reconstruction[p]=0.0; return;
    }
    float mean=0.0, second=0.0, samples=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        int2 q=p+int2(x,y);
        if(!InBounds(q)) continue;
        float4 c=input.Load(int3(q,0));
        if(!Eligible(c.a) || !AcceptReceiver(r,CurrentReceiver(q))) continue;
        float l=Luminance(c.rgb);
        mean+=l; second+=l*l; samples+=1.0;
    }
    mean/=max(samples,1.0); second/=max(samples,1.0);
    float sigma=sqrt(max(0.0,second-mean*mean));
    float2 motion=velocity.Load(int3(p,0));
    float motionLength=length(motion);
    float2 uv=(float2(p)+0.5)/float2(width,height);
    float2 previousUV=float2(
        PreviousCoordinate(uv.x,motion.x,float(width),previousJitter.x,currentJitter.x),
        PreviousCoordinate(uv.y,motion.y,float(height),previousJitter.y,currentJitter.y));
    float2 previousPixel=previousUV*float2(width,height)-0.5;
    int2 base=int2(floor(previousPixel));
    float2 f=frac(previousPixel);
    float4 old=0.0, oldMoments=0.0;
    float total=0.0, rejected=0.0;
    if(historyValid!=0 && r.roughness>mirrorRoughness && all(isfinite(previousUV))) {
        for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
            int2 q=base+int2(x,y);
            if(!InBounds(q)) { rejected+=1.0; continue; }
            float4 g=geometry.Load(int3(q,0));
            float2 properties=guides.Load(int3(q,0));
            Receiver prior;
            prior.position=g.xyz; prior.normal=UnpackNormal(max(g.w,0.0));
            prior.surface=g.w>=0.0; prior.roughness=properties.x; prior.linearDepth=properties.y;
            float4 h=history.Load(int3(q,0));
            if(h.a<=0.0 || !AcceptReceiver(r,prior)) { rejected+=1.0; continue; }
            float w=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);
            old+=h*w; oldMoments+=moments.Load(int3(q,0))*w; total+=w;
        }
    }
    float l=Luminance(current.rgb);
    float count=1.0, fastCount=1.0, fast=mean;
    float3 color=current.rgb;
    float2 updatedMoments=float2(l,l*l);
    float clampDistance=0.0;
    if(total>0.00001) {
        old/=total; oldMoments/=total;
        fastCount=min(oldMoments.w+1.0,4.0);
        fast=lerp(oldMoments.z,mean,1.0/fastCount);
        float oldL=Luminance(old.rgb);
        float clamped=samples>0.0?ClampLuminance(oldL,fast,sigma,motionLength):oldL;
        clampDistance=abs(clamped-oldL);
        if(oldL>0.0) old.rgb*=clamped/oldL;
        count=min(old.a+1.0,HistoryCap(r.roughness,motionLength));
        float alpha=max(1.0/count,minCurrentWeight);
        color=lerp(old.rgb,current.rgb,alpha);
        updatedMoments=lerp(oldMoments.xy,float2(l,l*l),alpha);
    } else if(r.roughness>mirrorRoughness) {
        // Disocclusion reconstruction is current-only, never recursively blurred.
        int radius=r.roughness<0.15?1:2;
        float3 sum=0.0; float weights=0.0;
        for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) {
            int2 q=p+int2(x,y);
            if(!InBounds(q)) continue;
            float4 c=input.Load(int3(q,0));
            Receiver s=CurrentReceiver(q);
            if(!Eligible(c.a) || !AcceptReceiver(r,s)) continue;
            float w=exp(-float(x*x+y*y)*0.5)*ReceiverWeight(r,s);
            sum+=c.rgb*w; weights+=w;
        }
        if(weights>0.0) color=sum/weights;
    }
    outHistory[p]=float4(color,count);
    reconstruction[p]=float4(color,current.a);
    outMoments[p]=float4(updatedMoments,fast,fastCount);
    diagnostics[p]=float2(clampDistance,rejected);
}
