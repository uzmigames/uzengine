# GPU Debugging — Never Guess, Always Diagnose

When rendering produces wrong output, follow this exact diagnostic order:

1. **CPU Readback First** — read back GPU resource to CPU, log actual values
2. **Use RenderDoc/PIX** — capture frame, inspect shader bindings, CB values, UAV output
3. **Isolate the Pass** — render ONLY the resource under test to screen
4. **Validate Against Known-Good Values** — compare with mathematically predictable values

## Required Infrastructure

- PIX Event Markers on every render/compute pass
- Debug readback path (cvar-gated) for every compute LUT
- Debug shader compilation with `-Zi -Od` flags
- DX12 Debug Layer + GPU-Based Validation in debug builds
- DRED Breadcrumbs for device-removed crashes

## NEVER

- Iterate blindly by changing shader code and hoping it works
- Guess at parameter values to "fix" visual bugs
- Add compensation factors to make wrong output look right
