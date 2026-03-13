# Editor Testing — Automated UI & Integration Tests

> **Status**: Planned (Phase 3)
> **Source**: `extension/tests/`, `runtime/editor/tests/`, `tests/editor/`
> **References**: Playwright (browser automation), VSCode Testing Extension, Godot test framework

## Purpose

Automated editor testing ensures UI stability, viewport interaction, undo/redo correctness, and performance. Tests cover webview panels (Playwright), viewport interactions (simulated input), and asset workflows.

## Test Categories

### 1. Webview Panel Tests (Playwright)

Test HTML/CSS/TypeScript UI panels running inside VSCode webviews.

```typescript
// extension/tests/inspector.test.ts

import { describe, it, expect } from '@jest/globals';
import { chromium, Browser, Page } from 'playwright';

describe('Inspector Panel', () => {
    let browser: Browser;
    let page: Page;

    beforeAll(async () => {
        // Launch VSCode with extension
        browser = await chromium.launch({
            args: ['--disable-gpu', '--disable-dev-shm-usage'],
        });
        page = await browser.newPage();

        // Load VSCode extension test environment
        // (This would require VSCode's test extension runner)
    });

    afterAll(async () => {
        await browser.close();
    });

    it('should display component properties when entity selected', async () => {
        // Simulate selecting entity in hierarchy
        await page.click('[data-testid="hierarchy-item-player"]');

        // Inspector panel should update
        const inspectorPanel = await page.waitForSelector('[data-testid="inspector-panel"]');
        expect(inspectorPanel).toBeDefined();

        // Verify component is visible
        const meshRenderer = await page.$('[data-testid="component-MeshRenderer"]');
        expect(meshRenderer).toBeDefined();
    });

    it('should allow editing component properties', async () => {
        // Select entity
        await page.click('[data-testid="hierarchy-item-player"]');

        // Find color property input
        const colorInput = await page.$('[data-testid="property-color"]');
        expect(colorInput).toBeDefined();

        // Change color
        await colorInput!.fill('ff0000');  // Red
        await page.keyboard.press('Enter');

        // Verify color changed in viewport (via screenshot comparison)
        const viewport = await page.screenshot({ path: '/tmp/after_color_change.png' });
        expect(viewport).toBeDefined();
    });

    it('should display enum properties as dropdown', async () => {
        // Select entity with enum property
        await page.click('[data-testid="hierarchy-item-object-with-enum"]');

        // Find enum dropdown
        const enumDropdown = await page.$('[data-testid="property-enum-render-mode"]');
        expect(enumDropdown).toBeDefined();

        // Change selection
        await enumDropdown!.selectOption('wireframe');

        // Verify change propagated
        const value = await enumDropdown!.inputValue();
        expect(value).toBe('wireframe');
    });

    it('should show validation errors for invalid values', async () => {
        const numberInput = await page.$('[data-testid="property-speed"]');

        // Enter invalid value (negative for speed)
        await numberInput!.fill('-5');
        await page.keyboard.press('Enter');

        // Error should appear
        const error = await page.waitForSelector('[data-testid="property-error"]');
        const errorText = await error!.textContent();
        expect(errorText).toContain('must be positive');
    });

    it('should support undo for property changes', async () => {
        // Change property
        await page.click('[data-testid="property-health"]');
        const input = await page.$('[data-testid="property-health-input"]');
        await input!.fill('100');
        await page.keyboard.press('Enter');

        // Undo
        await page.keyboard.press('Control+Z');

        // Property should revert
        const value = await input!.inputValue();
        expect(value).not.toBe('100');
    });
});
```

### 2. Hierarchy Panel Tests

