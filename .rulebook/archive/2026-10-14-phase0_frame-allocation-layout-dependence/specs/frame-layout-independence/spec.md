# Frame layout independence

## Purpose
The rendered frame does not depend on where resources are allocated.

## ADDED Requirements

### Requirement: No dependence on allocation layout
A frame SHALL be bit-identical whether or not an unrelated resource is allocated before the scene's resources.

#### Scenario: A pass that skips its resources
Given the reflection, fog-shaft and path-tracer references
When one pass's resources are not allocated (or a diagnostic allocation shifts the layout)
Then the screenshots are bit-identical to those of the unshifted layout.
