# PCG Editor — Procedural Content Generation Graph

> **Status**: Planned (Phase 4–5)
> **Source**: `extension/editor/pcg-editor.ts`, `engine/runtime/pcg/pcg_graph.h`
> **References**: Node Graph Framework (§40.09), Terrain System (§11.03), Foliage System (§11.04), UE5 PCG Framework, Houdini Engine

## Purpose

The PCG (Procedural Content Generation) Editor enables data-driven world building without manual placement. Designers build node graphs that transform inputs (landscape heightmaps, splines, volumes) through processing steps (scatter, filter, transform, merge) to generate outputs (static mesh instances, foliage, actors). Live preview shows results in the viewport; seed control ensures deterministic generation for iteration.

## Architecture

```
PCG Editor (TypeScript/Webview)
    │
    ├─ Graph Panel (node canvas)
    │   ├─ Input Nodes (sample landscape/spline/volume)
    │   ├─ Processing Nodes (scatter/filter/transform/merge)
    │   └─ Output Nodes (spawn instances)
    │
    ├─ Parameters Panel (right sidebar)
    │   ├─ Random seed (deterministic generation)
    │   ├─ Quality levels (100%, 50%, 25% density for preview)
    │   ├─ Bounding region (worldspace extent)
    │   └─ Preview checkbox (enable/disable live viz)
    │
    ├─ Preset Library (bottom, collapsible)
    │   ├─ Saved graphs: Forest, Village, Road, River, Cliff
    │   ├─ Load/Save/Delete buttons
    │   └─ Thumbnail preview
    │
    ├─ Viewport Preview (native HWND)
    │   ├─ Live instance visualization
    │   ├─ Bounding box of generation area
    │   ├─ Statistics (instance count, memory)
    │   └─ Real-time updates as graph changes
    │
    └─ Generate Button
        ├─ Finalize output
        ├─ Save instances to scene
        └─ Clear preview instances

Runtime: PCG Graph (.uzpcg JSON) → Expand nodes → Sample inputs → Process → Generate instances
```

## Node Types

### Input Nodes

Sample from world data:

```cpp
/// Base for input nodes (no inputs, produce data).
class PCGInputNode {
    virtual PCGData GenerateData() = 0;
};

/// Sample landscape heightmap in region.
class LandscapeHeightInputNode : public PCGInputNode {
    AssetRef terrain;          // Terrain asset reference
    Vec3 sample_center;        // World position
    float sample_radius = 50.0f;

    /// Returns heightmap samples (points with Y = height).
    PCGData GenerateData() override {
        std::vector<Vec3> points;

        // Sample terrain at grid
        for (float x = -sample_radius; x <= sample_radius; x += 2.0f) {
            for (float z = -sample_radius; z <= sample_radius; z += 2.0f) {
                float height = terrain->SampleHeight(sample_center.x + x, sample_center.z + z);
                points.push_back({sample_center.x + x, height, sample_center.z + z});
            }
        }

        return PCGData{points};
    }
};

/// Sample landscape layer (cliff, grass, water, etc.).
class LandscapeLayerInputNode : public PCGInputNode {
    AssetRef terrain;
    std::string layer_name;    // "Cliff", "Grass", "Water"
    float sample_radius = 50.0f;

    /// Returns points where layer is painted (alpha > threshold).
    PCGData GenerateData() override {
        std::vector<Vec3> points;
        float threshold = 0.5f;

        // Sample layer weight map
        for (float x = -sample_radius; x <= sample_radius; x += 2.0f) {
            for (float z = -sample_radius; z <= sample_radius; z += 2.0f) {
                float alpha = terrain->SampleLayerAlpha(layer_name, x, z);
                if (alpha > threshold) {
                    float height = terrain->SampleHeight(x, z);
                    points.push_back({x, height, z});
                }
            }
        }

        return PCGData{points};
    }
};

/// Sample from spline path.
class SplineInputNode : public PCGInputNode {
    EntityHandle spline_entity;
    float sample_distance = 1.0f;  // Sample every N meters

    PCGData GenerateData() override {
        std::vector<Vec3> points;
        Spline* spline = GetSpline(spline_entity);

        // Sample along spline curve
        for (float t = 0.0f; t <= 1.0f; t += sample_distance / spline->GetLength()) {
            points.push_back(spline->Sample(t));
        }

        return PCGData{points};
    }
};

/// Sample points inside volume.
class VolumeInputNode : public PCGInputNode {
    EntityHandle volume_entity;
    float point_density = 1.0f;  // Points per cubic meter

    PCGData GenerateData() override {
        std::vector<Vec3> points;
        BoundingVolume* volume = GetVolume(volume_entity);

        // Random points inside volume
        int point_count = (int)(volume->GetVolume() * point_density);
        for (int i = 0; i < point_count; i++) {
            Vec3 point = volume->RandomPointInside();
            points.push_back(point);
        }

        return PCGData{points};
    }
};

/// Manual point cloud input.
class PointCloudInputNode : public PCGInputNode {
    std::vector<Vec3> points;  // Painted or imported points

    PCGData GenerateData() override {
        return PCGData{points};
    }
};
```

