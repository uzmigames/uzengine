# Cloud noise assets

## Purpose
The engine's cloud noise volumes reach every project run by uzengine.exe.

## ADDED Requirements

### Requirement: The noise volumes are engine content
The build SHALL install the cloud noise volumes next to the exe, and the cloud pass SHALL load them from the
project's VFS when the project provides them and from the engine's content otherwise, trying once.

#### Scenario: A sample project
Given sample/projects/cloud_test.uzproject, whose content root has no atmosphere folder
When uzengine.exe runs it for 180 frames
Then the log reports the shape and the detail noise loaded, at most two log lines mention the noise files, and
the run completes without a device removal.

#### Scenario: The installed files
Given a build
When the installed bin/content/atmosphere files are compared with runtime/content/atmosphere
Then they are byte-identical.
