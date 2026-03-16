# Visual Verification Over Unit Tests for Rendering

Unit tests are NOT sufficient to validate rendering features. A test can pass while the visual result is completely wrong.

## Rules

1. Every rendering feature MUST have a demo under `demos/` or modify an existing one
2. Run the demo with `--screenshot` to capture output to `e:/UzEngine/.screenshots/`
3. View the screenshot (Read tool on the PNG) and verify visual correctness
4. Delete the screenshot after verification
5. Only after visual confirmation is the feature considered implemented
6. Use CompareGrid for multi-backend testing (--compose-grid, --cell-size 384)
7. Use --diff for pixel-by-pixel comparison between backends
8. PSNR > 40 dB = visually identical, < 30 dB = investigate

Unit tests remain useful for math utilities, data structures, and non-visual logic.
