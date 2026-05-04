# VFX Viewport 22-Preset Cycling Validation - Spec Delta

## ADDED Requirements

### Requirement: --start-preset CLI Flag

`vfx_viewport_app.exe` MUST accept a `--start-preset <name|index>` CLI
flag that selects which of the 22 in-code presets is active when the
demo starts. Index 0..21 OR a string matching `kPresetNames[i]`
(case-insensitive). The flag is additive to existing CLI flags and
must not break `--vfx-asset` mode (the two are mutually exclusive -
asset mode forces `m_currentPreset = -1`).

#### Scenario: --start-preset with valid index applies the preset on startup

Given the demo is launched with `--start-preset 5`
When `OnStart()` returns
Then `m_currentPreset == 5`
And the overlay status text reports `"MagicOrb"` (matching `kPresetNames[5]`)
And the standalone emitter renders with MagicOrb's visual signature within 60 frames

#### Scenario: --start-preset with valid name applies the preset on startup

Given the demo is launched with `--start-preset Tornado`
When `OnStart()` returns
Then `m_currentPreset == 15` (or whichever index matches "Tornado" in `kPresetNames`)
And the overlay status text reports `"Tornado"`

#### Scenario: --start-preset with invalid value falls back to default

Given the demo is launched with `--start-preset NotARealPreset` or `--start-preset 999`
When `OnStart()` runs
Then a `[App] [warning]` log line reports the invalid preset value
And the demo defaults to preset index 1 (`Fire`) as if the flag was not passed
And the demo continues startup normally

### Requirement: Baseline Screenshot for Every Preset

There MUST be a baseline screenshot stored at
`e:/UzmiEngine/uzengine/.sandbox/preset-baseline/<NN>-<name>.png` for
every preset index `NN` in `00..21`. Each baseline MUST be:
- Captured at frame 120 (steady-state with multiple particles).
- A valid PNG decodable by the `Read` tool.
- NOT fully black (file size > 50 KB, or PSNR vs all-black PNG > 5 dB).
- NOT camera-engulfed (no single uniform color covering > 90% of the
  viewport, validated by histogram inspection).

#### Scenario: Capture script produces 22 valid baselines

Given `scripts/vfx_validate_presets.ps1 -Mode capture` is executed from a clean state
When the script completes
Then 22 PNG files exist under `.sandbox/preset-baseline/`
And `manifest.json` lists all 22 with their sha256 hashes and byte sizes
And no file in the manifest is smaller than 50 KB

### Requirement: Validation Script Detects Regression

`scripts/vfx_validate_presets.ps1 -Mode validate` MUST recapture all
22 presets, PSNR-diff each against its baseline, and report fail for
any preset whose PSNR drops below 35 dB. Exit code 0 = all pass,
non-zero = at least one fail.

#### Scenario: Unchanged build produces 22/22 pass

Given the baseline manifest matches the current build's output exactly
When `scripts/vfx_validate_presets.ps1 -Mode validate` runs
Then the script reports 22/22 pass
And the exit code is 0

#### Scenario: Smoke preset color regression triggers fail

Given the baseline was captured with the corrected Smoke color
And a regression is introduced that re-breaks Smoke's color-over-life
When `-Mode validate` runs
Then the script reports `Smoke: FAIL (PSNR <35dB)` in the summary
And the exit code is non-zero