### Processing Nodes

Transform and filter data:

```cpp
/// Base for processing nodes (input → output).
class PCGProcessNode {
    virtual PCGData Process(const PCGData& input) = 0;
};

/// Scatter: distribute points with spacing (Poisson disk sampling).
class ScatterNode : public PCGProcessNode {
    float minimum_distance = 2.0f;  // Meters between points
    int max_retries = 30;           // How hard to try

    PCGData Process(const PCGData& input) override {
        std::vector<Vec3> result;

        // Poisson disk sampling
        for (const auto& point : input.points) {
            bool valid = true;

            // Check distance to all existing points
            for (const auto& existing : result) {
                if ((existing - point).Length() < minimum_distance) {
                    valid = false;
                    break;
                }
            }

            if (valid) {
                result.push_back(point);
            }
        }

        return PCGData{result};
    }
};

/// Filter: keep points matching condition (slope, altitude, distance from something).
class FilterNode : public PCGProcessNode {
    enum class FilterType { Slope, Altitude, Distance };
    FilterType type = FilterType::Altitude;

    // Altitude filter
    float min_altitude = 0.0f;
    float max_altitude = 100.0f;

    // Slope filter
    float min_slope_degrees = 0.0f;
    float max_slope_degrees = 45.0f;

    // Distance filter
    EntityHandle target_entity;
    float min_distance = 0.0f;
    float max_distance = 50.0f;

    PCGData Process(const PCGData& input) override {
        std::vector<Vec3> result;

        for (const auto& point : input.points) {
            bool keep = false;

            switch (type) {
                case FilterType::Altitude:
                    keep = point.y >= min_altitude && point.y <= max_altitude;
                    break;

                case FilterType::Slope: {
                    float slope = EstimateSlope(point);
                    keep = slope >= min_slope_degrees && slope <= max_slope_degrees;
                    break;
                }

                case FilterType::Distance: {
                    Vec3 target_pos = GetEntityPosition(target_entity);
                    float dist = (point - target_pos).Length();
                    keep = dist >= min_distance && dist <= max_distance;
                    break;
                }
            }

            if (keep) {
                result.push_back(point);
            }
        }

        return PCGData{result};
    }

private:
    float EstimateSlope(const Vec3& point) {
        // Sample terrain normal and calculate slope angle
        Vec3 normal = terrain->GetNormal(point.x, point.z);
        float angle = acosf(normal.y);  // Y is up
        return glm::degrees(angle);
    }
};

/// Transform: apply random rotation/scale to points.
class TransformNode : public PCGProcessNode {
    bool random_rotation = false;
    float rotation_range = 360.0f;

    bool random_scale = false;
    float scale_min = 0.8f;
    float scale_max = 1.2f;

    bool height_offset = false;
    float offset_amount = 0.0f;  // Lift points above terrain

    PCGData Process(const PCGData& input) override {
        std::vector<Vec3> result;
        Random rng;

        for (const auto& point : input.points) {
            Vec3 transformed = point;

            // Random rotation around Y (for foliage)
            if (random_rotation) {
                float angle = rng.Float(0.0f, rotation_range);
                // Store rotation in metadata
                transformed.rotation_y = glm::radians(angle);
            }

            // Random scale
            if (random_scale) {
                float scale = rng.Float(scale_min, scale_max);
                transformed.scale = scale;
            }

            // Snap to terrain and offset
            if (height_offset) {
                float ground_height = terrain->SampleHeight(point.x, point.z);
                transformed.y = ground_height + offset_amount;
            }

            result.push_back(transformed);
        }

        return PCGData{result};
    }
};

/// Density Map: reduce point count by painting (e.g., "paint where trees should go").
class DensityMapNode : public PCGProcessNode {
    AssetRef density_texture;  // Grayscale texture (white = keep, black = remove)
    float threshold = 0.5f;

    PCGData Process(const PCGData& input) override {
        std::vector<Vec3> result;

        for (const auto& point : input.points) {
            // Sample texture at world position
            float alpha = density_texture->SampleAt(point.x, point.z);

            if (alpha > threshold) {
                result.push_back(point);
            }
        }

        return PCGData{result};
    }
};
```

