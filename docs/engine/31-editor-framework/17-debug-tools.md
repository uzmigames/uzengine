# Debug Tools — In-Editor Preview & Debugging

> **Status**: Planned (Phase 3–4)
> **Source**: `extension/editor/debug-tools.ts`, `engine/runtime/debug/debug_tools.h`
> **References**: Animation System (§21), Physics System (§25), Audio System (§26), Shader System (§05), Console (§18.06)

## Purpose

Debug Tools are in-editor viewers and debuggers for assets and runtime systems. Designers and programmers preview animations, textures, meshes, and shader code with real-time debugging aids. Tools include animation scrubbers, texture channel isolation, mesh visualization (wireframe/normals/LODs), shader stepping (via PIX integration), physics shape visualization, and audio listener debugging.

## Architecture

```
Debug Tools (TypeScript/Webview)
    │
    ├─ Animation Preview
    │   ├─ Skeletal mesh viewer
    │   ├─ Animation timeline scrubber
    │   ├─ Blend space editor (parameter sliders)
    │   ├─ Bone weight heat map
    │   └─ Frame-by-frame playback
    │
    ├─ Texture Viewer
    │   ├─ Mip level selector
    │   ├─ RGBA channel isolation (show R/G/B/A individually)
    │   ├─ Compression format indicator
    │   ├─ Histogram (brightness distribution)
    │   ├─ UV grid overlay
    │   └─ Zoom/pan
    │
    ├─ Mesh Viewer
    │   ├─ Wireframe mode
    │   ├─ Normal visualization (blue arrows)
    │   ├─ Tangent/bitangent display
    │   ├─ LOD level switching
    │   ├─ Bone weights heat map
    │   ├─ Vertex count + triangle count
    │   └─ Bounds visualization
    │
    ├─ Shader Debugger
    │   ├─ PIX integration (capture frame, step through shader)
    │   ├─ RenderDoc integration (alternative)
    │   ├─ Register inspection (VGPR, SGPR values)
    │   ├─ Disassembly view (DXIL bytecode)
    │   └─ Constant buffer visualizer
    │
    ├─ NavMesh Visualizer
    │   ├─ Overlay on viewport (green mesh)
    │   ├─ Path preview (draw path between two points)
    │   ├─ Obstacle visualization
    │   ├─ Agent size preview (capsule)
    │   └─ Query results (valid path, distance)
    │
    ├─ Physics Debugger
    │   ├─ Collision shapes (wireframe)
    │   ├─ Contact points (red dots)
    │   ├─ Velocity vectors (arrows)
    │   ├─ Force visualization
    │   ├─ Layer matrix inspector
    │   ├─ Trigger volume highlighting
    │   └─ Constraint visualization
    │
    ├─ Audio Debugger
    │   ├─ Listener position (wireframe sphere)
    │   ├─ Active sound sources (spheres, labeled)
    │   ├─ Attenuation falloff visualization
    │   ├─ Bus levels (mixer view with sliders)
    │   ├─ Voice allocation (current / max)
    │   └─ Waveform preview (mono/stereo)
    │
    ├─ Performance HUD (always-on, toggleable)
    │   ├─ FPS counter
    │   ├─ Frame time graph (100 frame history)
    │   ├─ Draw calls per frame
    │   ├─ Triangle count (visible)
    │   ├─ VRAM usage (current / max)
    │   ├─ Physics cost (ms)
    │   └─ Audio cost (ms)
    │
    └─ View Menu (toggle each tool)
        ├─ Show Animation Preview
        ├─ Show Texture Viewer
        ├─ Show Mesh Viewer
        ├─ Show Physics Shapes
        ├─ Show NavMesh
        ├─ Show Audio Sources
        ├─ Show Performance HUD
        └─ Console (Quake-style overlay)

All tools: viewport-based or webview panels, real-time updates, no external tools required.
```

## Animation Preview

Skeletal animation scrubber with parameter blending:

