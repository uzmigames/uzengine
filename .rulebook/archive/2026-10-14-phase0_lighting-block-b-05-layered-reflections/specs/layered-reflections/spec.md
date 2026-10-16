# Layered Reflections

## Purpose
Screen-space reflections find what is on screen, are blended before the BRDF over the SDF or IBL
reflection layer, and the additive post-TAA composite is retired.

## ADDED Requirements

### Requirement: The Hi-Z pyramid holds the frame's depth
The Hi-Z pyramid SHALL be built from the opaque depth after the GBuffer pass, so that every
consumer (SSR, SSGI, GTAO, GPU culling) reads the frame's depth.

#### Scenario: Trace samples
Given a frame of the mirror reference scene
When the trace samples the pyramid's mip 0 and mip 1 at a pixel
Then the samples equal the depth there (diagnostic build), and the trace finds hits.

### Requirement: The SSR chain runs in order
The SSR passes SHALL declare every graph resource they read, so the render graph keeps
trace → resolve → temporal → denoise in that order, and the previous-colour copy runs after TAA.

#### Scenario: Full chain
Given the mirror reference scene with every SSR pass enabled
When the pass-inspector targets are dumped at frame 20
Then the resolve and temporal targets carry the trace's hits, and the hits' colours are the scene's.

### Requirement: SSR blended before the BRDF over the active layer
The deferred lighting SHALL blend the screen-space reflection, weighted by its confidence, into
the reflected radiance of the active layer (SDF gather or prefiltered IBL) before the BRDF terms,
and, where the environment-MIS estimate replaces the specular, over that estimate with the
single-scatter Fresnel weight; the post-TAA additive composite SHALL be off by default.

#### Scenario: Mirror floor
Given the mirror reference scene and its mirrored camera
When floor pixels whose reflected hit is on screen are compared with F × the mirrored view
Then the median relative error is at most 0.15 with SSR on, at least twice that with SSR off,
and the legacy composite over-brightens them by at least 1.5 × while the default stays within
[0.85, 1.15].

#### Scenario: Temporal stability
Given frames 90-99 of the mirror reference scene
When the frame-to-frame luminance change over the floor is measured
Then with SSR on it is at most 3 × the SSR-off value plus 0.002.

#### Scenario: Sponza unchanged with SSR off
Given Sponza's project (SSR off)
When frame 60 is compared with the committed build's capture
Then direct_light and taa_output differ by at most 1e-6.

#### Scenario: Cost
Given 1920 × 1080
When the SSR passes run in the reference scene and in Sponza
Then they take at most 3.0 ms.
