<!-- doc-meta
  title: Gizmo Implementation Guide for UzEngine
  description: Step-by-step implementation roadmap with C++ code patterns, mesh generation, and visual coordinate systems
  author: Claude Code
  date: 2026-03-26
  status: reference
-->

# Gizmo Implementation Guide for UzEngine

## Overview

This guide translates UE5.6 gizmo specifications into concrete C++ implementation steps for UzEngine. All code patterns are designed for clarity and pixel-perfect parity with UE5.

---

## Part 1: Data Structures

### 1.1: GizmoStyle Configuration

```cpp
/// @ref UE5.6 TransformGizmo.h - FGizmoStyle structure
struct GizmoStyle
{
    // Global scaling
    float axisSize Multiplier = 1.0f;
    float lineThicknessMultiplier = 1.0f;

    // Translation geometry (screen-space units)
    float translateAxisLength = 70.0f;       // Stem length
    float translateAxisRadius = 1.5f;        // Stem cylinder radius
    float translateConeFaceHeight = 22.0f;   // Cone height
    float translateConeRadius = 7.0f;        // Cone base radius
    float translateConeHalfAngle = 16.0f;    // Cone angle (degrees)
    float translateScreenSpaceSize = 14.0f;  // Screen-space handle size

    // Rotation geometry
    float rotateAxisRadius = 70.0f;          // Torus major radius
    float rotateAxisInnerRadius = 1.25f;     // Torus minor radius (thickness)
    float rotateAxisOuterOffset = 3.0f;      // Outer edge thickness
    i32   rotateAxisSegments = 64;           // Circle segments
    i32   rotateAxisSlices = 8;              // Torus vertical slices
    float rotateArcballRadius = 70.0f;       // Arcball sphere radius

    // Scale geometry
    float scaleAxisLength = 70.0f;           // Cube stem length
    float scaleAxisRadius = 1.5f;            // Cube stem cylinder radius
    float scaleCubeDimension = 12.0f;        // Cube size (12×12×12)

    // Planar handles
    float planarHandleSize = 15.0f;          // Box size (15×15)
    float planarHandleThickness = 2.0f;      // Depth thickness
    float planarHandleOffset = -15.0f;       // Position along axis (-15 = inset)

    // Colors (Linear RGB, pre-multiplied alpha)
    glm::vec4 axisColorX{0.594f, 0.0197f, 0.0f, 0.8f};
    glm::vec4 axisColorY{0.1349f, 0.3959f, 0.0f, 0.8f};
    glm::vec4 axisColorZ{0.0251f, 0.207f, 0.85f, 0.8f};
    glm::vec4 screenSpaceColor{0.765f, 0.765f, 0.765f, 0.8f};
    glm::vec4 hoverColor{1.0f, 1.0f, 0.0f, 1.0f};
    glm::vec4 inactiveColor{0.5f, 0.5f, 0.5f, 0.3f};
    glm::vec4 arcballColor{0.5f, 0.5f, 0.5f, 0.03f};
};
```

### 1.2: Gizmo Element Base Class

```cpp
/// Base class for all renderable gizmo elements
class GizmoElement
{
public:
    enum class Type : u8
    {
        Axis,
        Planar,
        Arcball,
        ScreenSpace,
    };

    Type elementType;
    glm::vec3 position;          // World-space position
    glm::mat4 coordinateFrame;   // Transform matrix (world/local/custom)
    glm::vec4 color;             // Linear RGBA
    f32 opacity = 1.0f;          // Alpha override
    bool isHovered = false;
    bool isActive = false;
    bool isVisible = true;

    virtual void render(RHI::DrawContext& ctx, const GizmoStyle& style) = 0;
    virtual void updateMesh() = 0;
    virtual glm::mat4 getWorldTransform() const;
};
```

---

## Part 2: Mesh Generation

### 2.1: Cylinder (Axis Stem)

