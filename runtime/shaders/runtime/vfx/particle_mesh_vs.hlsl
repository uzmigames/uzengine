struct O{float4 p:SV_Position;float2 u:TEXCOORD0;};O main(uint v:SV_VertexID){O o;o.u=float2((v<<1)&2,v&2);o.p=float4(o.u*float2(2,-2)+float2(-1,1),0,1);return o;}
