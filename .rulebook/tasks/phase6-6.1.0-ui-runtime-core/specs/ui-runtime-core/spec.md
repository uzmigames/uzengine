# UI Runtime Core — Specification

## ADDED Requirements

### Requirement: HTML Subset Parser
The UI system SHALL parse a safe subset of HTML for game UI authoring.

#### Scenario: Parse Simple HUD
Given HTML: `<div class="healthbar"><div class="fill" style="width: var(--health)%"></div></div>`
When the parser processes the input
Then a UIElement tree SHALL be created with parent div.healthbar containing child div.fill
And class names SHALL be extracted for CSS rule matching
And inline styles SHALL be parsed and applied

#### Scenario: Unsupported Tags
Given HTML containing `<script>` or `<iframe>`
When the parser encounters these tags
Then they SHALL be ignored (stripped) with a warning logged
And parsing SHALL continue with the rest of the document

### Requirement: CSS Flexbox Layout
The UI system SHALL implement CSS flexbox layout for UI element positioning.

#### Scenario: Horizontal Layout
Given CSS: `.toolbar { display: flex; flex-direction: row; gap: 8px; }`
And 3 child elements each 100px wide
When layout is computed
Then children SHALL be placed horizontally with 8px gaps
And total width SHALL be 100+8+100+8+100 = 316px

#### Scenario: Justify Content
Given CSS: `.container { display: flex; justify-content: space-between; width: 500px; }`
And 3 child elements each 100px wide
When layout is computed
Then remaining space (200px) SHALL be distributed as 100px gaps between children

### Requirement: SDF Font Rendering
The UI system SHALL render text using signed distance field fonts.

#### Scenario: Text Rendering
Given a UiText element with text="Score: 1000" and font-size=24px
When the UIPass renders
Then SDF glyphs SHALL be sampled from the font atlas texture
And anti-aliasing SHALL be achieved via smoothstep on the distance field
And text SHALL remain sharp at any resolution

### Requirement: UIPass in Render Graph
The UI system SHALL render UI as a post-scene overlay via the render graph.

#### Scenario: UI Rendering
Given a scene with 3D content and a UIDocument loaded
When the frame renders
Then the UIPass SHALL execute after all 3D passes (sort_key=800)
And UI elements SHALL be alpha-blended over the 3D scene
And depth test SHALL be disabled for UI rendering