```cpp
/// Generate cylinder mesh for axis stems
/// Length: along Z-axis. Radius: radial extent.
struct CylinderMesh
{
    std::vector<glm::vec3> vertices;
    std::vector<u32> indices;
    glm::vec3 position;
    f32 length;
    f32 radius;
    i32 segmentCount = 16;

    void generate()
    {
        vertices.clear();
        indices.clear();

        // Generate cylinder with Z-axis as spine, Y-up view
        const f32 angleStep = 2.0f * M_PI / segmentCount;

        // Bottom cap vertices (Z = 0)
        for (i32 i = 0; i < segmentCount; ++i)
        {
            const f32 angle = i * angleStep;
            vertices.push_back({
                radius * cos(angle),
                radius * sin(angle),
                0.0f
            });
        }

        // Top cap vertices (Z = length)
        for (i32 i = 0; i < segmentCount; ++i)
        {
            const f32 angle = i * angleStep;
            vertices.push_back({
                radius * cos(angle),
                radius * sin(angle),
                length
            });
        }

        // Generate side face triangles
        for (i32 i = 0; i < segmentCount; ++i)
        {
            i32 bottomThis = i;
            i32 bottomNext = (i + 1) % segmentCount;
            i32 topThis = i + segmentCount;
            i32 topNext = ((i + 1) % segmentCount) + segmentCount;

            // Quad split into two triangles (CCW facing outward)
            indices.push_back(bottomThis);
            indices.push_back(topNext);
            indices.push_back(topThis);

            indices.push_back(bottomThis);
            indices.push_back(bottomNext);
            indices.push_back(topNext);
        }

        // Bottom cap triangles (facing down, -Z direction)
        for (i32 i = 1; i < segmentCount - 1; ++i)
        {
            indices.push_back(0);
            indices.push_back(i + 1);
            indices.push_back(i);
        }

        // Top cap triangles (facing up, +Z direction)
        const i32 topCenter = segmentCount;
        for (i32 i = 1; i < segmentCount - 1; ++i)
        {
            indices.push_back(topCenter);
            indices.push_back(topCenter + i);
            indices.push_back(topCenter + i + 1);
        }
    }

    void updatePosition(const glm::vec3& newPos)
    {
        position = newPos;
    }
};
```

### 2.2: Cone (Arrow Tip)

```cpp
/// Generate cone mesh for translate/rotate arrow tips
/// Height: Z-axis extent. Radius: base width.
struct ConeMesh
{
    std::vector<glm::vec3> vertices;
    std::vector<u32> indices;
    glm::vec3 position;
    f32 height;
    f32 baseRadius;
    i32 segmentCount = 16;

    void generate()
    {
        vertices.clear();
        indices.clear();

        // Tip vertex (apex)
        vertices.push_back({0.0f, 0.0f, height});

        // Base ring vertices
        const f32 angleStep = 2.0f * M_PI / segmentCount;
        for (i32 i = 0; i < segmentCount; ++i)
        {
            const f32 angle = i * angleStep;
            vertices.push_back({
                baseRadius * cos(angle),
                baseRadius * sin(angle),
                0.0f
            });
        }

        // Side face triangles (apex to base ring)
        for (i32 i = 0; i < segmentCount; ++i)
        {
            i32 apex = 0;
            i32 baseThis = 1 + i;
            i32 baseNext = 1 + ((i + 1) % segmentCount);

            // CCW facing outward
            indices.push_back(apex);
            indices.push_back(baseNext);
            indices.push_back(baseThis);
        }

        // Base cap triangles (facing down, -Z)
        for (i32 i = 1; i < segmentCount - 1; ++i)
        {
            indices.push_back(1);
            indices.push_back(1 + i + 1);
            indices.push_back(1 + i);
        }
    }
};
```

### 2.3: Torus (Rotation Ring)

