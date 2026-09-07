# Sponza graph-material export evidence

Inspected 2026-09-06. Source audit uses Node.js built-in fs/path to read the prefab material_graph references and their authored JSON. Runtime build/scene validation is performed separately by the parent task.

## Confirmed integration gap

- The prefab uses material_graph references. Before this fix, scene_sync.cpp filled materialGraphPipeline and materialTexIndices for those graphs, but left ordinary PBR draw fields at fallback values whenever no MaterialInterface was attached.
- MaterialGraph lacked blendMode/twoSided storage. FromJson only read domain and shadingModel, discarding the authored Translucent/twoSided keys; ToJson omitted them as well.
- GDF reads ordinary resolved draw metadata. Consequently the graph-only scene supplied fallback white diffuse reflectance and incorrectly included translucent glass among solid occluders.

## Authored input counts

```json
{
  "materials": 28,
  "instances": 138,
  "blendModes": {
    "Opaque": 26,
    "Translucent": 1,
    "Masked": 1
  },
  "twoSided": 0,
  "directBaseColorTextures": 25,
  "metallicBlueTextures": 24
}
```

## Exact authored graph examples

```json
[
  {
    "file": "materials/sponza/glass.uzmat",
    "blend": "Translucent",
    "twoSided": false,
    "params": {
      "roughnessMultiplier": 1,
      "metallicMultiplier": 1,
      "baseColorTint": [
        1,
        1,
        1
      ],
      "normalStrength": 1,
      "aoMultiplier": 1,
      "emissiveMultiplier": 1,
      "specularMultiplier": 1
    },
    "links": [
      {
        "input": "Emissive",
        "sourceType": "Vector3Constant",
        "sourcePin": 0,
        "constants": {
          "x": 8,
          "y": 6.5,
          "z": 4.5
        }
      }
    ]
  },
  {
    "file": "materials/sponza/floor_01.uzmat",
    "blend": "Opaque",
    "twoSided": false,
    "params": {
      "roughnessMultiplier": 1,
      "metallicMultiplier": 1,
      "baseColorTint": [
        1,
        1,
        1
      ],
      "normalStrength": 1,
      "aoMultiplier": 1,
      "emissiveMultiplier": 1,
      "specularMultiplier": 1
    },
    "links": [
      {
        "input": "BaseColor",
        "sourceType": "TextureSample",
        "sourcePin": "RGBA",
        "path": "textures/sponza/floor_01_baseColor.png"
      },
      {
        "input": "Metallic",
        "sourceType": "TextureSample",
        "sourcePin": "B",
        "path": "textures/sponza/floor_01_metallicRoughness.png"
      },
      {
        "input": "Roughness",
        "sourceType": "TextureSample",
        "sourcePin": "G",
        "path": "textures/sponza/floor_01_metallicRoughness.png"
      },
      {
        "input": "Normal",
        "sourceType": "TextureSample",
        "sourcePin": "RGBA",
        "path": "textures/sponza/floor_01_normal.png"
      }
    ]
  }
]
```

## Correction and scope

- MaterialGraph now preserves typed blend mode and two-sided metadata through parse, serialization and content hashing. Added existing-suite regression MaterialGraphSerialization.BlendModeAndTwoSidedRoundTripAndHash.
- scene_sync exports direct TextureSample links using PBROutput input semantic and connected output channel, not filenames or node display names. Metallic B links select packed ORM; albedo uses its color texture; direct constants and canonical defaults preserve tint, opacity and emission.
- Forward translucent emission uses the authored constant and graph multiplier. Opaque compiled graph emission uses neutral outer factors because the compiled graph already evaluates and multiplies its own emission.
- This is metadata extraction for simple direct texture/constant connections. It does not replace evaluation of arbitrary material graphs. GDF material values remain a coarsest-mip mesh average, not a UV-resolved surface cache.
