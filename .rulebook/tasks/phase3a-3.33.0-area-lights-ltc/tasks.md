## 1. Implementation

- [ ] 1.1 Generate LTC precomputed textures — compute or import 2× 64×64 LUT textures (Heitz 2016 reference data). LTC Matrix: RGBA32F (M⁻¹ coefficients indexed by roughness × NdotV). LTC Amplitude: RG32F (Fresnel scale/bias). Load as engine resources at init.
- [ ] 1.2 Implement LTC evaluation HLSL include (`ltc.hlsli`) — given surface point, normal, view dir, roughness, and rect light corners: (1) lookup M⁻¹ from LTC matrix texture at (roughness, NdotV), (2) transform rect corners by M⁻¹, (3) clip transformed polygon to upper hemisphere, (4) analytically integrate cosine over polygon (IntegrateEdge formula), (5) multiply by Fresnel from amplitude texture.
- [ ] 1.3 Implement rect light polygon clipping — clip quadrilateral to hemisphere (z > 0 plane). Handle 3-6 sided result polygon after clipping. Sum edge integrals: `IntegrateEdge = acos(dot(v1,v2)) * normalize(cross(v1,v2)).z`.
- [ ] 1.4 Implement LTC diffuse — same framework but with identity matrix (M = I). Diffuse = cosine distribution, analytically integrated over rect solid angle. Faster than specular (no LUT lookup needed).
- [ ] 1.5 Add RectArea light type to LightGPU struct — `lightType=3`. Additional data: `float2 lightSize` (width, height), `float3 lightRight` (local X axis), `float3 lightUp` (local Y axis). 4 rect corners derived from position + right/up × size/2.
- [ ] 1.6 Integrate area lights into deferred lighting — after point/spot evaluation: if `lightType == 3`, evaluate LTC specular + diffuse. Add to final lighting.
- [ ] 1.7 Bind LTC textures — register t8, t9 (or bindless). Bound for entire lighting pass. Static textures loaded once.
- [ ] 1.8 Implement area light attenuation — distance from pixel to closest point on rect surface. Smooth falloff: `atten = saturate(1 - (dist/range)²)² / (dist² + 1)`. Same windowing as point lights.
- [ ] 1.9 Create area light demo — glossy floor plane with rectangular area lights above. Camera showing rect-shaped specular reflection. Roughness gradient showing reflection transition from sharp rect to diffuse glow.

## 2. Visual Verification

- [ ] 2.1 Screenshot: glossy floor reflecting rectangular light — rect shape visible in reflection
- [ ] 2.2 Screenshot: roughness gradient — mirror=sharp rect, rough=blurred rect, very rough=diffuse blob
- [ ] 2.3 Screenshot: area light vs point light — area produces softer, wider illumination
- [ ] 2.4 Screenshot: large area light — soft shadows and wide specular
- [ ] 2.5 Regression: existing point/spot lights unchanged

## 3. Documentation

- [ ] 3.1 Document LTC algorithm with Heitz 2016 reference
- [ ] 3.2 Add area light parameters with UE5 RectLight equivalents
- [ ] 3.3 Document LTC texture format and generation process
