# GPU Pass Timing

## Purpose
A diagnostic run reports the GPU cost of every render pass as measured on the GPU, from frames
whose execution has completed, labelled with the pass that produced each measurement.

## ADDED Requirements

### Requirement: Diagnostic timing is populated
The system SHALL fill the `timing` array of the `--diag` report with one entry per render-graph
pass measured in the run's timing window, carrying mean, min and max GPU milliseconds and the
number of sampled frames, plus the GPU frame span.

#### Scenario: Diagnostic run on a scene
Given `uzengine.exe --diag` renders a scene for its frame budget
When the report is printed
Then `timing` is non-empty, every statistic is finite with min <= mean <= max, and passes that run
every frame carry as many samples as the window has frames.

### Requirement: Measurements belong to completed frames
The system SHALL resolve timestamps only from a frame whose fence the CPU has already waited, and
SHALL label each measurement with the pass names recorded for that same frame.

#### Scenario: A pass that runs on one frame of the window
Given a pass that executes on exactly one frame inside the timing window (a sky IBL re-bake)
When the statistics are reported
Then that pass carries exactly one sample under its own name, and every reported name is a pass
the graph registered.

### Requirement: Manifest and report agree
The system SHALL derive the run manifest's `gpu_ms` and the report's `timing` from the same
accumulation.

#### Scenario: Both outputs requested
Given a run with `--diag` and `--run-manifest`
When both are produced
Then they list the same pass names with equal means.