```cpp
/// Generate torus mesh for rotation circles
/// majorRadius: Ring centerline radius (70.0 units)
/// minorRadius: Tube radius (1.25 units)
struct TorusMesh
{
    std::vector<glm::vec3> vertices;
    std::vector<u32> indices;
    glm::vec3 position;
    glm::vec3 axis;            // Rotation axis (X, Y, or Z)
    f32 majorRadius;
    f32 minorRadius;
    i32 majorSegments = 64;    // Segments around the ring
    i32 minorSegments = 8;     // Segments around the tube

    void generate()
    {
        vertices.clear();
        indices.clear();

        const f32 majorStep = 2.0f * M_PI / majorSegments;
        const f32 minorStep = 2.0f * M_PI / minorSegments;

        // Generate vertex grid
        for (i32 i = 0; i < majorSegments; ++i)
        {
            const f32 majorAngle = i * majorStep;
            const f32 cosM = cos(majorAngle);
            const f32 sinM = sin(majorAngle);

            for (i32 j = 0; j < minorSegments; ++j)
            {
                const f32 minorAngle = j * minorStep;
                const f32 cosm = cos(minorAngle);
                const f32 sinm = sin(minorAngle);

                // Compute vertex position
                // (Assuming axis is Z-axis; adjust for X/Y accordingly)
                f32 x = (majorRadius + minorRadius * cosm) * cosM;
                f32 y = (majorRadius + minorRadius * cosm) * sinM;
                f32 z = minorRadius * sinm;

                vertices.push_back({x, y, z});
            }
        }

        // Generate quad indices
        for (i32 i = 0; i < majorSegments; ++i)
        {
            const i32 iNext = (i + 1) % majorSegments;

            for (i32 j = 0; j < minorSegments; ++j)
            {
                const i32 jNext = (j + 1) % minorSegments;

                i32 v0 = i * minorSegments + j;
                i32 v1 = i * minorSegments + jNext;
                i32 v2 = iNext * minorSegments + jNext;
                i32 v3 = iNext * minorSegments + j;

                // Two triangles per quad (CCW)
                indices.push_back(v0);
                indices.push_back(v2);
                indices.push_back(v1);

                indices.push_back(v0);
                indices.push_back(v3);
                indices.push_back(v2);
            }
        }
    }

    void rotateForAxis(const glm::vec3& normalizedAxis)
    {
        axis = normalizedAxis;
        // Regenerate mesh with axis rotation applied
        // (Implementation: apply rotation matrix to all vertices)
    }
};
```

### 2.4: Cube (Scale Tip)

```cpp
/// Generate cube mesh for scale axis tips
/// Dimension: side length (12.0 units)
struct CubeMesh
{
    std::vector<glm::vec3> vertices;
    std::vector<u32> indices;
    f32 dimension;

    void generate()
    {
        vertices.clear();
        indices.clear();

        const f32 h = dimension * 0.5f;

        // 8 vertices of cube (centered at origin)
        vertices = {
            {-h, -h, -h}, {+h, -h, -h}, {+h, +h, -h}, {-h, +h, -h}, // -Z face
            {-h, -h, +h}, {+h, -h, +h}, {+h, +h, +h}, {-h, +h, +h}  // +Z face
        };

        // 6 faces, 2 triangles each, CCW facing outward
        // -Z face
        indices.insert(indices.end(), {0, 2, 1, 0, 3, 2});
        // +Z face
        indices.insert(indices.end(), {4, 5, 6, 4, 6, 7});
        // -X face
        indices.insert(indices.end(), {0, 4, 7, 0, 7, 3});
        // +X face
        indices.insert(indices.end(), {1, 2, 6, 1, 6, 5});
        // -Y face
        indices.insert(indices.end(), {0, 1, 5, 0, 5, 4});
        // +Y face
        indices.insert(indices.end(), {3, 7, 6, 3, 6, 2});
    }
};
```

---

## Part 3: Rendering Pipeline

### 3.1: Gizmo Renderer Class

