# Sponza Intel parity — scene calibration values and GI binding traps
**Source**: manual
**Date**: 2026-08-31
**Related Task**: phase1_fix-sponza-intel-lighting-parity
Sponza Intel parity analysis (2026-08-31): sponza.uzscene now uses sun rotation [-56,79,0] (Intel glTF SUN node: elevation 55.9°, azimuth ~79° in engine coords; engine convention pitch-about-Y/yaw-about-Z, forward +X, to-sun = R·Forward). Sun/sky scale unified via sky_atmosphere sun_illuminance [10,10,10] matching light intensity 10. Key trap: scene_loader silently dropped directional light keys before the fix; SSGI/DDGI/RC have "bound but never written" resource patterns — always gate binding on a first-produced-output flag, not IsInitialized. r.SSGI master cvar defaults FALSE — setting only r.SSGI.Intensity is a dead knob.