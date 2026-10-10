// ─────────────────────────────────────────────────────────────────────────────
// vfx_camera.hlsli — Shared camera data interface for VFX compute shaders.
//
// Matches `VfxCameraConstants` in runtime/engine/vfx/data_interfaces/vfx_camera_di.h.
// Include this in any spawn / update / stage CS that needs active-camera
// state; the runtime binds the CB once per frame at register(b8).
//
// @ref UE5 UNiagaraDataInterfaceCamera
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_VFX_CAMERA_HLSLI
#define UZ_VFX_CAMERA_HLSLI

cbuffer VfxCameraCB : register(b8) {
    float4x4 vfxCameraView;           // world -> view
    float4x4 vfxCameraProj;           // view -> clip

    float3   vfxCameraPos;
    float    _vfxCameraPadPos;

    float3   vfxCameraForward;
    float    vfxCameraFovY;           // radians

    float    vfxCameraNearZ;
    float    vfxCameraFarZ;
    float    vfxCameraAspect;
    float    _vfxCameraPadTail;

    float2   vfxCameraScreenSize;
    float2   _vfxCameraPadScreen;
};

// World-space camera position accessor (short form used by preset modules).
float3 VfxCameraPosition() { return vfxCameraPos; }

// Normalised camera forward vector.
float3 VfxCameraForward()  { return vfxCameraForward; }

// Projects a world-space position to clip space with the active camera.
float4 VfxCameraWorldToClip(float3 worldPos) {
    return mul(vfxCameraProj, mul(vfxCameraView, float4(worldPos, 1.0f)));
}

// Linear depth in camera space at a given world-space position. Useful for
// screen-space-size driven LOD and soft-particle effects.
float VfxCameraLinearViewDepth(float3 worldPos) {
    const float4 viewPos = mul(vfxCameraView, float4(worldPos, 1.0f));
    // Convention: -z is forward in view space; clamp to [near, far] so
    // points behind the camera don't produce negative LOD fractions.
    return clamp(-viewPos.z, vfxCameraNearZ, vfxCameraFarZ);
}

#endif // UZ_VFX_CAMERA_HLSLI