```typescript
// extension/tests/hierarchy.test.ts

describe('Hierarchy Panel', () => {
    it('should display all entities in scene', async () => {
        const items = await page.$$('[data-testid^="hierarchy-item-"]');
        expect(items.length).toBeGreaterThan(0);
    });

    it('should support drag-and-drop reparenting', async () => {
        // Get bounding boxes
        const parentBox = await page.boundingBox('[data-testid="hierarchy-item-player"]');
        const childBox = await page.boundingBox('[data-testid="hierarchy-item-weapon"]');

        // Drag weapon onto player
        await page.dragAndDrop(
            '[data-testid="hierarchy-item-weapon"]',
            '[data-testid="hierarchy-item-player"]'
        );

        // Verify hierarchy changed
        const parent = await page.$('[data-testid="hierarchy-item-player"]');
        const children = await parent!.$$('[data-testid^="hierarchy-item-"]');
        expect(children.length).toBeGreaterThan(0);
    });

    it('should create entity with context menu', async () => {
        // Right-click in empty space
        await page.rightClick('[data-testid="hierarchy-panel"]');

        // Click "Create Entity"
        await page.click('[data-testid="context-menu-create-entity"]');

        // Dialog should appear
        const dialog = await page.waitForSelector('[data-testid="create-entity-dialog"]');
        expect(dialog).toBeDefined();

        // Enter name
        const input = await page.$('[data-testid="entity-name-input"]');
        await input!.fill('NewEntity');

        // Create
        await page.click('[data-testid="create-button"]');

        // Entity should appear in hierarchy
        const newEntity = await page.waitForSelector(
            '[data-testid="hierarchy-item-NewEntity"]'
        );
        expect(newEntity).toBeDefined();
    });

    it('should delete entity with keyboard shortcut', async () => {
        // Select entity
        await page.click('[data-testid="hierarchy-item-to-delete"]');

        // Press Delete
        await page.keyboard.press('Delete');

        // Confirmation dialog
        const dialog = await page.waitForSelector('[data-testid="confirm-delete-dialog"]');
        await page.click('[data-testid="confirm-delete-button"]');

        // Entity should be gone
        const entity = await page.$('[data-testid="hierarchy-item-to-delete"]');
        expect(entity).toBeNull();
    });

    it('should rename entity inline', async () => {
        // Double-click entity name
        const item = await page.$('[data-testid="hierarchy-item-player"]');
        await item!.dblclick();

        // Should enter edit mode
        const input = await page.waitForSelector('[data-testid="hierarchy-item-player-edit"]');
        expect(input).toBeDefined();

        // Change name
        await input!.fill('MainCharacter');
        await page.keyboard.press('Enter');

        // Verify change
        const label = await page.$('[data-testid="hierarchy-item-label-MainCharacter"]');
        expect(label).toBeDefined();
    });
});
```

### 3. Viewport Interaction Tests

Simulate mouse/keyboard input in the native viewport (HWND).