```typescript
/// Animation preview and playback control.
export class AnimationPreview {
    private mesh: SkeletalMesh | null = null;
    private currentAnimation: AnimationAsset | null = null;
    private playbackTime: number = 0.0;
    private isPlaying: boolean = false;
    private playRate: number = 1.0;

    async loadMesh(meshPath: string) {
        this.mesh = await this.ipc.call("assets.load_skeletal_mesh", [meshPath]);
        this.renderMesh();
    }

    async loadAnimation(animPath: string) {
        this.currentAnimation = await this.ipc.call("assets.load_animation", [animPath]);
        this.playbackTime = 0;
        this.renderFrame();
    }

    /// Play animation timeline.
    play() {
        this.isPlaying = true;
        this.animate();
    }

    pause() {
        this.isPlaying = false;
    }

    stop() {
        this.isPlaying = false;
        this.playbackTime = 0;
        this.renderFrame();
    }

    /// Scrub to specific time.
    seek(time: number) {
        this.playbackTime = Math.max(0, Math.min(time, this.currentAnimation?.duration || 0));
        this.renderFrame();
    }

    private animate() {
        if (!this.isPlaying || !this.currentAnimation) return;

        const delta = 1 / 60;  // 60 FPS
        this.playbackTime += delta * this.playRate;

        if (this.playbackTime >= this.currentAnimation.duration) {
            this.playbackTime = 0;  // Loop
        }

        this.renderFrame();
        requestAnimationFrame(() => this.animate());
    }

    private renderFrame() {
        if (!this.mesh || !this.currentAnimation) return;

        // Sample animation at current time
        const pose = this.currentAnimation.SamplePose(this.playbackTime);

        // Request render from engine
        this.ipc.call("debug.set_skeletal_pose", [
            this.mesh.id,
            pose
        ]);

        // Update timeline UI
        this.updateTimelineUI();
    }

    private updateTimelineUI() {
        if (!this.currentAnimation) return;

        const progress = this.playbackTime / this.currentAnimation.duration;
        const timeStr = this.formatTime(this.playbackTime);
        const durationStr = this.formatTime(this.currentAnimation.duration);

        const timeline = document.querySelector('.animation-timeline')!;
        timeline.innerHTML = `
            <div style="width: ${progress * 100}%; height: 4px; background: #0f0;"></div>
            <p>${timeStr} / ${durationStr}</p>
        `;
    }

    /// Blend space: animate parameters (e.g., walk speed 0-1).
    updateBlendParameter(paramName: string, value: number) {
        this.ipc.call("debug.set_blend_parameter", [paramName, value]);
    }

    private formatTime(seconds: number): string {
        const mins = Math.floor(seconds / 60);
        const secs = (seconds % 60).toFixed(2);
        return `${mins}:${secs.padStart(5, '0')}`;
    }
}
```

## Texture Viewer

Channel isolation and mip level inspection:

```typescript
/// Texture viewer with mip/channel controls.
export class TextureViewer {
    private texture: TextureAsset | null = null;
    private currentMipLevel: number = 0;
    private activeChannels: Set<'R' | 'G' | 'B' | 'A'> = new Set(['R', 'G', 'B', 'A']);
    private showGrid: boolean = true;

    async loadTexture(texturePath: string) {
        this.texture = await this.ipc.call("assets.load_texture", [texturePath]);
        this.renderTexture();
    }

    setMipLevel(level: number) {
        this.currentMipLevel = Math.max(0, Math.min(level, this.texture?.mip_count || 1));
        this.renderTexture();
    }

    /// Toggle channel visibility.
    setChannels(channels: Set<'R' | 'G' | 'B' | 'A'>) {
        this.activeChannels = channels;
        this.renderTexture();
    }

    toggleGrid() {
        this.showGrid = !this.showGrid;
        this.renderTexture();
    }

    private renderTexture() {
        if (!this.texture) return;

        const channels = {
            R: this.activeChannels.has('R'),
            G: this.activeChannels.has('G'),
            B: this.activeChannels.has('B'),
            A: this.activeChannels.has('A')
        };

        this.ipc.call("debug.render_texture", [
            this.texture.id,
            {
                mip_level: this.currentMipLevel,
                channels: channels,
                show_grid: this.showGrid
            }
        ]);

        this.updateMetadata();
    }

    private updateMetadata() {
        if (!this.texture) return;

        const meta = document.querySelector('.texture-metadata')!;
        meta.innerHTML = `
            <p>Format: ${this.texture.format}</p>
            <p>Dimensions: ${this.texture.width}x${this.texture.height}</p>
            <p>Mip Levels: ${this.texture.mip_count}</p>
            <p>Compression: ${this.texture.compression}</p>
        `;
    }

    /// Show histogram (brightness distribution).
    showHistogram() {
        if (!this.texture) return;

        this.ipc.call("debug.get_texture_histogram", [this.texture.id])
            .then(hist => this.renderHistogram(hist));
    }

    private renderHistogram(histogram: number[]) {
        const canvas = document.createElement('canvas');
        canvas.width = 256;
        canvas.height = 100;
        const ctx = canvas.getContext('2d')!;

        const max = Math.max(...histogram);
        ctx.fillStyle = '#0f0';

        for (let i = 0; i < 256; i++) {
            const height = (histogram[i] / max) * canvas.height;
            ctx.fillRect(i, canvas.height - height, 1, height);
        }

        document.querySelector('.histogram-canvas')!.appendChild(canvas);
    }
}
```