### Output Nodes

Generate instances in the scene:

```cpp
/// Base for output nodes (consume data, spawn objects).
class PCGOutputNode {
    virtual void Generate(const PCGData& input, Scene& scene) = 0;
};

/// Spawn static mesh instances.
class MeshInstanceOutputNode : public PCGOutputNode {
    AssetRef mesh;
    AssetRef material_override;
    bool cast_shadows = true;

    void Generate(const PCGData& input, Scene& scene) override {
        for (const auto& point : input.points) {
            auto entity = scene.CreateEntity();
            entity->AddComponent<MeshComponent>(mesh);
            entity->SetPosition(point);

            if (point.rotation_y != 0) {
                entity->SetRotation(Quat::FromAxisAngle({0, 1, 0}, point.rotation_y));
            }

            if (point.scale != 1.0f) {
                entity->SetScale({point.scale, point.scale, point.scale});
            }
        }
    }
};

/// Spawn foliage (optimized batching).
class FoliageInstanceOutputNode : public PCGOutputNode {
    AssetRef foliage_asset;  // Tree, grass, bush
    int max_instances = 10000;

    void Generate(const PCGData& input, Scene& scene) override {
        // Add to foliage system (batched rendering)
        auto foliage = scene.GetOrCreateFoliageSystem();

        std::vector<FoliageInstance> instances;
        for (int i = 0; i < std::min((int)input.points.size(), max_instances); i++) {
            const auto& point = input.points[i];
            instances.push_back({
                position: point,
                rotation: Quat::FromAxisAngle({0, 1, 0}, point.rotation_y),
                scale: point.scale
            });
        }

        foliage->AddInstances(foliage_asset, instances);
    }
};

/// Spawn along spline (road, river).
class SplineMeshOutputNode : public PCGOutputNode {
    AssetRef mesh;            // Cross-section mesh
    float segment_length = 1.0f;

    void Generate(const PCGData& input, Scene& scene) override {
        // Input is a spline (line of points)
        for (int i = 0; i < input.points.size() - 1; i++) {
            Vec3 start = input.points[i];
            Vec3 end = input.points[i + 1];

            auto entity = scene.CreateEntity();
            entity->AddComponent<SplineMeshComponent>(mesh);
            entity->SetPosition(start);
            entity->LookAt(end);
        }
    }
};

/// Spawn actors (buildings, gates, etc.).
class ActorSpawnOutputNode : public PCGOutputNode {
    AssetRef actor_prefab;
    bool randomize_rotation = false;

    void Generate(const PCGData& input, Scene& scene) override {
        for (const auto& point : input.points) {
            auto entity = scene.Instantiate(actor_prefab);
            entity->SetPosition(point);

            if (randomize_rotation) {
                float angle = Random::Float(0.0f, 360.0f);
                entity->SetRotation(Quat::FromAxisAngle({0, 1, 0}, glm::radians(angle)));
            }
        }
    }
};
```