```cpp
// runtime/editor/tests/viewport_test.h

#include <gtest/gtest.h>
#include "editor/viewport.h"
#include "scene/scene.h"
#include "entity/entity.h"

class ViewportInteractionTest : public ::testing::Test {
protected:
    void SetUp() override {
        scene_ = std::make_unique<Scene>();
        viewport_ = std::make_unique<EditorViewport>();
        viewport_->SetScene(*scene_);
    }

    std::unique_ptr<Scene> scene_;
    std::unique_ptr<EditorViewport> viewport_;

    // Helper: create test entity
    Entity CreateTestEntity(Vec3 position = Vec3::Zero()) {
        Entity e = scene_->Create();
        e.Add<NameComponent>("TestEntity");
        auto* transform = e.Add<TransformComponent>();
        transform->position = position;
        return e;
    }

    // Helper: simulate mouse click
    void SimulateMouseClick(int x, int y, MouseButton button = MouseButton::Left) {
        // Convert screen coords to viewport ray
        Ray ray = viewport_->ScreenPointToRay(x, y);
        viewport_->OnMouseClick(ray, button);
    }

    // Helper: simulate mouse drag
    void SimulateMouseDrag(int x_start, int y_start, int x_end, int y_end) {
        Ray ray_start = viewport_->ScreenPointToRay(x_start, y_start);
        Ray ray_end = viewport_->ScreenPointToRay(x_end, y_end);
        viewport_->OnMouseDragStart(ray_start);

        // Simulate intermediate points
        for (int i = 0; i < 10; ++i) {
            float t = i / 10.0f;
            Ray ray = Ray::Lerp(ray_start, ray_end, t);
            viewport_->OnMouseDragMove(ray);
        }

        viewport_->OnMouseDragEnd(ray_end);
    }
};

TEST_F(ViewportInteractionTest, SelectEntityByClick) {
    auto entity = CreateTestEntity();

    // Simulate click on entity
    SimulateMouseClick(200, 150);

    // Entity should be selected
    auto selection = viewport_->GetSelection();
    EXPECT_EQ(selection.size(), 1);
    EXPECT_EQ(selection[0].GetId(), entity.GetId());
}

TEST_F(ViewportInteractionTest, DragTranslateGizmo) {
    auto entity = CreateTestEntity(Vec3(0, 0, 0));
    viewport_->SelectEntity(entity);

    // Simulate drag on translate gizmo
    SimulateMouseDrag(300, 200, 400, 200);  // Drag right

    // Entity should have moved
    auto* transform = entity.Get<TransformComponent>();
    EXPECT_GT(transform->position.x, 0.0f);
}

TEST_F(ViewportInteractionTest, MultiSelectWithCtrlClick) {
    auto entity1 = CreateTestEntity(Vec3(0, 0, 0));
    auto entity2 = CreateTestEntity(Vec3(5, 0, 0));

    // Click first entity
    viewport_->SelectEntity(entity1);
    EXPECT_EQ(viewport_->GetSelection().size(), 1);

    // Ctrl+click second entity
    viewport_->SetMultiSelectMode(true);
    viewport_->OnMouseClick(Ray(), MouseButton::Left);  // This should be on entity2
    viewport_->SetMultiSelectMode(false);

    // Both should be selected
    auto selection = viewport_->GetSelection();
    EXPECT_EQ(selection.size(), 2);
}

TEST_F(ViewportInteractionTest, FocusSelectedEntity) {
    auto entity = CreateTestEntity(Vec3(10, 10, 10));
    viewport_->SelectEntity(entity);

    Vec3 camera_pos_before = viewport_->GetCamera().position;

    // Simulate focus key
    viewport_->OnKeyPress(Key::F);

    Vec3 camera_pos_after = viewport_->GetCamera().position;

    // Camera should have moved toward entity
    EXPECT_NE(camera_pos_before, camera_pos_after);
}

TEST_F(ViewportInteractionTest, ChangeViewportMode) {
    // Start in Lit mode
    EXPECT_EQ(viewport_->GetCurrentMode(), ViewportMode::Lit);

    // Switch to Wireframe
    viewport_->SetMode(ViewportMode::Wireframe);

    EXPECT_EQ(viewport_->GetCurrentMode(), ViewportMode::Wireframe);
}

TEST_F(ViewportInteractionTest, GridSnapConstrainsMovement) {
    auto entity = CreateTestEntity(Vec3(0, 0, 0));
    viewport_->SelectEntity(entity);
    viewport_->GetPreferences().SetGridSnapEnabled(true);
    viewport_->GetPreferences().SetGridSnapSize(0.5f);

    // Drag to non-grid position
    SimulateMouseDrag(300, 200, 337, 200);  // Drag ~1.2 units

    auto* transform = entity.Get<TransformComponent>();

    // Position should snap to 0.5 unit grid
    float remainder_x = std::fmod(transform->position.x, 0.5f);
    EXPECT_LT(remainder_x, 0.01f);  // Allow small float error
}
```

### 4. Undo/Redo Stress Test

```cpp
// runtime/editor/tests/undo_redo_stress_test.h

TEST_F(EditorTest, UndoRedoStressTest) {
    // Create 100 entities
    std::vector<Entity> entities;
    for (int i = 0; i < 100; ++i) {
        entities.push_back(CreateTestEntity());
    }

    UndoStack undo_stack;

    // Perform 1000 random operations
    const int kNumOperations = 1000;
    for (int i = 0; i < kNumOperations; ++i) {
        int op = rand() % 3;
        Entity target = entities[rand() % entities.size()];

        switch (op) {
            case 0: {
                // Move entity
                auto* transform = target.Get<TransformComponent>();
                Transform before = transform->Get();
                transform->position += Vec3(1, 0, 0);
                Transform after = transform->Get();

                auto cmd = std::make_unique<TransformCommand>(target, before, after);
                undo_stack.ExecuteCommand(std::move(cmd));
                break;
            }
            case 1: {
                // Scale entity
                auto* transform = target.Get<TransformComponent>();
                Transform before = transform->Get();
                transform->scale *= 1.1f;
                Transform after = transform->Get();

                auto cmd = std::make_unique<TransformCommand>(target, before, after);
                undo_stack.ExecuteCommand(std::move(cmd));
                break;
            }
            case 2: {
                // Undo if possible
                if (!undo_stack.Undo()) {
                    undo_stack.Redo();
                }
                break;
            }
        }
    }

    // Undo all the way
    int undo_count = 0;
    while (undo_stack.Undo()) {
        undo_count++;
    }

    // Should have many operations to undo
    EXPECT_GT(undo_count, 100);

    // Redo all the way
    int redo_count = 0;
    while (undo_stack.Redo()) {
        redo_count++;
    }

    // Redo count should match undo count
    EXPECT_EQ(redo_count, undo_count);
}
```