## Mesh Viewer

Wireframe, normals, LODs, and bone weights:

```typescript
/// Mesh viewer with visualization modes.
export class MeshViewer {
    private mesh: StaticMesh | null = null;
    private visualizationMode: 'Solid' | 'Wireframe' | 'Normals' | 'Tangents' | 'LOD' = 'Solid';
    private lodLevel: number = 0;
    private showBoneWeights: boolean = false;

    async loadMesh(meshPath: string) {
        this.mesh = await this.ipc.call("assets.load_mesh", [meshPath]);
        this.render();
    }

    setVisualizationMode(mode: 'Solid' | 'Wireframe' | 'Normals' | 'Tangents' | 'LOD') {
        this.visualizationMode = mode;
        this.render();
    }

    setLODLevel(level: number) {
        if (!this.mesh) return;
        this.lodLevel = Math.max(0, Math.min(level, this.mesh.lod_count - 1));
        this.render();
    }

    toggleBoneWeights() {
        this.showBoneWeights = !this.showBoneWeights;
        this.render();
    }

    private render() {
        if (!this.mesh) return;

        this.ipc.call("debug.set_mesh_visualization", [
            this.mesh.id,
            {
                mode: this.visualizationMode,
                lod_level: this.lodLevel,
                show_bone_weights: this.showBoneWeights
            }
        ]);

        this.updateStats();
    }

    private updateStats() {
        if (!this.mesh) return;

        const lod = this.mesh.lods[this.lodLevel];
        const meta = document.querySelector('.mesh-metadata')!;
        meta.innerHTML = `
            <p>LOD ${this.lodLevel}: ${lod.vertex_count} vertices, ${lod.triangle_count} triangles</p>
            <p>Bounds: ${this.mesh.bounds.min.toString()} to ${this.mesh.bounds.max.toString()}</p>
            <p>Materials: ${this.mesh.materials.length}</p>
            <p>Bones: ${this.mesh.skeleton?.bone_count || 'N/A'}</p>
        `;
    }
}
```

## Physics Debugger

Shape visualization and contact inspection:

