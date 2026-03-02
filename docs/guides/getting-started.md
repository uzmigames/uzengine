# Getting Started with UzEngine

This is a quick start guide to build and run UzEngine for the first time.

---

## Prerequisites (5 minutes)

### Install Dependencies

**Visual Studio 2022:**
- Download from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/)
- Install with C++ workload and DirectX 12 SDK

**CMake 3.20+:**
```powershell
# Via Chocolatey
choco install cmake

# Or download: cmake.org/download
```

**vcpkg:**
```powershell
cd c:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

**Node.js (for VSCode extension):**
- Download from [nodejs.org](https://nodejs.org/) (v18+)

**VSCode:**
- Download from [code.visualstudio.com](https://code.visualstudio.com/)

**LuaJIT (automatic via vcpkg):**
- LuaJIT 2.1 is installed automatically by vcpkg during the C++ build
- No manual installation needed
- Version locked in `vcpkg.json`

---

## Build the C++ Application (10 minutes)

### Step 1: Clone/Open the Repository

```powershell
cd e:\UzEngine
```

### Step 2: Configure CMake

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=c:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

This generates the Visual Studio project and installs vcpkg dependencies.

### Step 3: Build

```powershell
# Release build (recommended)
cmake --build build --config Release

# Or Debug build
cmake --build build --config Debug
```

**Expected output:**
```
Build succeeded.
0 Warning(s)
0 Error(s)
```

**Verify the executable:**
```powershell
ls build/Release/UzEngine.exe
ls build/Release/turbojpeg.dll  # Should be copied by POST_BUILD
```

---

## Test the Application (5 minutes)

### Run in Windowed Mode (with window visible)

```powershell
$env:UZENGINE_WINDOWED = '1'
.\build\Release\UzEngine.exe
```

You should see:
- A window titled "UzEngine PoC" with a spinning colored pyramid
- Resolution: 1280×720

Press **ESC** to close the window.

### Check WebSocket Server

Open another PowerShell and test connectivity:

```powershell
# Install wscat if you don't have it
npm install -g wscat

# Connect to the WebSocket server (in another terminal while UzEngine runs)
wscat -c ws://localhost:9002

# You should see binary JPEG frames arriving
# (wscat will show them as binary data)
```

---

## Build the VSCode Extension (5 minutes)

### Step 1: Install Dependencies

```powershell
cd vscode-uzengine
npm install
```

### Step 2: Compile

```powershell
npm run compile
```

**Expected output:**
```
Compiling TypeScript...
Successfully compiled.
```

---

## Run the Full System (10 minutes)

### Step 1: Open VSCode

```powershell
cd e:\UzEngine
code .
```

### Step 2: Open the Stream Viewer

**Option A: Command Palette**
1. Press `Ctrl+Shift+P`
2. Type: `UzEngine: Open Stream Viewer`
3. Press Enter

**Option B: Debug (F5)**
1. Open `vscode-uzengine/` folder in VSCode
2. Press **F5** to debug the extension
3. A new VSCode window opens
4. Run the command above in the new window

### Step 3: View the Stream

You should see:
- **Status bar:** `$(debug-stop) UzEngine: running` (bottom left)
- **Webview panel:** "UzEngine Stream" tab with:
  - Toolbar showing connection status, FPS counter, server URL
  - Live JPEG stream of the spinning pyramid
  - FPS counter updating every second

The engine auto-starts when you open the stream viewer.

---

---

## Phase 0: What Works Today

UzEngine is currently in **Phase 0 (Proof of Concept)**. The following features are working:

**Working:**
- DX12 rendering: 3D pyramid with smooth rotation
- Real-time JPEG streaming: 60 fps render, 30-40 fps display
- WebSocket server: Binary frame broadcasting
- IPC control: `scene.setRotation`, `scene.setBackground`, `scene.setPaused`
- VSCode extension: Live stream viewer with connection management
- MCP server: Claude Code tool access to engine control

**Not yet available (Phases 1-4):**
- Scene system (.uzscene format, node tree)
- Asset import (glTF, FBX, textures)
- Physics (Jolt)
- Audio (XAudio2)
- Lua scripting
- Editor panels (Scene Tree, Inspector, Asset Browser)
- Play mode (separate process)
- Shader hot-reload
- And much more — see [Engine Vision](../specs/engine-vision.md) for the complete roadmap

> **Note**: If you're looking to build actual game content, wait for Phase 1 (Scene System). Phase 0 is for validating the architecture and render pipeline.

---

## Your First Project (Phase 1 Preview)

Once Phase 1 is available, you'll create projects like this:

**Project structure:**
```
myproject/
├── project.uzproj           # Project config (TOML)
├── scenes/
│   └── main.uzscene         # Scene file (INI format)
├── res/
│   └── game/
│       └── hello.lua        # Lua script
├── assets/
│   ├── meshes/
│   └── textures/
└── materials/
```

**Example: `scenes/main.uzscene` (INI format)**
```ini
[uzscene version=1 name="Main"]

[node id=1 type=Node3D name="World" parent=0]

