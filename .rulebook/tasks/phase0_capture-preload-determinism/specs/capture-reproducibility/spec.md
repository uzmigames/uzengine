# Capture reproducibility

## Purpose
Separate processes of one build and one configuration render the same frames.

## ADDED Requirements

### Requirement: The pre-load length depends only on the scene
The pre-load SHALL take the same number of frames in every process of one build and one configuration,
whatever the timing of its worker threads, and SHALL start the scene's distance-field bakes in a fixed
order.

#### Scenario: Six processes of Sponza
Given six processes of `scripts/sponza-flicker-w.uzproject` at fixed exposure on one build
When each pre-loads the scene and screenshots frame 237
Then every process reports the same pre-load length
And the screenshots agree within one code value.
