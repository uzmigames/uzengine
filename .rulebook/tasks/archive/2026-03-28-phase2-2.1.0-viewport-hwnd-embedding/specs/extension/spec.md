## ADDED Requirements

### Requirement: H.264 Frame Encoder
The runtime SHALL encode DX12 rendered frames as H.264 bitstream using Media Foundation hardware encoder with Baseline profile, low-latency mode, and CBR bitrate.

#### Scenario: Encode a rendered frame
Given the runtime has an active DX12 viewport rendering at 1280x720
When `uz_encode_frame()` is called after `uz_render_frame()`
Then the function SHALL return a Buffer containing H.264 NAL units within 4ms

#### Scenario: Keyframe request
Given the encoder is producing delta frames
When `uz_request_keyframe()` is called
Then the next encoded frame SHALL be an IDR keyframe (NAL type 5)

#### Scenario: JPEG fallback
Given the system does not have a hardware H.264 encoder
When the H264Encoder is initialized
Then the encoder SHALL fall back to JPEG encoding via stb_image_write and log a warning

### Requirement: WebCodecs Canvas Viewport
The VSCode webview SHALL decode H.264 frames using WebCodecs VideoDecoder and render them to a canvas element at the panel's native resolution.

#### Scenario: Open .uzscene file
Given a `.uzscene` file exists in the workspace
When the user opens the file in VSCode
Then the SceneEditorProvider SHALL display a canvas with the rendered 3D scene inside the editor tab

#### Scenario: Frame delivery pipeline
Given the SceneEditorProvider render loop is active
When `renderFrame()` + `encodeFrame()` complete on the extension host
Then the H.264 frame data SHALL be posted to the webview via `postMessage`
And the webview SHALL decode it with `VideoDecoder` and draw to canvas within 7ms total latency

#### Scenario: Panel resize
Given the viewport canvas is visible
When the user resizes the editor panel
Then the extension SHALL call `resizeViewport(newWidth, newHeight)`
And the encoder SHALL reinitialize at the new resolution
And the webview canvas SHALL resize to match

### Requirement: Viewport Lifecycle Management
The extension SHALL manage the DX12 viewport lifecycle to match the webview panel visibility and disposal.

#### Scenario: Panel hidden
Given the viewport is rendering
When the user switches to a different tab
Then the render+encode loop SHALL stop
And the native HWND SHALL be hidden

#### Scenario: Panel revealed
Given the viewport was previously hidden
When the user switches back to the .uzscene tab
Then the render+encode loop SHALL resume
And a keyframe SHALL be requested for clean decoder restart

#### Scenario: Panel closed
Given the viewport is active
When the user closes the .uzscene tab
Then `destroyViewport()` SHALL be called
And the H264Encoder SHALL be shut down
And all GPU resources SHALL be released

### Requirement: Adaptive Bitrate
The encoder SHALL adjust bitrate based on scene motion to balance quality and performance.

#### Scenario: Static scene
Given the viewport shows a mostly static scene
When motion variance is below 0.5
Then the encoder bitrate SHALL be set to 20 Mbps for maximum quality

#### Scenario: High motion scene
Given the viewport shows rapid camera movement or animation
When motion variance exceeds 0.5
Then the encoder bitrate SHALL be reduced to 10 Mbps to maintain frame rate

## MODIFIED Requirements

### Requirement: SceneEditorProvider Output
The SceneEditorProvider SHALL render engine output to a canvas inside the webview instead of displaying a "Rendering in native viewport window" placeholder.

#### Scenario: Canvas replaces placeholder
Given the SceneEditorProvider is registered for `.uzscene` files
When a `.uzscene` file is opened
Then the webview SHALL contain a full-size canvas element
And the canvas SHALL display live DX12-rendered frames
And the toolbar (scene name, gizmo buttons, stats) SHALL overlay the canvas