[node id=2 type=Camera name="MainCamera" parent=1]
position = [0, 2, 5]
fov = 75

[node id=3 type=Node3D name="GameLogic" parent=1]
script = "res://game/hello.lua"
```

**Example: `res/game/hello.lua` (Lua script)**
```lua
-- res://game/hello.lua
local M = {}

function M:on_start(ctx)
    ctx.log("Hello from Lua!")
    self.timer = 0
end

function M:on_update(ctx, dt)
    self.timer = self.timer + dt
    if self.timer >= 1.0 then
        ctx.log("1 second passed!")
        self.timer = 0
    end
end

function M:on_stop(ctx)
    ctx.log("Goodbye!")
end

return M
```

**Running in editor mode:**
```powershell
# Once Phase 1 is implemented
.\build\Release\UzEngine.exe --editor
```

The VSCode extension will open the scene in the viewport, and the Lua script will execute in the background. Check the engine log output to see "Hello from Lua!" printed every second.

> **Status**: Lua scripting is planned for Phase 2-3. Scene system comes first (Phase 1). See [Lua Scripting Guide](lua-scripting-guide.md) for detailed API reference.

---

## Next Steps

### Explore the Codebase

Open `src/main.cpp` to see the main loop and IPC setup.

Key files:
- `src/renderer/renderer.h/cpp` — DX12 rendering
- `src/encoder/jpeg_encoder.cpp` — JPEG encoding
- `src/network/ws_server.cpp` — WebSocket server
- `vscode-uzengine/src/stream-panel.ts` — Webview stream display

### Control the Engine

Use the MCP tools to control the engine from Claude Code:

```python
# Call uz_call to change scene parameters
uz_call("scene.setBackground", {"r": 0.0, "g": 0.05, "b": 0.15})
uz_call("scene.setRotation", {"y": 2.5})
uz_screenshot()  # Capture the result
```

See [AI Interaction Guide](ai-interaction.md) for more examples.

### Read the Full Documentation

Start with [Architecture Overview](architecture.md), then explore:
- [Streaming Pipeline](streaming-pipeline.md) — Frame lifecycle
- [IPC and MCP](ipc-mcp.md) — Control and automation
- [Build Guide](build.md) — Detailed build instructions
- [VSCode Extension](vscode-extension.md) — Extension architecture

---

## Troubleshooting

### "UzEngine.exe not found"

**Error in VSCode:**
> UzEngine.exe not found. Build the project first or set uzengine.serverExecutable.

**Fix:**
1. Build the C++ app: `cmake --build build --config Release`
2. Verify: `ls build/Release/UzEngine.exe`

### turbojpeg.dll not found

**Error:** Engine crashes on startup with missing DLL.

**Fix:**
```powershell
# Copy manually
cp c:\vcpkg\installed\x64-windows\bin\turbojpeg.dll build\Release\
```

Then rebuild:
```powershell
cmake --build build --config Release
```

### "WebSocket connection failed"

**Webview shows "Disconnected"**

**Check:**
1. Is UzEngine.exe running? (Check Task Manager)
2. Is port 9002 open?
   ```powershell
   netstat -ano | findstr :9002
   ```
3. Check "UzEngine Server" output channel in VSCode (View → Output)

**Fix:**
- Stop the server: Command palette → `UzEngine: Stop Server`
- Restart: Command palette → `UzEngine: Open Stream Viewer`

### "Cannot find ixwebsocket"

**Error during CMake configure.**

**Fix:**
```powershell
# Ensure vcpkg is installed
cd c:\vcpkg

# Reinstall
rm build
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=c:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

---

## Performance Tips

### High FPS Streaming

The extension displays 30-40 fps typical. To increase:
1. Reduce JPEG quality (less encoding time)
   - Edit `src/main.cpp` line 85: `encoder.encode(..., 85, ...)`
   - Try 70-75 for faster encoding
2. Reduce resolution (smaller JPEG files)
   - Edit `src/common/types.h`: `kDefaultWidth/Height`

### CPU Usage

If CPU is maxed out:
- Reduce render FPS target (currently 60)
- Pause the animation: `uz_call("scene.setPaused", {"paused": true})`
- Check if encoding is slow: monitor `uz_status()` FPS

---

## What's Next?

1. **Learn the architecture:** Read [Architecture Overview](architecture.md)
2. **Modify the scene:** Change pyramid color, rotation speed (see [IPC and MCP](ipc-mcp.md))
3. **Add a shader:** Write custom HLSL and reload via IPC
4. **Integrate with AI:** Use the MCP tools to control the engine from Claude Code
5. **Deploy:** Package the extension as `.vsix` and share

---

## Getting Help

- **Build errors?** → [Build Guide](build.md)
- **Extension issues?** → [VSCode Extension Reference](vscode-extension.md)
- **How to control the engine?** → [IPC and MCP Guide](ipc-mcp.md)
- **Understanding the pipeline?** → [Streaming Pipeline](streaming-pipeline.md)
- **Overall design?** → [Architecture Overview](architecture.md)

Happy hacking!