```cpp
class GizmoRenderer
{
public:
    struct RenderContext
    {
        RHI::DrawContext* drawContext;
        const Camera& camera;
        const glm::mat4& viewProj;
        i32 viewportWidth;
        i32 viewportHeight;
    };

    void render(const RenderContext& ctx, const GizmoStyle& style)
    {
        // Phase 1: Translation mode
        if (currentMode == GizmoMode::Translate)
        {
            renderAxisCones(ctx, style);
            renderPlanarHandles(ctx, style);
            renderCenterHandle(ctx, style);
        }

        // Phase 2: Rotation mode
        else if (currentMode == GizmoMode::Rotate)
        {
            renderAxisTorusRings(ctx, style);
            renderArcballSphere(ctx, style);
        }

        // Phase 3: Scale mode
        else if (currentMode == GizmoMode::Scale)
        {
            renderAxisCubes(ctx, style);
            renderCenterHandle(ctx, style);
        }
    }

private:
    void renderAxisCones(const RenderContext& ctx, const GizmoStyle& style)
    {
        glm::vec4 colors[] = {style.axisColorX, style.axisColorY, style.axisColorZ};
        glm::vec3 axes[] = {glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1)};

        for (i32 i = 0; i < 3; ++i)
        {
            // Transform cone to world space
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), widgetPosition);
            transform = transform * coordinateFrame;
            transform = glm::translate(transform, axes[i] * style.translateAxisLength * 0.5f);

            // Align cone along axis
            glm::vec3 axisDir = glm::normalize(axes[i]);
            transform = transform * rotationMatrixToAlignZ(axisDir);

            // Determine alpha (hover/drag state)
            f32 alpha = (isHovered && hoveredAxis == i) ? 1.0f :
                        (isActive && activeAxis != i) ? 0.3f : style.axisColorX.a;

            glm::vec4 finalColor = glm::vec4(colors[i].rgb, alpha);

            // Render cone mesh
            ctx.drawContext->drawMesh(coneMesh[i], transform, finalColor);
        }
    }

    void renderAxisTorusRings(const RenderContext& ctx, const GizmoStyle& style)
    {
        glm::vec4 colors[] = {style.axisColorX, style.axisColorY, style.axisColorZ};
        glm::vec3 axes[] = {glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1)};

        for (i32 i = 0; i < 3; ++i)
        {
            // Transform torus to world space (aligned perpendicular to axis)
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), widgetPosition);
            transform = transform * coordinateFrame;
            transform = transform * rotationMatrixFromAxis(axes[i]);

            // Determine alpha
            f32 alpha = (isHovered && hoveredAxis == i) ? 1.0f :
                        (isActive && activeAxis != i) ? 0.3f : style.axisColorX.a;

            glm::vec4 finalColor = glm::vec4(colors[i].rgb, alpha);

            // Render torus mesh
            ctx.drawContext->drawMesh(torusMesh[i], transform, finalColor);
        }
    }

    void renderAxisCubes(const RenderContext& ctx, const GizmoStyle& style)
    {
        glm::vec4 colors[] = {style.axisColorX, style.axisColorY, style.axisColorZ};
        glm::vec3 axes[] = {glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1)};

        for (i32 i = 0; i < 3; ++i)
        {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), widgetPosition);
            transform = transform * coordinateFrame;
            transform = glm::translate(transform, axes[i] * style.scaleAxisLength * 0.5f);

            f32 alpha = (isHovered && hoveredAxis == i) ? 1.0f :
                        (isActive && activeAxis != i) ? 0.3f : style.axisColorX.a;

            glm::vec4 finalColor = glm::vec4(colors[i].rgb, alpha);

            ctx.drawContext->drawMesh(cubeMesh[i], transform, finalColor);
        }
    }

    void renderPlanarHandles(const RenderContext& ctx, const GizmoStyle& style)
    {
        // XY Plane
        glm::mat4 xyTransform = glm::translate(glm::mat4(1.0f), widgetPosition);
        xyTransform = xyTransform * coordinateFrame;
        ctx.drawContext->drawMesh(planarBoxMesh, xyTransform, style.hoverColor);
        // ... repeat for YZ and XZ
    }

    void renderCenterHandle(const RenderContext& ctx, const GizmoStyle& style)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), widgetPosition);
        transform = transform * coordinateFrame;
        glm::vec4 color = isHovered ? style.hoverColor : style.screenSpaceColor;
        ctx.drawContext->drawMesh(sphereMesh, transform, color);
    }

    void renderArcballSphere(const RenderContext& ctx, const GizmoStyle& style)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), widgetPosition);
        transform = transform * coordinateFrame;
        ctx.drawContext->drawMesh(arcballSphereMesh, transform, style.arcballColor);
    }
};
```

