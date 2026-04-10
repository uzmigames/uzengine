# CSM caster pullback — shadow volume must extend beyond the frustum sphere

**Category**: rendering
**Tags**: rendering, csm, shadows, cpu, ue5

## Description

Ao construir a lightView/lightProj de uma cascade CSM a partir do bounding sphere do frustum da câmera, o `lightEye` DEVE ser pulled back múltiplas vezes o radius (não apenas `radius * 1`), e o `orthoFar` DEVE estender de `pullback` a `pullback + radius`. Isso garante que shadow casters que estão entre o sol e o frustum — mas fora da própria sphere — sejam incluídos na rasterização do shadow map.

O padrão ingênuo (`lightEye = center - lightDirNorm * radius`, `orthoFar = 2 * radius`) rasteriza apenas casters DENTRO da bounding sphere. Em ambientes fechados como Sponza, as paredes externas que deveriam projetar sombra no interior ficam fora da sphere → não entram no shadow atlas → sol "vaza" através delas. O bug se manifesta como iluminação seguindo a câmera ao rotacionar, porque o set de casters incluídos muda com o frustum da câmera.

Valor prático: `kCasterPullbackScale = 8.0f` é um fallback razoável para cenas até algumas centenas de metros. A solução ideal é derivar um `CastersBox` per-frame da AABB da scene (padrão UE5 `FShadowCascadeSettings::ShadowBoundsAccurate`), limitado superiormente pelo bounding sphere da scene.

Custo: multiplicar o pullback por 8x reduz precisão de depth do shadow map em ~3 bits, ainda dentro de 24-bit precision.

## Example

// shadow_cascade.cpp — ComputeCascades
// ANTES (errado):
const Vec3 lightEye = center - lightDirNorm * radius;
const float orthoNear = 0.0f;
const float orthoFar  = 2.0f * radius;

// DEPOIS (correto):
const float kCasterPullbackScale = 8.0f;
const float pullback = radius * kCasterPullbackScale;
const Vec3  lightEye = center - lightDirNorm * pullback;
const float orthoNear = 0.0f;
const float orthoFar  = pullback + radius;  // cover pullback region + sphere

const Mat4 lightView = Mat4::LookAt(lightEye, center, lightUp);
const Mat4 lightProj{XMMatrixOrthographicOffCenterLH(
    -radius, radius, -radius, radius, orthoNear, orthoFar)};

## When to Use

Ao construir o lightView/lightProj de qualquer cascade CSM derivada do frustum da câmera. Aplica-se a TODAS as cascadas (near e far).

## When NOT to Use

Shadow maps de point/spot lights (volume esférico ou cônico, não frustum-based). Para esses, o volume é definido pelo raio/cone da luz, não pelo frustum da câmera.