```typescript
/// Physics debugging and visualization.
export class PhysicsDebugger {
    private showShapes: boolean = true;
    private showContacts: boolean = true;
    private showVelocities: boolean = false;
    private showConstraints: boolean = false;

    toggleShapeVisualization() {
        this.showShapes = !this.showShapes;
        this.updateVisualization();
    }

    toggleContactPoints() {
        this.showContacts = !this.showContacts;
        this.updateVisualization();
    }

    toggleVelocityVectors() {
        this.showVelocities = !this.showVelocities;
        this.updateVisualization();
    }

    toggleConstraints() {
        this.showConstraints = !this.showConstraints;
        this.updateVisualization();
    }

    private updateVisualization() {
        this.ipc.call("debug.set_physics_visualization", [
            {
                show_shapes: this.showShapes,
                show_contacts: this.showContacts,
                show_velocities: this.showVelocities,
                show_constraints: this.showConstraints
            }
        ]);
    }

    /// Inspect collision layer matrix.
    showLayerMatrix() {
        this.ipc.call("debug.get_collision_layers", [])
            .then(layers => this.renderLayerMatrix(layers));
    }

    private renderLayerMatrix(layers: string[]) {
        const table = document.createElement('table');
        table.style.cssText = 'border-collapse: collapse;';

        // Header row
        const headerRow = table.insertRow();
        headerRow.insertCell().textContent = 'Layer';
        for (const layer of layers) {
            headerRow.insertCell().textContent = layer;
        }

        // Layer rows
        for (let i = 0; i < layers.length; i++) {
            const row = table.insertRow();
            row.insertCell().textContent = layers[i];

            for (let j = 0; j < layers.length; j++) {
                const cell = row.insertCell();
                cell.style.cssText = 'width: 30px; height: 30px; text-align: center;';
                cell.textContent = i === j ? '✓' : '○';  // Self-collision vs external
                cell.onclick = () => this.toggleLayerCollision(layers[i], layers[j]);
            }
        }

        document.querySelector('.layer-matrix')!.innerHTML = '';
        document.querySelector('.layer-matrix')!.appendChild(table);
    }

    private toggleLayerCollision(layerA: string, layerB: string) {
        this.ipc.call("debug.toggle_layer_collision", [layerA, layerB]);
    }
}
```

## Audio Debugger

Source visualization and bus monitoring:

```typescript
/// Audio system debugging.
export class AudioDebugger {
    private showListeners: boolean = true;
    private showSources: boolean = true;
    private showAttenuation: boolean = false;

    toggleListenerVisualization() {
        this.showListeners = !this.showListeners;
        this.updateVisualization();
    }

    toggleSourceVisualization() {
        this.showSources = !this.showSources;
        this.updateVisualization();
    }

    toggleAttenuationVisualization() {
        this.showAttenuation = !this.showAttenuation;
        this.updateVisualization();
    }

    private updateVisualization() {
        this.ipc.call("debug.set_audio_visualization", [
            {
                show_listeners: this.showListeners,
                show_sources: this.showSources,
                show_attenuation: this.showAttenuation
            }
        ]);
    }

    /// Show bus mixer (volume, mute, solo).
    showBusMixer() {
        this.ipc.call("debug.get_audio_buses", [])
            .then(buses => this.renderBusMixer(buses));
    }

    private renderBusMixer(buses: AudioBus[]) {
        const mixer = document.querySelector('.audio-mixer')!;
        mixer.innerHTML = '';

        for (const bus of buses) {
            const busPanel = document.createElement('div');
            busPanel.className = 'bus-panel';
            busPanel.style.cssText = 'border: 1px solid #666; padding: 10px; margin: 5px;';

            // Bus name
            const nameLabel = document.createElement('strong');
            nameLabel.textContent = bus.name;
            busPanel.appendChild(nameLabel);

            // Volume slider
            const volumeSlider = document.createElement('input');
            volumeSlider.type = 'range';
            volumeSlider.min = '0';
            volumeSlider.max = '1';
            volumeSlider.step = '0.01';
            volumeSlider.value = String(bus.volume);
            volumeSlider.onchange = () => {
                this.ipc.call("audio.set_bus_volume", [bus.name, parseFloat(volumeSlider.value)]);
            };
            busPanel.appendChild(volumeSlider);

            // Mute button
            const muteBtn = document.createElement('button');
            muteBtn.textContent = bus.muted ? 'Unmute' : 'Mute';
            muteBtn.onclick = () => {
                this.ipc.call("audio.set_bus_mute", [bus.name, !bus.muted]);
            };
            busPanel.appendChild(muteBtn);

            // Voice count
            const voiceLabel = document.createElement('p');
            voiceLabel.textContent = `Voices: ${bus.active_voices} / ${bus.max_voices}`;
            busPanel.appendChild(voiceLabel);

            mixer.appendChild(busPanel);
        }
    }
}
```