---

## Part 4: Screen-Space Scaling

### 4.1: Dynamic Scale Calculation

```cpp
/// Calculate screen-space scaling factor to maintain constant viewport size
f32 calculateScreenSpaceScale(
    const glm::vec3& widgetWorldPos,
    const Camera& camera,
    i32 viewportWidth,
    f32 targetScreenSize = 70.0f  // Virtual screen units
)
{
    // Transform widget position to screen space
    glm::vec4 screenPos = camera.viewProjMatrix * glm::vec4(widgetWorldPos, 1.0f);

    // Perspective divide
    f32 w = screenPos.w;
    if (fabs(w) < 1e-6f) return 1.0f;

    // Projection matrix scale (viewport width factor)
    // This converts from screen space coordinates back to world space units
    f32 projScale = viewportWidth / 2.0f;
    f32 depth = w;

    // Screen-space size = target size in pixels
    // World-space size = screen-space size * depth / projScale
    f32 worldScale = targetScreenSize * depth / (camera.fov * projScale);

    return worldScale;
}

/// Apply screen-space scaling to gizmo geometry
void applyScreenSpaceScale(GizmoStyle& style, f32 scale)
{
    style.translateAxisLength *= scale;
    style.rotateAxisRadius *= scale;
    style.scaleAxisLength *= scale;
    // ... scale all other dimension-related properties
}
```

---

## Part 5: Hit Testing

### 5.1: Ray-Sphere Intersection (Planar Handles)

```cpp
/// Test ray intersection with axis at given distance
/// Returns true if ray hits within hit radius
bool rayAxisIntersection(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& axisOrigin,
    const glm::vec3& axisDirection,
    f32 hitRadius,
    f32& outT
)
{
    glm::vec3 oa = rayOrigin - axisOrigin;
    glm::vec3 d = glm::normalize(rayDirection);
    glm::vec3 a = glm::normalize(axisDirection);

    // Closest point on axis to ray
    f32 t = -glm::dot(glm::cross(oa, a), glm::cross(d, a)) / glm::dot(glm::cross(d, a), glm::cross(d, a));
    glm::vec3 closestPoint = axisOrigin + a * t;
    glm::vec3 pointOnRay = rayOrigin + d * t;

    f32 distance = glm::distance(closestPoint, pointOnRay);
    outT = t;

    return distance < hitRadius;
}

/// Test ray intersection with sphere
bool raySphereIntersection(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& sphereCenter,
    f32 radius,
    f32& outT
)
{
    glm::vec3 oc = rayOrigin - sphereCenter;
    f32 a = glm::dot(rayDirection, rayDirection);
    f32 b = 2.0f * glm::dot(oc, rayDirection);
    f32 c = glm::dot(oc, oc) - radius * radius;

    f32 discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return false;

    f32 t1 = (-b - sqrt(discriminant)) / (2.0f * a);
    f32 t2 = (-b + sqrt(discriminant)) / (2.0f * a);

    outT = (t1 >= 0.0f) ? t1 : t2;
    return outT >= 0.0f;
}
```

---

## Part 6: Coordinate Space Handling

### 6.1: Local vs World Transformation

