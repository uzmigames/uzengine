# Depth Prepass

## Purpose
The depth prepass writes the frame's opaque depth with the GBuffer's own transform, the GBuffer
reuses it, and the pass is on only where it pays.

## ADDED Requirements

### Requirement: The prepass depth is the GBuffer's depth
The depth prepass SHALL render standard opaque draws with the GBuffer's vertex shader and constants,
and the GBuffer pass SHALL keep that depth and draw standard opaque geometry with an Equal test and
no depth write; masked and material-graph draws SHALL be skipped by the prepass and drawn by the
GBuffer with Greater and depth writes.

#### Scenario: Identity
Given Sponza Camera C, the overdraw stack and the masked-wall scene
When the G-buffer positions and the direct light are compared with and without the prepass
Then they are bit-identical.

#### Scenario: Early-Z
Given the overdraw stack (48 walls drawn far to near)
When the GBuffer pass is timed with and without the prepass
Then with it, it costs at most 0.7 × without.

### Requirement: The default is measured
`r.DepthPrepass` SHALL default to the cheaper of the two configurations on Sponza (prepass + GBuffer
against GBuffer alone, ties to off).

#### Scenario: Sponza
Given Sponza Camera C at 1080p
When both configurations are timed
Then the shipped default matches the smaller sum, and the frame with the default has positions and
direct light identical to the committed capture.
