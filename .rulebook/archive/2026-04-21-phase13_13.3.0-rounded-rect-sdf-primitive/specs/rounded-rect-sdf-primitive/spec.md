# rounded-rect-sdf-primitive

## ADDED Requirements

### Requirement: AddRoundedRect emits a tagged quad

`TextRenderer::AddRoundedRect(rect, r, fill, bt, bc, so, sb, sc)`
SHALL append a 4-vertex quad whose primitive-type tag equals 3 and
whose ring-buffer slot carries the passed parameters verbatim.

#### Scenario: Rounded panel emitted with correct tag

Given a `TextRenderer` with an active frame
When `AddRoundedRect({0,0,200,100}, 6.0f, gray, 1.0f, black, {2,2}, 4.0f, shadow)` runs
Then the captured vertex buffer contains a quad with tag bit=3 and
the ring-buffer slot at index `vertexID/4` carries cornerRadius=6.0.

### Requirement: Degenerate args fast-path

When `cornerRadius == 0 && borderThickness == 0 && shadowBlur == 0`,
`AddRoundedRect` SHALL emit a quad indistinguishable from `AddRect` —
same tag (0), same vertex layout, same shader path.

#### Scenario: Zero-radius rrect is byte-identical to AddRect

Given identical fill color + rect + device state
When the author calls `AddRect` vs `AddRoundedRect` with
zero-everything
Then both captured vertex sequences are byte-for-byte identical.

### Requirement: SDF shader correctness at corners

The SDF rounded-rect shader SHALL produce an alpha=0 fragment at the
exact corner of a `cornerRadius=8` rect (pixel perfectly outside the
rounded boundary), an alpha=1 fragment at the rect center, and a
smooth transition of width >= 1 px at the boundary.

#### Scenario: Corner pixel is transparent

Given a 100x100 `AddRoundedRect` with cornerRadius=8 rendered to an
offscreen target
When the pixel at `(0, 0)` is sampled
Then its alpha is < 0.01.