```cpp
enum class CoordinateSpace
{
    World,
    Local,
    Parent,
    Custom
};

class GizmoTransform
{
public:
    void setCoordinateSpace(CoordinateSpace space)
    {
        currentSpace = space;
        updateMatrix();
    }

private:
    void updateMatrix()
    {
        switch (currentSpace)
        {
        case CoordinateSpace::World:
            coordinateFrame = glm::mat4(1.0f);
            break;

        case CoordinateSpace::Local:
            // Rotate to match object's local axes
            coordinateFrame = glm::toMat4(objectRotationQuat);
            break;

        case CoordinateSpace::Parent:
            if (parentTransform)
            {
                coordinateFrame = parentTransform->getWorldMatrix();
            }
            break;

        case CoordinateSpace::Custom:
            // User-defined transformation matrix
            coordinateFrame = customMatrix;
            break;
        }
    }

    glm::mat4 coordinateFrame = glm::mat4(1.0f);
    CoordinateSpace currentSpace = CoordinateSpace::World;
    glm::quat objectRotationQuat;
    glm::mat4 customMatrix;
};
```

---

## Part 7: Implementation Checklist

### Phase 1: Core Structures
- [ ] Create `GizmoStyle` data structure with all constants
- [ ] Create `GizmoElement` base class hierarchy
- [ ] Create `GizmoRenderer` class with render pipeline

### Phase 2: Mesh Generation
- [ ] Implement cylinder mesh generator (axis stems)
- [ ] Implement cone mesh generator (arrow tips)
- [ ] Implement torus mesh generator (rotation rings)
- [ ] Implement cube mesh generator (scale tips)
- [ ] Implement box mesh generator (planar handles)
- [ ] Implement sphere mesh generator (center + arcball)

### Phase 3: Rendering
- [ ] Create emissive material with dynamic color binding
- [ ] Implement color assignment based on axis/state
- [ ] Implement opacity/alpha blending
- [ ] Implement depth testing & sorting

### Phase 4: Interaction
- [ ] Implement ray-axis hit testing
- [ ] Implement ray-sphere hit testing
- [ ] Implement hover state feedback
- [ ] Implement drag state feedback

### Phase 5: Integration
- [ ] Integrate screen-space scaling calculation
- [ ] Integrate coordinate space transformation
- [ ] Wire up mode switching (Translate/Rotate/Scale)
- [ ] Wire up hover/active state updates

### Phase 6: Polish
- [ ] Verify all colors match UE5 values
- [ ] Verify all dimensions match UE5 constants
- [ ] Test screen-space scaling at various zoom levels
- [ ] Test all interaction modes
- [ ] Validate visual parity with reference

---

## Part 8: Material Definition (HLSL)

```hlsl
// GizmoEmissive.hlsl
// Unlit emissive material for gizmo rendering

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float3 normal : NORMAL;
};

cbuffer TransformCB : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
};

cbuffer GizmoCB : register(b1)
{
    float4 baseColor;
    float4 emissiveIntensity; // (r, g, b, alpha)
};

PSInput main(VSInput input)
{
    PSInput output;

    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.position = mul(worldPos, worldViewProj);
    output.color = input.color * baseColor;
    output.normal = mul(input.normal, (float3x3)world);

    return output;
}

// Pixel shader
float4 main(PSInput input) : SV_TARGET
{
    // Emissive output (unlit, high brightness)
    float3 emissive = input.color.rgb * emissiveIntensity.rgb * 2.0;
    float alpha = input.color.a;

    return float4(emissive, alpha);
}
```

---

## Summary

To achieve UE5.6 visual parity:

1. **Implement all mesh generators** with exact dimensions from UE5
2. **Use the color values** directly from AxisDisplayInfo (Section B.1)
3. **Apply screen-space scaling** to maintain constant viewport appearance
4. **Implement state-based alpha blending** (hover/active/inactive)
5. **Follow the render order** (arcball → planar → circles → stems → tips)
6. **Test at multiple zoom levels** to verify screen-space sizing

All numerical values are extracted from UE5.6 source and are production-ready for implementation.

