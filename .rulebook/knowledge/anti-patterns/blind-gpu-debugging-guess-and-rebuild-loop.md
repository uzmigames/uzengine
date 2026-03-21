# Blind GPU Debugging (Guess-and-Rebuild Loop)

**Category**: code
**Tags**: rendering, debugging, gpu, renderdoc, pix, diagnosis

## Description

When rendering produces wrong output (black screen, wrong colors, white sky), iterating blindly by changing shader code and rebuilding without diagnostic data. CPU code can be 'correct' while GPU sees completely different data. This wasted hours on the sky atmosphere transmittance LUT — the LUT was correct but USE_TRANSMITTANCE_LUT define was never set.

## Example

// WRONG workflow:\n// 1. See wrong output\n// 2. Change shader math\n// 3. Rebuild\n// 4. Still wrong\n// 5. Change more code... (repeat 10x)\n\n// CORRECT workflow:\n// 1. CPU readback of GPU resource → verify actual values\n// 2. RenderDoc/PIX capture → inspect bindings & descriptors\n// 3. Isolate pass → render only the resource to screen\n// 4. Validate against known-good values\n// 5. Step through shader in PIX if needed (-Zi -Od)

## When to Use

When any rendering feature produces unexpected visual results. When unit tests pass but the screen output is wrong.
