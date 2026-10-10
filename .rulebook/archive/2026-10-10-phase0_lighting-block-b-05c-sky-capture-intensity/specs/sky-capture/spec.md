# Sky cubemap capture

## Purpose
The sky cubemap every image-based term samples is the sky the frame renders, per channel.

## ADDED Requirements

### Requirement: The capture scatters the sun illuminance per channel
The sky cubemap capture SHALL scale the in-scatter and the ground term by the sun illuminance per
channel, the same quantity the sky pass uses, and SHALL carry no sun disc.

#### Scenario: A white sun
Given the reflection reference (sun illuminance (10, 10, 10)) and the camera mirrored through the floor
When the dumped environment is compared with the mirrored view's sky along the same rays
Then the ratio sky pass / capture has a median within [0.95, 1.05] and a p10-p90 within [0.90, 1.10].

#### Scenario: A coloured sun
Given the warm-sun variant (sun colour (1.0, 0.7, 0.4))
When the per-channel ratio sky pass / capture is taken over the sky pixels
Then each channel's median lies within [0.95, 1.05].

#### Scenario: The reflection is the sky
Given the chrome floor with SSR off
When the floor pixels whose reflected ray sees the sky are compared with F x the mirrored view
Then the median relative error is at most 0.15.

### Requirement: Only the atmosphere capture changes
The direct lighting and every path without the atmosphere capture SHALL be unchanged.

#### Scenario: Sponza
Given Sponza Camera C at frame 120
When the frame is compared with the capture of the build before the item
Then positions and direct light are identical and the indirect light's median ratio lies within
[0.5, 1.0].

#### Scenario: A PMREM environment
Given the chrome floor under the ballroom PMREM
When its specular is compared with the build before the item
Then it is bit-identical.