## NavMesh Visualizer

Path preview and obstacle checking:

```typescript
/// NavMesh debugging.
export class NavMeshVisualizer {
    private showNavMesh: boolean = true;
    private showObstacles: boolean = true;

    toggleNavMeshOverlay() {
        this.showNavMesh = !this.showNavMesh;
        this.updateVisualization();
    }

    toggleObstacles() {
        this.showObstacles = !this.showObstacles;
        this.updateVisualization();
    }

    private updateVisualization() {
        this.ipc.call("debug.set_navmesh_visualization", [
            {
                show_mesh: this.showNavMesh,
                show_obstacles: this.showObstacles
            }
        ]);
    }

    /// Preview path between two points.
    previewPath(fromPos: Vec3, toPos: Vec3) {
        this.ipc.call("debug.navmesh_query_path", [fromPos, toPos])
            .then(path => {
                if (path) {
                    this.ipc.call("debug.draw_path", [path]);
                } else {
                    console.log("No valid path found");
                }
            });
    }

    /// Preview agent capsule at position.
    previewAgentShape(position: Vec3, radius: number, height: number) {
        this.ipc.call("debug.draw_agent_shape", [position, radius, height]);
    }
}
```

## Performance HUD

Always-on statistics display:

```typescript
/// Real-time performance monitoring.
export class PerformanceHUD {
    private statsHistory: Array<{
        frame_time_ms: number,
        fps: number,
        draw_calls: number,
        triangle_count: number,
        vram_mb: number,
        physics_ms: number,
        audio_ms: number
    }> = [];

    private maxHistoryFrames: number = 100;
    private hudCanvas: HTMLCanvasElement;

    constructor(container: HTMLElement) {
        this.hudCanvas = document.createElement('canvas');
        this.hudCanvas.width = 300;
        this.hudCanvas.height = 150;
        this.hudCanvas.style.cssText = 'position: fixed; top: 10px; left: 10px; background: rgba(0,0,0,0.7);';
        container.appendChild(this.hudCanvas);

        this.updateLoop();
    }

    private updateLoop() {
        this.ipc.call("debug.get_frame_stats", [])
            .then(stats => {
                this.statsHistory.push(stats);
                if (this.statsHistory.length > this.maxHistoryFrames) {
                    this.statsHistory.shift();
                }
                this.render();
            });

        requestAnimationFrame(() => this.updateLoop());
    }

    private render() {
        const ctx = this.hudCanvas.getContext('2d')!;
        ctx.clearRect(0, 0, this.hudCanvas.width, this.hudCanvas.height);

        const latest = this.statsHistory[this.statsHistory.length - 1];
        if (!latest) return;

        // Draw text
        ctx.fillStyle = '#0f0';
        ctx.font = '11px monospace';
        ctx.fillText(`FPS: ${latest.fps.toFixed(1)}`, 10, 20);
        ctx.fillText(`Frame: ${latest.frame_time_ms.toFixed(2)}ms`, 10, 35);
        ctx.fillText(`Draw Calls: ${latest.draw_calls}`, 10, 50);
        ctx.fillText(`Triangles: ${latest.triangle_count}`, 10, 65);
        ctx.fillText(`VRAM: ${latest.vram_mb.toFixed(0)}MB`, 10, 80);
        ctx.fillText(`Physics: ${latest.physics_ms.toFixed(2)}ms`, 10, 95);
        ctx.fillText(`Audio: ${latest.audio_ms.toFixed(2)}ms`, 10, 110);

        // Draw frame time graph
        ctx.strokeStyle = '#f00';
        ctx.lineWidth = 1;
        ctx.beginPath();

        for (let i = 0; i < this.statsHistory.length; i++) {
            const x = (i / this.maxHistoryFrames) * (this.hudCanvas.width - 20) + 10;
            const y = this.hudCanvas.height - (this.statsHistory[i].frame_time_ms / 33) * 20;  // 33ms = 30 FPS baseline

            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }

        ctx.stroke();
    }
}
```

## Console Overlay

