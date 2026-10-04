# Sponza regression fixtures

`gdf_expanded_before_raw.h` freezes the CPU-translated production GDF compositor immediately before the raw-distance output was added during the 2026-09-07 lighting investigation. It was preserved from `test_gdf_raw_composite_artifacts/before.generated.h`.

`test_gdf_raw_composite.ps1` extracts the current production compositor and always compares 200 deterministic cases against this reference. The reference replaces GPU resource access with the same test adapters used for the current function; material reflectance is supplied by the fixture.

Keep this file versioned. Do not regenerate it from current code or make the comparisons conditional on ignored local artifacts.