### 5. Screenshot Regression Testing

Capture viewport and compare with golden images to detect visual regressions.

```cpp
// runtime/editor/tests/screenshot_regression_test.h

class ScreenshotRegressionTest : public ::testing::Test {
protected:
    /// Render viewport and capture as PNG.
    void CaptureScreenshot(StringView test_name, std::vector<uint8_t>& out_png) {
        viewport_->Render();

        // Read backbuffer (assuming RGBA8)
        auto backbuffer = viewport_->GetBackbuffer();
        std::vector<uint8_t> pixels = backbuffer.ReadPixels();

        // Encode to PNG
        EncodePNG(pixels, backbuffer.GetWidth(), backbuffer.GetHeight(), out_png);
    }

    /// Compare two PNG images, return diff percentage.
    float CompareImages(const std::vector<uint8_t>& expected_png,
                       const std::vector<uint8_t>& actual_png) {
        // Decode both images
        auto expected = DecodePNG(expected_png);
        auto actual = DecodePNG(actual_png);

        // Compare pixels
        if (expected.size() != actual.size()) {
            return 100.0f;  // Completely different
        }

        uint32_t different = 0;
        for (size_t i = 0; i < expected.size(); ++i) {
            if (expected[i] != actual[i]) {
                different++;
            }
        }

        return (float)different / expected.size() * 100.0f;
    }

    /// Golden image path.
    fs::path GetGoldenImagePath(StringView test_name) {
        return fs::path("tests/editor/golden_images/") / (std::string(test_name) + ".png");
    }
};

TEST_F(ScreenshotRegressionTest, LitModeMaterial) {
    // Load test scene
    scene_->Load("res://test_scenes/material_showcase.uzscene");
    viewport_->SetMode(ViewportMode::Lit);

    // Capture screenshot
    std::vector<uint8_t> actual_png;
    CaptureScreenshot("lit_mode_material", actual_png);

    // Load golden image
    auto golden_path = GetGoldenImagePath("lit_mode_material");
    std::vector<uint8_t> expected_png = ReadFile(golden_path);

    // Compare
    float diff = CompareImages(expected_png, actual_png);
    EXPECT_LT(diff, 1.0f) << "Screenshot differs by " << diff << "%";
}

TEST_F(ScreenshotRegressionTest, WireframeMode) {
    scene_->Load("res://test_scenes/complex_mesh.uzscene");
    viewport_->SetMode(ViewportMode::Wireframe);

    std::vector<uint8_t> actual_png;
    CaptureScreenshot("wireframe_mode", actual_png);

    auto golden_path = GetGoldenImagePath("wireframe_mode");
    std::vector<uint8_t> expected_png = ReadFile(golden_path);

    float diff = CompareImages(expected_png, actual_png);
    EXPECT_LT(diff, 1.0f);
}

TEST_F(ScreenshotRegressionTest, NormalVisualization) {
    scene_->Load("res://test_scenes/normal_maps.uzscene");
    viewport_->SetMode(ViewportMode::Normals);

    std::vector<uint8_t> actual_png;
    CaptureScreenshot("normal_visualization", actual_png);

    auto golden_path = GetGoldenImagePath("normal_visualization");
    std::vector<uint8_t> expected_png = ReadFile(golden_path);

    float diff = CompareImages(expected_png, actual_png);
    EXPECT_LT(diff, 1.0f);
}
```

### 6. Performance Benchmarks

