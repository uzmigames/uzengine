# Visual Verification Over Unit Tests for Rendering

Unit tests are NOT sufficient to validate rendering features. A test can pass while the visual result is completely wrong.

## Rules

1. Every rendering feature MUST have a demo under `demos/` or modify an existing one
2. **PREFER `--screenshot-stdout`** over `--screenshot <path>` — prints base64 PNG to stdout, no disk I/O needed
3. View the screenshot (Read tool on PNG, or parse base64 from stdout) and verify visual correctness
4. Delete any disk screenshots after verification
5. Only after visual confirmation is the feature considered implemented
6. Use CompareGrid for multi-backend testing (--compose-grid, --cell-size 384)
7. Use --diff for pixel-by-pixel comparison between backends
8. PSNR > 40 dB = visually identical, < 30 dB = investigate

## Screenshot Methods (fastest to slowest)

### Method 1: Base64 stdout (PREFERRED for LLM workflows)
```bash
demo.exe --screenshot-stdout --screenshot-frame 5 --frames 8
# Implies --headless (no window shown) + suppresses console logs
# Output: SCREENSHOT_BASE64:iVBORw0KGgo...
# Parse the base64 string after the prefix. No disk I/O.
```

### Method 2: Headless + file to disk
```bash
demo.exe --headless --screenshot e:/UzEngine/.screenshots/output.png --screenshot-frame 5 --frames 8
# Window created but hidden. Render works normally.
# Then: Read tool on the PNG file
# Then: rm e:/UzEngine/.screenshots/output.png
```

### Method 3: Visible window + file to disk (interactive debugging)
```bash
demo.exe --screenshot e:/UzEngine/.screenshots/output.png --screenshot-frame 5 --frames 8
# Window visible. Useful when you need to see the render live.
```

### Headless Mode
`--headless` creates the HWND (required by DX12 swapchain) but never calls `ShowWindow`. The render pipeline works identically — just no GUI appears. Use for:
- CI/CD automated testing
- LLM-driven visual verification (no human interaction needed)
- Batch rendering (multiple screenshots in sequence)

Unit tests remain useful for math utilities, data structures, and non-visual logic.
