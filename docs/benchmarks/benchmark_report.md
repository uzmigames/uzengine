# UzEngine Demo Benchmark Report

**Date**: 2026-03-12
**GPU**: RTX 4090
**Resolution**: 1280x720

## Results

| Demo | Backend | FPS | Status | Screenshot |
|------|---------|-----|--------|------------|
| clear_screen | dx12 | 591 | OK | ![clear_screen_dx12](screenshots/clear_screen_dx12.png) |
| clear_screen | dx11 | 828 | OK | ![clear_screen_dx11](screenshots/clear_screen_dx11.png) |
| clear_screen | vulkan | 810 | OK | ![clear_screen_vulkan](screenshots/clear_screen_vulkan.png) |
| triangle | dx12 | — | OK | ![triangle_dx12](screenshots/triangle_dx12.png) |
| triangle | dx11 | 972 | OK | ![triangle_dx11](screenshots/triangle_dx11.png) |
| triangle | vulkan | 900 | OK | ![triangle_vulkan](screenshots/triangle_vulkan.png) |
| textured_quad | dx12 | 174 | OK | ![textured_quad_dx12](screenshots/textured_quad_dx12.png) |
| textured_quad | dx11 | 842 | OK | ![textured_quad_dx11](screenshots/textured_quad_dx11.png) |
| textured_quad | vulkan | 826 | OK | ![textured_quad_vulkan](screenshots/textured_quad_vulkan.png) |
| mesh_test | dx12 | 256 | OK | ![mesh_test_dx12](screenshots/mesh_test_dx12.png) |
| mesh_test | dx11 | 252 | OK | ![mesh_test_dx11](screenshots/mesh_test_dx11.png) |
| mesh_test | vulkan | 260 | OK | ![mesh_test_vulkan](screenshots/mesh_test_vulkan.png) |
| camera_test | dx12 | 259 | OK | ![camera_test_dx12](screenshots/camera_test_dx12.png) |
| camera_test | dx11 | 250 | OK | ![camera_test_dx11](screenshots/camera_test_dx11.png) |
| camera_test | vulkan | 261 | OK | ![camera_test_vulkan](screenshots/camera_test_vulkan.png) |

## Notes

- All demos run with `--frames 500 --screenshot-frame 10`
- Screenshots captured at frame 10
- textured_quad DX12 may show WARN due to intermittent DEVICE_REMOVED (pre-existing issue)