```cpp
// runtime/editor/tests/editor_performance_test.h

class EditorPerformanceBenchmark : public ::testing::Test {
protected:
    const int kEntityCount = 10000;
    const int kFrames = 120;

    void SetUp() override {
        scene_ = std::make_unique<Scene>();
        viewport_ = std::make_unique<EditorViewport>();

        // Create 10k entities
        for (int i = 0; i < kEntityCount; ++i) {
            Entity e = scene_->Create();
            e.Add<TransformComponent>();
            e.Add<MeshComponent>("res://meshes/cube.uzmesh");
        }
    }
};

TEST_F(EditorPerformanceBenchmark, ViewportFrametimeWith10KEntities) {
    std::vector<float> frame_times;
    frame_times.reserve(kFrames);

    for (int i = 0; i < kFrames; ++i) {
        auto start = std::chrono::high_resolution_clock::now();

        viewport_->Update(16.67f);  // 60 FPS
        viewport_->Render();

        auto end = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(end - start).count();
        frame_times.push_back(ms);
    }

    // Calculate stats
    float avg = std::accumulate(frame_times.begin(), frame_times.end(), 0.0f) / frame_times.size();
    float max = *std::max_element(frame_times.begin(), frame_times.end());

    // Expect reasonable performance
    EXPECT_LT(avg, 5.0f) << "Average frametime > 5ms";
    EXPECT_LT(max, 10.0f) << "Max frametime > 10ms";
}

TEST_F(EditorPerformanceBenchmark, GizmoDragLatency) {
    // Select entity
    Entity e = scene_->Get(0);
    viewport_->SelectEntity(e);

    std::vector<float> drag_latencies;

    // Simulate 100 gizmo drag updates
    for (int i = 0; i < 100; ++i) {
        auto start = std::chrono::high_resolution_clock::now();

        // Simulate drag delta
        viewport_->UpdateGizmoDrag(Vec3(0.1f, 0, 0));

        auto end = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(end - start).count();
        drag_latencies.push_back(ms);
    }

    float avg = std::accumulate(drag_latencies.begin(), drag_latencies.end(), 0.0f) /
                drag_latencies.size();

    // Gizmo drag should be fast (< 1ms)
    EXPECT_LT(avg, 1.0f) << "Average gizmo drag latency > 1ms";
}

TEST_F(EditorPerformanceBenchmark, SelectionHighlightPerformance) {
    // Select many entities
    std::vector<Entity> selected;
    for (int i = 0; i < 1000; ++i) {
        selected.push_back(scene_->Get(i));
    }

    auto start = std::chrono::high_resolution_clock::now();

    viewport_->SelectEntities(selected);

    auto end = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(end - start).count();

    // Selecting 1000 entities should be fast
    EXPECT_LT(ms, 10.0f) << "Multi-selection took > 10ms";
}
```

## Test Data Management

Store test scenes and golden images in version control:

```
tests/
├── editor/
│   ├── test_scenes/
│   │   ├── empty_scene.uzscene
│   │   ├── simple_entities.uzscene
│   │   ├── complex_mesh.uzscene
│   │   ├── material_showcase.uzscene
│   │   ├── normal_maps.uzscene
│   │   └── 10k_entities.uzscene
│   ├── golden_images/
│   │   ├── lit_mode_material.png
│   │   ├── wireframe_mode.png
│   │   ├── normal_visualization.png
│   │   └── ...
│   └── test_*.h / test_*.ts (test files)
```

## CI Pipeline Integration

```yaml
# .github/workflows/editor-tests.yml

name: Editor Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: windows-latest

    steps:
      - uses: actions/checkout@v3

      - name: Setup VSCode for testing
        run: |
          # Download VSCode in headless mode
          choco install vscode

      - name: Build runtime
        run: |
          zig build

      - name: Run C++ editor tests
        run: |
          zig build test
          ./zig-out/bin/editor_tests.exe

      - name: Run TypeScript extension tests
        run: |
          npm install
          npm run test:extension

      - name: Run viewport integration tests
        run: |
          ./zig-out/bin/editor_viewport_tests.exe

      - name: Generate coverage report
        run: |
          npm run coverage
          ./zig-out/bin/editor_tests.exe --coverage

      - name: Upload coverage
        uses: codecov/codecov-action@v3
        with:
          files: ./coverage/coverage.xml

      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v3
        with:
          name: test-results
          path: test-results/
```

## Test Coverage Goals

| Component | Target Coverage | Type |
|-----------|-----------------|------|
| **Inspector Panel** | 90% | UI (Playwright) + Unit |
| **Hierarchy Panel** | 85% | UI (Playwright) + Unit |
| **Viewport** | 85% | Integration + Performance |
| **Undo/Redo** | 95% | Unit + Stress |
| **Gizmo** | 80% | Integration + Performance |
| **IPC Handler** | 90% | Unit + Integration |
| **Preferences** | 85% | Unit |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Basic viewport interaction tests; Inspector UI tests (Playwright); Undo/redo correctness tests; Gizmo drag tests; Screenshot regression testing (golden images) |
| **Phase 4** | Extended Playwright test suite (all panels); Performance benchmarks (frame time, selection, multi-edit); Stress tests (undo with 10k entities); CI/CD integration with coverage reporting; Load/save scene tests |