## .uzpcg File Format

PCG graphs save as JSON:

```json
{
  "name": "forest_generator",
  "description": "Scatter trees on slopes, add undergrowth, place rocks",
  "version": "1.0",
  "seed": 12345,
  "quality": 1.0,
  "bounding_region": {
    "center": [0, 0, 0],
    "extents": [100, 100, 100]
  },
  "nodes": [
    {
      "type": "LandscapeHeight",
      "id": "input_height",
      "terrain": "res://terrain/landscape_01.uzterrain",
      "sample_radius": 50.0
    },
    {
      "type": "LandscapeLayer",
      "id": "input_layer",
      "terrain": "res://terrain/landscape_01.uzterrain",
      "layer_name": "Grass"
    },
    {
      "type": "Filter",
      "id": "filter_slope",
      "type": "Slope",
      "min_slope_degrees": 10.0,
      "max_slope_degrees": 45.0
    },
    {
      "type": "Scatter",
      "id": "scatter_trees",
      "minimum_distance": 5.0
    },
    {
      "type": "Transform",
      "id": "transform_trees",
      "random_rotation": true,
      "random_scale": true,
      "scale_min": 0.9,
      "scale_max": 1.1,
      "height_offset": true,
      "offset_amount": 0.1
    },
    {
      "type": "FoliageInstanceOutput",
      "id": "output_trees",
      "foliage_asset": "res://foliage/oak_tree.uzfoliage",
      "max_instances": 5000
    }
  ],
  "connections": [
    {
      "source_node": "input_height",
      "target_node": "filter_slope"
    },
    {
      "source_node": "filter_slope",
      "target_node": "scatter_trees"
    },
    {
      "source_node": "scatter_trees",
      "target_node": "transform_trees"
    },
    {
      "source_node": "transform_trees",
      "target_node": "output_trees"
    }
  ]
}
```

## Quality Levels & Parameters

Fast iteration with quality settings:

```typescript
/// PCG preview with quality/seed controls.
export class PCGPreview {
    private seed: number = 12345;
    private quality: number = 1.0;  // 0.25 = 25%, 0.5 = 50%, 1.0 = 100%
    private isPreviewEnabled: boolean = true;

    setQuality(level: number) {
        this.quality = Math.max(0.1, Math.min(level, 1.0));
        this.regeneratePreview();
    }

    randomizeSeed() {
        this.seed = Math.floor(Math.random() * 999999);
        this.regeneratePreview();
    }

    setSeed(value: number) {
        this.seed = value;
        this.regeneratePreview();
    }

    private regeneratePreview() {
        this.ipc.call("pcg.generate_preview", [
            this.graphId,
            {
                seed: this.seed,
                quality: this.quality,
                clear_existing: true
            }
        ]);
    }

    /// Finalize: apply quality 100%, save to scene.
    async finalize() {
        await this.ipc.call("pcg.generate_final", [
            this.graphId,
            {
                seed: this.seed,
                quality: 1.0,
                save_to_scene: true
            }
        ]);

        // Hide preview instances, show actual entities
        this.clearPreview();
    }

    clearPreview() {
        this.ipc.call("pcg.clear_preview", [this.graphId]);
    }
}
```

## Preset Library

Pre-built graphs for common scenarios:

