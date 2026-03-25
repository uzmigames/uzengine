/// HDRI Environment Test — Entry Point
///
/// Loads an equirectangular .exr panorama as the sky environment for IBL.
/// Renders PBR spheres with varying metallic/roughness to showcase HDRI
/// environment reflections.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--frames <N>` — Total frames to render (0 = run until window closed)

#include "hdri_test.h"

UZ_APPLICATION_MAIN(uz::HDRITest)
