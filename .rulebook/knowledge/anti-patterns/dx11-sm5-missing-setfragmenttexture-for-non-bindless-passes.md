# DX11 SM5 Missing SetFragmentTexture for Non-Bindless Passes

**Category**: rhi
**Tags**: dx11, sm5, bindless, SetFragmentTexture, sky, tonemap, ibl, black-screen

## Description

When a render pass binds textures via descriptor index (SM6.6 bindless on DX12), the DX11 SM5 path must use explicit SetFragmentTexture(slot, handle) calls instead. If the SM5 path is forgotten, the shader reads from unbound SRV slots — returning zeros. Common symptoms: black sky (transmittance LUT not bound at t1 in sky atmosphere pass), black tonemap output (HDR scene color not bound at t0), invisible IBL (irradiance/prefiltered not bound at t5-t7).

## Example

// WRONG: Only bindless path implemented
cmd->SetRootConstants(0, &srvIndex, 1); // SM6.6 only — DX11 ignores this

// CORRECT: Both paths
if (bindless) {
    cmd->SetRootConstants(0, &srvIndex, 1);
} else {
    cmd->SetFragmentTexture(0, textureHandle); // SM5 explicit binding
}

// Cases found in UzEngine:
// 1. Tonemap: missing SetFragmentTexture(0, m_hdrSceneColor)
// 2. Sky atmosphere: missing SetFragmentTexture(1, transmittanceLutTexture)
// 3. Lighting: missing SetFragmentTexture(5-7, iblTextures)

## When to Use

Every render pass that reads textures via shaders. When adding a new pass or texture binding, always implement both the bindless (SM6.6) and explicit (SM5) paths.

## When NOT to Use

Compute-only passes that use SetDescriptorTable for UAV binding — those have a different binding model.
