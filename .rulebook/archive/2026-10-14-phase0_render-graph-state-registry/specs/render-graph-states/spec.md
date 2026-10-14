# Render-graph resource states

## Purpose
Every barrier the render graph emits starts from the resource's actual state.

## ADDED Requirements

### Requirement: Imported resources carry their state across frames
The render graph SHALL import a tracked resource in the state the previous frame (or its creation) left it in,
SHALL deduplicate a handle imported twice, and SHALL report an explicit import claim that disagrees with the
tracked state.

#### Scenario: Two frames
Given a texture imported as tracked in two consecutive frames
When the first frame leaves it in ShaderResource
Then the second frame's first barrier for it starts from ShaderResource.

#### Scenario: A recreated texture
Given a texture destroyed and recreated (same slot, new generation)
When it is imported as tracked
Then its first barrier starts from its creation state.
