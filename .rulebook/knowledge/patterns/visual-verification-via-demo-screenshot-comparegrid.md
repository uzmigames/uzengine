# Visual Verification via Demo + Screenshot + CompareGrid

**Category**: testing
**Tags**: testing, rendering, screenshot, visual-validation, compare-grid

## Description

Every rendering feature must have a demo under demos/ that can be run with --screenshot to capture output. The screenshot is viewed via Read tool to verify visual correctness. For multi-backend/multi-config testing, CompareGrid composes all screenshots into a single labeled grid image (384x384 cells). Diff mode produces 3-panel comparison (Image A | Image B | Diff Heatmap) with PSNR stats. Screenshots always go to e:/UzEngine/.screenshots/ and are deleted after verification.

## Example

# Capture\ndemo.exe --backend dx12 --screenshot e:/UzEngine/.screenshots/dx12.png\n# Compare grid\ndemo.exe --compose-grid e:/UzEngine/.screenshots/compare.png --cell-size 384 \\\n  e:/UzEngine/.screenshots/dx12.png \"DX12\" \"Frame 5\" \\\n  e:/UzEngine/.screenshots/dx11.png \"DX11\" \"Frame 5\"\n# View, verify, delete\nrm e:/UzEngine/.screenshots/*.png

## When to Use

After implementing any shader, render pass, post-process effect, or GPU pipeline change. When comparing rendering across DX12/DX11/Vulkan/Metal backends. When regression testing visual quality.

## When NOT to Use

For non-visual systems (math utilities, data structures, ECS logic) where unit tests suffice.