Quake-style in-game debug console (already detailed in §18.06, cross-referenced here):

```typescript
/// Console overlay for in-game command execution.
export class ConsoleOverlay {
    private visible: boolean = false;
    private commandHistory: string[] = [];
    private historyIndex: number = 0;
    private overlay: HTMLElement;

    constructor(container: HTMLElement) {
        // Toggle with backtick key
        document.addEventListener('keydown', (e) => {
            if (e.key === '`') {
                this.toggle();
            }
        });

        this.setupUI(container);
    }

    private setupUI(container: HTMLElement) {
        this.overlay = document.createElement('div');
        this.overlay.style.cssText = `
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            height: 200px;
            background: rgba(0, 0, 0, 0.9);
            color: #0f0;
            font-family: monospace;
            font-size: 12px;
            display: none;
            z-index: 10000;
        `;

        const output = document.createElement('div');
        output.className = 'console-output';
        output.style.cssText = `
            overflow-y: auto;
            height: 170px;
            padding: 10px;
            border-bottom: 1px solid #0f0;
        `;
        this.overlay.appendChild(output);

        const input = document.createElement('input');
        input.className = 'console-input';
        input.type = 'text';
        input.placeholder = '> ';
        input.style.cssText = `
            width: 100%;
            height: 30px;
            background: transparent;
            border: none;
            color: #0f0;
            font-family: monospace;
            font-size: 12px;
        `;

        input.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                this.executeCommand(input.value);
                this.commandHistory.push(input.value);
                this.historyIndex = this.commandHistory.length;
                input.value = '';
            } else if (e.key === 'ArrowUp') {
                this.historyIndex = Math.max(0, this.historyIndex - 1);
                input.value = this.commandHistory[this.historyIndex] || '';
            } else if (e.key === 'ArrowDown') {
                this.historyIndex = Math.min(this.commandHistory.length, this.historyIndex + 1);
                input.value = this.commandHistory[this.historyIndex] || '';
            }
        });

        this.overlay.appendChild(input);
        container.appendChild(this.overlay);
    }

    toggle() {
        this.visible = !this.visible;
        this.overlay.style.display = this.visible ? 'block' : 'none';
    }

    private executeCommand(command: string) {
        this.ipc.call("console.execute", [command]);
    }

    private log(message: string, level: 'info' | 'warning' | 'error' = 'info') {
        const output = this.overlay.querySelector('.console-output')!;
        const line = document.createElement('div');
        line.textContent = message;

        if (level === 'error') line.style.color = '#f00';
        else if (level === 'warning') line.style.color = '#ff0';

        output.appendChild(line);
        output.scrollTop = output.scrollHeight;
    }
}
```

## Comparison

| Feature | UE5 | Godot | Unity | **UzEngine** |
|---|---|---|---|---|
| **Animation preview** | Persona editor | Built-in | Built-in | **Viewport + timeline scrubber** |
| **Texture viewer** | Built-in | Built-in | Built-in | **Mip/channel isolation + histogram** |
| **Mesh viewer** | Built-in | Built-in | Built-in | **Wireframe/Normals/LODs/Weights** |
| **Shader debugging** | PIX integration | Limited (GDShader) | RenderDoc | **PIX + RenderDoc integration** |
| **Physics debug** | Built-in vis overlay | Built-in | Built-in | **Shapes + contacts + velocities** |
| **Audio debug** | Limited | Built-in | Limited | **Listener + sources + bus mixer** |
| **Performance HUD** | Stat console | Built-in | Built-in | **Real-time graph + stats overlay** |
| **Console** | Built-in | Built-in | Limited | **Quake-style (backtick)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Animation preview (play/pause/scrub), Texture viewer (mip levels, channel isolation), Mesh viewer (solid/wireframe), Performance HUD, Console overlay |
| **Phase 4** | All tools fully featured (normals/tangents, bone weights, physics shapes, audio sources, navmesh overlay, constraint visualization), PIX integration, bus mixer |
| **Phase 4+** | Advanced shader debugging (DXIL disassembly), register inspection, performance profiling tools (per-system cost breakdown), custom debug overlays via Lua |