```typescript
/// PCG preset manager.
export class PCGPresetLibrary {
    private presets: Map<string, PCGPreset> = new Map();

    /// Built-in presets.
    initializeBuiltins() {
        this.presets.set('forest', {
            name: 'Forest',
            description: 'Scattered trees on natural terrain',
            graph: FOREST_GRAPH_JSON,
            thumbnail: 'builtin://presets/forest.png'
        });

        this.presets.set('village', {
            name: 'Village',
            description: 'Clustered buildings with roads',
            graph: VILLAGE_GRAPH_JSON,
            thumbnail: 'builtin://presets/village.png'
        });

        this.presets.set('river', {
            name: 'River',
            description: 'Water path with vegetation',
            graph: RIVER_GRAPH_JSON,
            thumbnail: 'builtin://presets/river.png'
        });

        this.presets.set('cliff', {
            name: 'Cliff Vegetation',
            description: 'Rocks and hardy plants on slopes',
            graph: CLIFF_GRAPH_JSON,
            thumbnail: 'builtin://presets/cliff.png'
        });

        this.presets.set('road', {
            name: 'Road Network',
            description: 'Procedural road paths',
            graph: ROAD_GRAPH_JSON,
            thumbnail: 'builtin://presets/road.png'
        });
    }

    loadPreset(presetName: string) {
        const preset = this.presets.get(presetName);
        if (!preset) throw new Error(`Preset '${presetName}' not found`);

        this.ipc.call("pcg.load_graph", [preset.graph]);
    }

    listPresets(): Array<{name: string, description: string, thumbnail: string}> {
        return Array.from(this.presets.values()).map(p => ({
            name: p.name,
            description: p.description,
            thumbnail: p.thumbnail
        }));
    }
}
```

## Statistics & Visualization

Real-time generation stats:

```typescript
/// Generation statistics overlay.
export class PCGStatsOverlay {
    private stats: {
        input_points: number,
        output_instances: number,
        generation_time_ms: number,
        memory_mb: number
    } = {input_points: 0, output_instances: 0, generation_time_ms: 0, memory_mb: 0};

    update(newStats: any) {
        this.stats = newStats;
        this.render();
    }

    private render() {
        const overlay = document.querySelector('.pcg-stats')!;
        overlay.innerHTML = `
            <div>Input Points: ${this.stats.input_points}</div>
            <div>Output Instances: ${this.stats.output_instances}</div>
            <div>Gen Time: ${this.stats.generation_time_ms.toFixed(1)}ms</div>
            <div>Memory: ${this.stats.memory_mb.toFixed(1)}MB</div>
        `;
    }
}
```

## Comparison

| Feature | UE5 (PCG Framework) | Godot (none built-in) | Houdini Engine | **UzEngine** |
|---|---|---|---|---|
| **Visual graph** | Yes, node-based | N/A | Yes (legendary) | **Yes (simplified)** |
| **Input types** | Landscape, Volume, Point Cloud, Spline | N/A | Unlimited | **5 types (essentials)** |
| **Processing** | 20+ nodes | N/A | 100+ nodes | **5 core nodes** |
| **Output types** | ISM, Foliage, Actor, Spline Mesh | N/A | Any | **Mesh, Foliage, Spline, Actor** |
| **Deterministic** | Yes (seed) | N/A | Yes | **Yes (seed control)** |
| **File format** | Binary .uasset | N/A | .hip (text) | **.uzpcg (JSON)** |
| **Presets** | Marketplace | N/A | Vast community | **Built-in presets (5+)** |

## Integration with Terrain & Foliage

PCG outputs integrate seamlessly with existing systems:

- **Terrain (§11.03)**: Input nodes sample heightmap and layer weights; FilterNode uses slope calculation from terrain normal maps.
- **Foliage (§11.04)**: FoliageInstanceOutputNode batches instances into the FoliageSystem for optimized rendering.
- **Scene Serialization**: Generated instances save to .uzscene as entities (or as foliage clusters for efficiency).

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 4** | Graph visualization, 3 input node types (Landscape Height, Spline, Volume), 3 processing nodes (Scatter, Filter by Altitude, Transform), 2 output types (Mesh, Foliage), seed/quality controls, .uzpcg serialization, live preview |
| **Phase 5** | All input types (Layer, Point Cloud), density map filtering, 5 preset templates (Forest, Village, Road, River, Cliff), statistics overlay, preset library (save/load), optimized batching |
| **Phase 5+** | Advanced nodes (merge, rasterize, conditional branching), performance optimization (GPU acceleration), cloud marketplace presets, procedural prefab composition |

