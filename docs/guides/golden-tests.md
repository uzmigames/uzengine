# Golden Image Tests — Guide
#
# This document describes how to create, run, and update golden image tests
# for the UzEngine rendering pipeline.

## Overview

Golden image tests compare rendered frames against known-good baseline images
using SSIM (Structural Similarity Index). A test passes if the SSIM score
is above a configurable threshold (default: 0.98).

## Directory Structure

```
assets/test/golden/
├── default_scene/
│   ├── baseline.png      # Known-good reference image
│   ├── current.png       # Latest capture (overwritten each run)
│   └── diff.png          # Generated on failure — visual diff
├── materials_scene/
│   ├── baseline.png
│   └── ...
```

## Prerequisites

- ImageMagick installed and `compare` (or `magick`) in PATH
- Engine built (`build/Release/render.exe`)
- MCP server running (for `uz_screenshot`)

## Running Tests

### Single Test
```powershell
.\tools\golden_test.ps1 -TestName "materials_scene" -Scene "materials.uzscene"
```

### With Custom Threshold
```powershell
.\tools\golden_test.ps1 -TestName "default_scene" -Scene "default.uzscene" -Threshold 0.95
```

### Creating a New Baseline
Run the test once without an existing baseline — the first frame is saved as
the baseline automatically:
```powershell
.\tools\golden_test.ps1 -TestName "my_new_test" -Scene "my_scene.uzscene"
# Output: "No existing baseline. Current frame saved as new baseline."
```

### Updating a Baseline
Delete the old baseline and re-run:
```powershell
Remove-Item assets\test\golden\my_test\baseline.png
.\tools\golden_test.ps1 -TestName "my_test" -Scene "my_scene.uzscene"
```

## Output Format

The script outputs structured JSON:
```json
{
  "test_name": "materials_scene",
  "scene": "materials.uzscene",
  "ssim": 0.992,
  "threshold": 0.98,
  "passed": true,
  "status": "passed",
  "baseline": "assets/test/golden/materials_scene/baseline.png",
  "current": "assets/test/golden/materials_scene/current.png",
  "message": "SSIM 0.992 >= threshold 0.98. Test PASSED."
}
```

On failure, a `diff` field points to the visual diff image.

## Integration with AI Agents

AI agents should use `tools/golden_test.ps1` after rendering fixes to verify
visual correctness. See AGENTS.md rule DEBUG-4: "Capture frame after every
rendering fix to verify."
