/// VFX Viewport Demo — Input Handling
///
/// Extracted from vfx_viewport_demo.cpp to keep files under 1500 lines.
/// Contains HandleInput() with keyboard/mouse controls for camera,
/// VFX parameters, presets, and editor UI.

#include "vfx_viewport_demo.h"
#include "platform_window.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Input Handling
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::HandleInput(float dt) {
    const auto* input = GetInput();

    // ── VFX editor UI (mouse-driven overlay — consumes mouse when hovered) ──
    m_editorUI.HandleMouse(input, dt);
    if (m_editorUI.WantsMouseInput()) {
        // UI consumed the mouse — still allow keyboard input to pass through
        // but skip camera/gizmo mouse handling below.
    }

    // ── VFX parameter panel (F10 toggles, consumes input when visible) ──
    if (input->IsKeyJustPressed(Key::F10)) {
        m_paramPanel.Toggle();
    }
    if (m_paramPanel.IsVisible()) {
        bool needsRestart = false;
        if (m_paramPanel.HandleInput(input, needsRestart)) {
            if (needsRestart) {
                auto* cmd = GetCommandList();
                ResetAllEmitters(cmd);
            }
            return; // Panel consumed the input — skip all other handlers.
        }
    }

    // ── VFX module stack panel (Q toggles, consumes input when visible) ──
    if (input->IsKeyJustPressed(Key::Q) && !m_paramPanel.IsVisible()) {
        m_moduleStack.Toggle();
    }
    if (m_moduleStack.HandleInput(input)) {
        return; // Module stack consumed the input — skip all other handlers.
    }

    // Debug view mode (F1-F11)
    ApplyDebugViewKeyboard(input);

    // Material cycling (Left/Right arrows)
    if (input->IsKeyJustPressed(Key::Left)) {
        m_currentMaterial = (m_currentMaterial + kMaterialCount - 1) % kMaterialCount;
        UZ_LOG(App, Info, "Material: {}", kMaterials[m_currentMaterial].name);
    }
    if (input->IsKeyJustPressed(Key::Right)) {
        m_currentMaterial = (m_currentMaterial + 1) % kMaterialCount;
        UZ_LOG(App, Info, "Material: {}", kMaterials[m_currentMaterial].name);
    }

    // Mesh cycling (Up/Down arrows)
    if (input->IsKeyJustPressed(Key::Up)) {
        m_currentMesh = (m_currentMesh + kMeshCount - 1) % kMeshCount;
        UZ_LOG(App, Info, "Mesh: {}", kMeshNames[m_currentMesh]);
    }
    if (input->IsKeyJustPressed(Key::Down)) {
        m_currentMesh = (m_currentMesh + 1) % kMeshCount;
        UZ_LOG(App, Info, "Mesh: {}", kMeshNames[m_currentMesh]);
    }

    // Time-of-day (T / Shift+T)
    if (input->IsKeyJustPressed(Key::T)) {
        if (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift)) {
            m_sunElevation -= 5.0f;
        } else {
            m_sunElevation += 5.0f;
        }
        m_sunElevation = std::clamp(m_sunElevation, -10.0f, 90.0f);
        UZ_LOG(App, Info, "Sun elevation: {:.1f} deg", m_sunElevation);
    }

    // Shadow bias (O = decrease, P-adjacent = use Num7/Num8)
    if (input->IsKeyJustPressed(Key::Num7)) {
        m_shadowBias = std::max(0.0001f, m_shadowBias * 0.5f);
        UZ_LOG(App, Info, "Shadow bias: {:.4f}", m_shadowBias);
    }
    if (input->IsKeyJustPressed(Key::Num8)) {
        m_shadowBias = std::min(0.05f, m_shadowBias * 2.0f);
        UZ_LOG(App, Info, "Shadow bias: {:.4f}", m_shadowBias);
    }

    // Exposure (Num9 = increase, Num0 = decrease)
    if (input->IsKeyJustPressed(Key::Num9)) {
        m_exposure = std::min(10.0f, m_exposure * 1.2f);
        UZ_LOG(App, Info, "Exposure: {:.2f}", m_exposure);
    }
    if (input->IsKeyJustPressed(Key::Num0)) {
        m_exposure = std::max(0.1f, m_exposure / 1.2f);
        UZ_LOG(App, Info, "Exposure: {:.2f}", m_exposure);
    }

    // Vortex force mode cycle (F key)
    // @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
    if (input->IsKeyJustPressed(Key::F)) {
        m_vortexMode = (m_vortexMode + 1) % 3; // 0=Off, 1=Tornado, 2=Whirlpool
        static const char* kVortexModeNames[] = { "OFF", "TORNADO", "WHIRLPOOL" };
        UZ_LOG(App, Info, "[F] Vortex mode: {}", kVortexModeNames[m_vortexMode]);
    }

    // Wind zone mode cycle (N key)
    // @ref Stam 1999 "Stable Fluids" -- wind zone accumulation
    if (input->IsKeyJustPressed(Key::N)) {
        m_windMode = (m_windMode + 1) % 3; // 0=Off, 1=Ambient, 2=Gust
        static const char* kWindModeNames[] = { "OFF", "AMBIENT", "GUST" };
        UZ_LOG(App, Info, "[N] Wind mode: {}", kWindModeNames[m_windMode]);
    }

    // Kill volume mode cycle (K key)
    // @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    if (input->IsKeyJustPressed(Key::K)) {
        m_killVolumeMode = (m_killVolumeMode + 1) % 4; // 0=Off, 1=InvWall, 2=Ground, 3=Sphere
        static const char* kKillModeNames[] = { "OFF", "INVISIBLE_WALL", "GROUND_BOUNDARY", "SPHERE_ZONE" };
        UZ_LOG(App, Info, "[K] Kill volume mode: {}", kKillModeNames[m_killVolumeMode]);
    }

    // Vignette (V key)
    if (input->IsKeyJustPressed(Key::V)) {
        m_vignetteIntensity = (m_vignetteIntensity > 0.01f) ? 0.0f : 0.4f;
        UZ_LOG(App, Info, "Vignette: {}", m_vignetteIntensity > 0.01f ? "on" : "off");
    }

    // Post-processing toggle (P key)
    if (input->IsKeyJustPressed(Key::P)) {
        m_postProcessEnabled = !m_postProcessEnabled;
        UZ_LOG(App, Info, "Post-processing: {}", m_postProcessEnabled ? "ACES" : "linear");
    }

    // Shadow map debug overlay (G key)
    if (input->IsKeyJustPressed(Key::G)) {
        m_showShadowDebug = !m_showShadowDebug;
        UZ_LOG(App, Info, "Shadow debug: {}", m_showShadowDebug ? "on" : "off");
    }

    // Bloom toggle (B key)
    if (input->IsKeyJustPressed(Key::B)) {
        m_bloomEnabled = !m_bloomEnabled;
        UZ_LOG(App, Info, "Bloom: {}", m_bloomEnabled ? "on" : "off");
    }

    // Lit particles toggle (J key) — applies to all emitters
    // @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse NdotL
    if (input->IsKeyJustPressed(Key::J)) {
        const uint32_t newMode = (ActiveEmitter().config.litMode == 0) ? 1u : 0u;
        for (auto& e : m_emitters) e.config.litMode = newMode;
        UZ_LOG(App, Info, "[J] Lit particles: {}", newMode ? "ON" : "OFF");
    }

    // VFX restart (Delete key) — clears all particles and fires new burst/restarts
    if (input->IsKeyJustPressed(Key::Delete)) {
        auto* cmd = GetCommandList();
        ResetAllEmitters(cmd);
        // Start sub-emitter countdown if enabled — fires secondary sparks burst
        // after kSubEmitterDelay seconds at the same world position.
        if (m_subEmitterEnabled) {
            m_subEmitterTimer = kSubEmitterDelay;
        }
        UZ_LOG(App, Info, "[VFX] Particles reset — emission restarted{}",
               m_subEmitterEnabled ? " (sub-emitter armed)" : "");
    }

    // Toggle burst/continuous mode (Insert key) — applies to all emitters
    if (input->IsKeyJustPressed(Key::Insert)) {
        const bool newMode = !ActiveEmitter().burstMode;
        for (auto& e : m_emitters) {
            e.burstMode = newMode;
            if (newMode) e.burstPending = true;
        }
        UZ_LOG(App, Info, "[VFX] Mode: {} (active emitter burst count={})",
               newMode ? "Burst" : "Continuous", ActiveEmitter().burstCount);
    }

    // VFX material preset cycling (M key)
    if (input->IsKeyJustPressed(Key::M)) {
        m_currentMaterialIndex = (m_currentMaterialIndex + 1) % kVFXMaterialCount;
        static const char* kVFXMaterialNames[] = {
            "Fire_Additive", "Smoke_Translucent", "Sparks_Additive",
            "Magic_Additive", "Debris_AlphaBlend", "Distortion_PremulAlpha"
        };
        UZ_LOG(App, Info, "[VFX] Material: {} (blend={}, emissive={:.1f}, tint=[{:.1f},{:.1f},{:.1f}])",
               kVFXMaterialNames[m_currentMaterialIndex],
               m_vfxMaterials[m_currentMaterialIndex].blendMode,
               m_vfxMaterials[m_currentMaterialIndex].emissiveIntensity,
               m_vfxMaterials[m_currentMaterialIndex].baseColorTintR,
               m_vfxMaterials[m_currentMaterialIndex].baseColorTintG,
               m_vfxMaterials[m_currentMaterialIndex].baseColorTintB);
    }

    // Color ramp LUT cycling (N key) — Off -> Fire -> Ice -> Rainbow -> Off
    // Applies the same ramp to ALL emitters so the compound effect is consistent.
    if (input->IsKeyJustPressed(Key::N)) {
        m_currentRampIndex = (m_currentRampIndex + 1);
        if (m_currentRampIndex >= kColorRampCount) {
            m_currentRampIndex = -1; // wrap back to legacy lerp
        }

        if (m_currentRampIndex < 0) {
            for (auto& e : m_emitters) e.system->ClearColorRampLUT();
            UZ_LOG(App, Info, "[VFX] Color ramp: Off (legacy lerp)");
        } else {
            auto* device = GetDevice();
            auto* cmd = GetCommandList();
            UploadContext upload(device);
            bool ok = true;
            for (auto& e : m_emitters) {
                if (!e.system->UploadColorRampLUT(upload, cmd, m_colorRamps[m_currentRampIndex])) {
                    ok = false;
                }
            }
            if (ok) {
                static const char* kRampNames[] = {"Fire", "Ice", "Rainbow"};
                UZ_LOG(App, Info, "[VFX] Color ramp: {}", kRampNames[m_currentRampIndex]);
            } else {
                UZ_LOG(App, Error, "[VFX] Failed to upload color ramp LUT");
                m_currentRampIndex = -1;
            }
        }
    }

    // Space key fireball removed — conflicts with camera jump/movement.
    // Use the [Fireball] button in the bottom toolbar or File menu instead.

    // VFX preset cycling (Tab / Shift+Tab) — skip if Ctrl is held (Ctrl+Tab = compound preset)
    const bool ctrlDown = input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl);
    if (input->IsKeyJustPressed(Key::Tab) && !ctrlDown) {
        if (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift)) {
            // Toggle VFX on/off
            m_vfxEnabled = !m_vfxEnabled;
            UZ_LOG(App, Info, "VFX: {}", m_vfxEnabled ? "enabled" : "disabled");
        } else {
            // Cycle through file-based presets (applied to the active emitter).
            // Fall back to built-in presets if the list is empty.
            if (!m_presetFiles.empty()) {
                const int nextIdx = (m_vfxPresetIndex + 1) % static_cast<int>(m_presetFiles.size());
                ActiveEmitter().spawnAccum = 0.0f;
                LoadPreset(nextIdx);
            } else {
                int presetCount = 0;
                const VFXPreset* presets = GetVFXPresets(presetCount);
                m_vfxPresetIndex = (m_vfxPresetIndex + 1) % presetCount;
                auto& ae = ActiveEmitter();
                ae.config = presets[m_vfxPresetIndex].config;
                ae.spawnRate = presets[m_vfxPresetIndex].spawnRate;
                ae.spawnAccum = 0.0f;
                UZ_LOG(App, Info, "VFX preset: {} (built-in fallback)", presets[m_vfxPresetIndex].name);
            }
        }
    }

    // Compound preset cycling (Ctrl+Tab) — Single -> Fire -> Explosion -> Magic
    if (input->IsKeyJustPressed(Key::Tab) &&
        (input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl))) {
        const int next = (static_cast<int>(m_compoundPreset) + 1)
                         % static_cast<int>(VFXCompoundPreset::Count);
        auto* device = GetDevice();
        auto* shaders = GetShaderManager();
        auto* cmd = GetCommandList();
        ShutdownAllEmitters(device);
        BuildCompoundPreset(static_cast<VFXCompoundPreset>(next));
        InitAllEmitters(device, shaders, cmd);
        // Re-point the parameter panel to the new active emitter
        {
            auto& ae = ActiveEmitter();
            m_paramPanel.Init(&ae.config, &ae.spawnRate, &ae.burstCount,
                              &ae.burstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled);
            m_moduleStack.Init(&ae.config, &m_subEmitterEnabled, &m_frustumCullingEnabled);
        }
        static const char* kCompoundNames[] = {"Single", "Fire", "Explosion", "Magic"};
        UZ_LOG(App, Info, "[VFX] Compound preset: {} ({} emitters)",
               kCompoundNames[next], static_cast<int>(m_emitters.size()));
    }

    // Emitter selection cycling (Ctrl+1..8 selects emitter index)
    for (int i = 0; i < static_cast<int>(m_emitters.size()) && i < kMaxEmitters; ++i) {
        const Key numKey = static_cast<Key>(static_cast<int>(Key::Num1) + i);
        if (input->IsKeyJustPressed(numKey) &&
            (input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl))) {
            m_activeEmitterIndex = i;
            auto& ae = ActiveEmitter();
            // Re-point the parameter panel to the new active emitter
            m_paramPanel.Init(&ae.config, &ae.spawnRate, &ae.burstCount,
                              &ae.burstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled);
            m_moduleStack.Init(&ae.config, &m_subEmitterEnabled, &m_frustumCullingEnabled);
            UZ_LOG(App, Info, "[VFX] Active emitter: {} ({})",
                   i, ae.name.c_str());
        }
    }

    // VFX parameter tweaking (number keys — plain = basic, Shift = advanced)
    // Ctrl+number is reserved for emitter selection, so skip tweaking when Ctrl is held.
    const bool shift = input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift);
    if (!shift && !ctrlDown) {
        // 1/2: Spawn rate ↑/↓
        if (input->IsKeyJustPressed(Key::Num1)) {
            ActiveEmitter().spawnRate = std::min(5000.0f, ActiveEmitter().spawnRate * 1.5f);
            UZ_LOG(App, Info, "[VFX] Spawn rate: {:.0f}/s", ActiveEmitter().spawnRate);
        }
        if (input->IsKeyJustPressed(Key::Num2)) {
            ActiveEmitter().spawnRate = std::max(10.0f, ActiveEmitter().spawnRate / 1.5f);
            UZ_LOG(App, Info, "[VFX] Spawn rate: {:.0f}/s", ActiveEmitter().spawnRate);
        }
        // 3/4: Particle size ↑/↓
        if (input->IsKeyJustPressed(Key::Num3)) {
            ActiveEmitter().config.initialSize = std::min(5.0f, ActiveEmitter().config.initialSize * 1.3f);
            UZ_LOG(App, Info, "[VFX] Size: {:.3f}", ActiveEmitter().config.initialSize);
        }
        if (input->IsKeyJustPressed(Key::Num4)) {
            ActiveEmitter().config.initialSize = std::max(0.02f, ActiveEmitter().config.initialSize / 1.3f);
            UZ_LOG(App, Info, "[VFX] Size: {:.3f}", ActiveEmitter().config.initialSize);
        }
        // 5/6: Gravity Z ↑/↓ (Z-up)
        if (input->IsKeyJustPressed(Key::Num5)) {
            ActiveEmitter().config.gravity[2] -= 2.0f;
            UZ_LOG(App, Info, "[VFX] Gravity Z: {:.1f}", ActiveEmitter().config.gravity[2]);
        }
        if (input->IsKeyJustPressed(Key::Num6)) {
            ActiveEmitter().config.gravity[2] += 2.0f;
            UZ_LOG(App, Info, "[VFX] Gravity Z: {:.1f}", ActiveEmitter().config.gravity[2]);
        }
        // 7/8: Velocity speed ↑/↓
        if (input->IsKeyJustPressed(Key::Num7)) {
            ActiveEmitter().config.spawnVelocity[2] = std::min(30.0f, ActiveEmitter().config.spawnVelocity[2] * 1.3f);
            UZ_LOG(App, Info, "[VFX] Velocity Z: {:.1f}", ActiveEmitter().config.spawnVelocity[2]);
        }
        if (input->IsKeyJustPressed(Key::Num8)) {
            ActiveEmitter().config.spawnVelocity[2] = std::max(0.5f, ActiveEmitter().config.spawnVelocity[2] / 1.3f);
            UZ_LOG(App, Info, "[VFX] Velocity Z: {:.1f}", ActiveEmitter().config.spawnVelocity[2]);
        }
        // 9/0: Spread angle ↑/↓
        if (input->IsKeyJustPressed(Key::Num9)) {
            ActiveEmitter().config.spreadAngle = std::min(3.14159f, ActiveEmitter().config.spreadAngle + 0.1f);
            UZ_LOG(App, Info, "[VFX] Spread: {:.2f} rad", ActiveEmitter().config.spreadAngle);
        }
        if (input->IsKeyJustPressed(Key::Num0)) {
            ActiveEmitter().config.spreadAngle = std::max(0.0f, ActiveEmitter().config.spreadAngle - 0.1f);
            UZ_LOG(App, Info, "[VFX] Spread: {:.2f} rad", ActiveEmitter().config.spreadAngle);
        }
    }
    // Shift+1/Shift+2: Lifetime ↑/↓
    if (input->IsKeyJustPressed(Key::Num1) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.minLifetime = std::min(10.0f, ActiveEmitter().config.minLifetime * 1.3f);
        ActiveEmitter().config.maxLifetime = std::min(15.0f, ActiveEmitter().config.maxLifetime * 1.3f);
        UZ_LOG(App, Info, "[VFX] Lifetime: {:.1f}-{:.1f}s", ActiveEmitter().config.minLifetime, ActiveEmitter().config.maxLifetime);
    }
    if (input->IsKeyJustPressed(Key::Num2) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.minLifetime = std::max(0.1f, ActiveEmitter().config.minLifetime / 1.3f);
        ActiveEmitter().config.maxLifetime = std::max(0.2f, ActiveEmitter().config.maxLifetime / 1.3f);
        UZ_LOG(App, Info, "[VFX] Lifetime: {:.1f}-{:.1f}s", ActiveEmitter().config.minLifetime, ActiveEmitter().config.maxLifetime);
    }
    // Shift+3/Shift+4: Emissive intensity ↑/↓
    if (input->IsKeyJustPressed(Key::Num3) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.emissiveIntensity = std::min(10.0f, ActiveEmitter().config.emissiveIntensity + 0.5f);
        UZ_LOG(App, Info, "[VFX] Emissive: {:.1f}", ActiveEmitter().config.emissiveIntensity);
    }
    if (input->IsKeyJustPressed(Key::Num4) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.emissiveIntensity = std::max(0.0f, ActiveEmitter().config.emissiveIntensity - 0.5f);
        UZ_LOG(App, Info, "[VFX] Emissive: {:.1f}", ActiveEmitter().config.emissiveIntensity);
    }
    // Shift+5/Shift+6: Damping ↑/↓
    if (input->IsKeyJustPressed(Key::Num5) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.damping = std::min(2.0f, ActiveEmitter().config.damping + 0.1f);
        UZ_LOG(App, Info, "[VFX] Damping: {:.2f}", ActiveEmitter().config.damping);
    }
    if (input->IsKeyJustPressed(Key::Num6) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.damping = std::max(0.0f, ActiveEmitter().config.damping - 0.1f);
        UZ_LOG(App, Info, "[VFX] Damping: {:.2f}", ActiveEmitter().config.damping);
    }
    // Shift+7/Shift+8: Spawn radius ↑/↓
    if (input->IsKeyJustPressed(Key::Num7) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.spawnRadius = std::min(5.0f, ActiveEmitter().config.spawnRadius + 0.2f);
        UZ_LOG(App, Info, "[VFX] Spawn radius: {:.1f}", ActiveEmitter().config.spawnRadius);
    }
    if (input->IsKeyJustPressed(Key::Num8) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.spawnRadius = std::max(0.0f, ActiveEmitter().config.spawnRadius - 0.2f);
        UZ_LOG(App, Info, "[VFX] Spawn radius: {:.1f}", ActiveEmitter().config.spawnRadius);
    }

    // Trail toggle (Y key)
    if (input->IsKeyJustPressed(Key::Y)) {
        m_trailEnabled = !m_trailEnabled;
        UZ_LOG(App, Info, "Trails: {}", m_trailEnabled ? "enabled" : "disabled");
    }

    // Decal toggle (D key)
    if (input->IsKeyJustPressed(Key::D)) {
        m_decalEnabled = !m_decalEnabled;
        UZ_LOG(App, Info, "Decals: {}", m_decalEnabled ? "enabled" : "disabled");
    }

    // Distortion toggle (X key)
    if (input->IsKeyJustPressed(Key::X)) {
        m_distortionEnabled = !m_distortionEnabled;
        UZ_LOG(App, Info, "Distortion: {}", m_distortionEnabled ? "enabled" : "disabled");
    }

    // Point lights toggle (L key)
    if (input->IsKeyJustPressed(Key::L)) {
        m_pointLightsEnabled = !m_pointLightsEnabled;
        UZ_LOG(App, Info, "Point lights: {}", m_pointLightsEnabled ? "enabled" : "disabled");
    }

    // ── Gizmo mode keys (W/E/R) — only when an entity is selected ──────
    // When nothing is selected, R still controls environment rotation.
    if (m_selectedMeshIndex != UINT32_MAX) {
        if (input->IsKeyJustPressed(Key::W)) {
            m_gizmoMode = GizmoMode::Translate;
            UZ_LOG(App, Info, "Gizmo mode: Translate");
        }
        if (input->IsKeyJustPressed(Key::E)) {
            m_gizmoMode = GizmoMode::Rotate;
            UZ_LOG(App, Info, "Gizmo mode: Rotate");
        }
        if (input->IsKeyJustPressed(Key::R)) {
            m_gizmoMode = GizmoMode::Scale;
            UZ_LOG(App, Info, "Gizmo mode: Scale");
        }
    } else {
        // Environment rotation (R key — 15 deg step, Shift+R = reverse)
        if (input->IsKeyJustPressed(Key::R)) {
            if (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift)) {
                m_envRotation -= 15.0f;
            } else {
                m_envRotation += 15.0f;
            }
            // Wrap to [0, 360)
            m_envRotation = std::fmod(m_envRotation + 360.0f, 360.0f);
            UZ_LOG(App, Info, "Environment rotation: {:.0f} deg", m_envRotation);
        }
        // Sub-emitter toggle (E key — no selection) — Task 3.87.0
        // When enabled, pressing Delete fires the primary burst AND starts a
        // kSubEmitterDelay-second countdown that dispatches a secondary sparks
        // burst at the same world position. This is a CPU-side mechanism: no
        // GPU readback, no per-particle death flags.
        if (input->IsKeyJustPressed(Key::E)) {
            m_subEmitterEnabled = !m_subEmitterEnabled;
            // Cancel any in-flight countdown when disabling, so a stale timer
            // from a previous burst does not fire after the mode is turned off.
            if (!m_subEmitterEnabled) {
                m_subEmitterTimer = -1.0f;
            }
            UZ_LOG(App, Info, "[VFX] Sub-emitter: {} ({} sparks after {:.2f}s)",
                   m_subEmitterEnabled ? "ON" : "OFF",
                   m_subEmitterCount, kSubEmitterDelay);
        }
    }

    // Object rotation (Alt + LMB drag) — only when NOT dragging gizmo
    if (input->IsKeyDown(Key::LeftAlt) && input->IsMouseDown(MouseButton::Left)
        && !m_gizmoInteraction.IsDragging()) {
        constexpr float kRotSensitivity = 0.3f;
        m_objectYaw   += input->GetMouseDeltaX() * kRotSensitivity;
        m_objectPitch += input->GetMouseDeltaY() * kRotSensitivity;
        m_objectPitch = std::clamp(m_objectPitch, -89.0f, 89.0f);
    }

    // Background preset cycling (C key)
    if (input->IsKeyJustPressed(Key::C)) {
        m_bgPreset = (m_bgPreset + 1) % kBgPresetCount;
        static const char* kBgNames[] = {"Sky", "Dark Gray", "Black", "White", "Neutral Gray"};
        UZ_LOG(App, Info, "Background: {}", kBgNames[m_bgPreset]);
    }

    // Outline toggle (O key)
    if (input->IsKeyJustPressed(Key::O)) {
        m_outlineEnabled = !m_outlineEnabled;
        UZ_LOG(App, Info, "Outline: {}", m_outlineEnabled ? "enabled" : "disabled");
    }

    // Curl noise toggle (N key) — toggles curl noise force on/off for active emitter
    if (input->IsKeyJustPressed(Key::N)) {
        if (ActiveEmitter().config.curlStrength > 0.0f) {
            ActiveEmitter().config.curlStrength = 0.0f;
            UZ_LOG(App, Info, "[N] Curl noise: OFF");
        } else {
            ActiveEmitter().config.curlStrength = 2.0f;
            UZ_LOG(App, Info, "[N] Curl noise: ON  strength=2.0");
        }
    }

    // Escape — deselect
    if (input->IsKeyJustPressed(Key::Escape)) {
        if (m_selectedMeshIndex != UINT32_MAX) {
            m_selectedMeshIndex = UINT32_MAX;
            m_gizmoInteraction.EndDrag();
            UZ_LOG(App, Info, "Deselected");
        }
    }

    // ── Gizmo interaction (hover + drag) ────────────────────────────────
    // Every frame when a mesh is selected: hit-test the gizmo, handle drag.
    {
        const float mouseX = input->GetMouseX();
        const float mouseY = input->GetMouseY();

        const float vpW = static_cast<float>(GetWidth());
        const float vpH = static_cast<float>(GetHeight());
        const Camera& cam = GetCamera();

        if (m_selectedMeshIndex != UINT32_MAX && m_gizmoRenderer.IsInitialized()) {
            const auto& xform = m_meshTransforms[m_selectedMeshIndex];

            // Compute constant screen-space scale factor for gizmo hit testing.
            // Same formula as GizmoRenderer: 120px / (dist * tan(fov/2) * 2 / vpH).
            const float dist = (cam.GetPosition() - xform.position).Length();
            constexpr float kTargetPx = 120.0f;
            const float fovRad = cam.GetFovY() * (3.14159265f / 180.0f);
            const float screenScale = (dist > 0.001f)
                ? kTargetPx / (dist * std::tan(fovRad * 0.5f) * 2.0f / vpH)
                : 1.0f;

            if (m_gizmoInteraction.IsDragging()) {
                // Update drag — compute transform delta
                Vec3 deltaPos{};
                Quat deltaRot = Quat::Identity();
                Vec3 deltaScale{};
                const bool snap = input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl);
                float snapGrid = kDefaultTranslateSnap;
                if (m_gizmoMode == GizmoMode::Rotate) snapGrid = kDefaultRotateSnap;
                else if (m_gizmoMode == GizmoMode::Scale) snapGrid = kDefaultScaleSnap;

                if (m_gizmoInteraction.UpdateDrag(mouseX, mouseY, vpW, vpH, cam,
                                                   snap, snapGrid,
                                                   deltaPos, deltaRot, deltaScale)) {
                    auto& xf = m_meshTransforms[m_selectedMeshIndex];
                    if (m_gizmoMode == GizmoMode::Translate) {
                        xf.position = m_gizmoInteraction.GetDragStartPos() + deltaPos;
                    } else if (m_gizmoMode == GizmoMode::Rotate) {
                        xf.rotation = deltaRot * m_gizmoInteraction.GetDragStartRot();
                    } else if (m_gizmoMode == GizmoMode::Scale) {
                        xf.scale = m_gizmoInteraction.GetDragStartScale() + deltaScale;
                    }
                }

                // End drag on LMB release
                if (input->IsMouseJustReleased(MouseButton::Left)) {
                    m_gizmoInteraction.EndDrag();
                }
            } else {
                // Hit-test gizmo for hover highlight
                auto hitResult = m_gizmoInteraction.HitTest(
                    mouseX, mouseY, vpW, vpH, cam,
                    xform.position, screenScale,
                    static_cast<uint8_t>(m_gizmoMode),
                    static_cast<uint8_t>(m_gizmoSpace),
                    xform.rotation);
                m_gizmoInteraction.SetHoveredAxis(hitResult);

                // Begin drag on LMB press when hovering a gizmo axis
                if (input->IsMouseJustPressed(MouseButton::Left)
                    && hitResult != GizmoHitResult::None
                    && !input->IsKeyDown(Key::LeftAlt)) {
                    m_gizmoInteraction.BeginDrag(
                        hitResult, mouseX, mouseY, vpW, vpH, cam,
                        xform.position, xform.rotation, xform.scale,
                        screenScale,
                        static_cast<uint8_t>(m_gizmoMode),
                        static_cast<uint8_t>(m_gizmoSpace));
                }
                // Click-to-select (LMB click, not on gizmo, not Alt-held)
                else if (input->IsMouseJustPressed(MouseButton::Left)
                         && hitResult == GizmoHitResult::None
                         && !input->IsKeyDown(Key::LeftAlt)
                         && !input->IsKeyDown(Key::RightAlt)) {
                    // Ray-sphere intersection: test against the current mesh's bounding sphere.
                    // The main mesh sits at m_meshTransforms[m_currentMesh].position.
                    // Use a conservative bounding sphere radius of 1.5 (most procedural meshes
                    // fit within a radius of ~1.0-1.5).
                    const auto& meshXform = m_meshTransforms[m_currentMesh];
                    const Mat4 invVP = cam.GetViewProjection().Inverse();

                    // Unproject mouse position to a world-space ray
                    const float ndcX = (mouseX / vpW) * 2.0f - 1.0f;
                    const float ndcY = 1.0f - (mouseY / vpH) * 2.0f; // Flip Y: screen top=+1
                    const Vec4 nearClip = invVP * Vec4(ndcX, ndcY, 1.0f, 1.0f); // Reverse-Z: near=1
                    const Vec4 farClip  = invVP * Vec4(ndcX, ndcY, 0.0f, 1.0f); // Reverse-Z: far=0
                    const Vec3 nearW = Vec3(nearClip.x, nearClip.y, nearClip.z) / nearClip.w;
                    const Vec3 farW  = Vec3(farClip.x, farClip.y, farClip.z) / farClip.w;
                    const Vec3 rayDir = (farW - nearW).Normalized();

                    // Ray-sphere test: |P + t*D - C|^2 = r^2
                    const Vec3 oc = nearW - meshXform.position;
                    constexpr float kBoundingRadius = 1.5f;
                    const float a = rayDir.Dot(rayDir);
                    const float b = 2.0f * oc.Dot(rayDir);
                    const float c = oc.Dot(oc) - kBoundingRadius * kBoundingRadius;
                    const float discriminant = b * b - 4.0f * a * c;

                    if (discriminant >= 0.0f) {
                        m_selectedMeshIndex = static_cast<uint32_t>(m_currentMesh);
                        UZ_LOG(App, Info, "Selected mesh: {}", kMeshNames[m_currentMesh]);
                    } else {
                        m_selectedMeshIndex = UINT32_MAX;
                    }
                }
            }
        } else if (m_selectedMeshIndex == UINT32_MAX) {
            // No selection — still support click-to-select
            if (input->IsMouseJustPressed(MouseButton::Left)
                && !input->IsKeyDown(Key::LeftAlt)
                && !input->IsKeyDown(Key::RightAlt)
                && !input->IsMouseDown(MouseButton::Right)) {
                const auto& meshXform = m_meshTransforms[m_currentMesh];
                const Mat4 invVP = cam.GetViewProjection().Inverse();
                const float ndcX = (mouseX / vpW) * 2.0f - 1.0f;
                const float ndcY = 1.0f - (mouseY / vpH) * 2.0f;
                const Vec4 nearClip = invVP * Vec4(ndcX, ndcY, 1.0f, 1.0f);
                const Vec4 farClip  = invVP * Vec4(ndcX, ndcY, 0.0f, 1.0f);
                const Vec3 nearW = Vec3(nearClip.x, nearClip.y, nearClip.z) / nearClip.w;
                const Vec3 farW  = Vec3(farClip.x, farClip.y, farClip.z) / farClip.w;
                const Vec3 rayDir = (farW - nearW).Normalized();
                const Vec3 oc = nearW - meshXform.position;
                constexpr float kBoundingRadius = 1.5f;
                const float a = rayDir.Dot(rayDir);
                const float b = 2.0f * oc.Dot(rayDir);
                const float c = oc.Dot(oc) - kBoundingRadius * kBoundingRadius;
                const float discriminant = b * b - 4.0f * a * c;
                if (discriminant >= 0.0f) {
                    m_selectedMeshIndex = static_cast<uint32_t>(m_currentMesh);
                    UZ_LOG(App, Info, "Selected mesh: {}", kMeshNames[m_currentMesh]);
                }
            }
        }
    }

    // Frustum culling toggle (Ctrl+F) — skips VFX dispatch/render when emitter is off-screen
    if (input->IsKeyJustPressed(Key::F) &&
        (input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl))) {
        m_frustumCullingEnabled = !m_frustumCullingEnabled;
        UZ_LOG(App, Info, "[Ctrl+F] VFX frustum culling: {} (culled frames so far: {})",
               m_frustumCullingEnabled ? "ON" : "OFF", m_culledFrameCount);
    }

    // Help overlay (H key) — prints keyboard shortcut reference to console
    if (input->IsKeyJustPressed(Key::H)) {
        UZ_LOG(App, Info, "=== VFX Viewport Controls ===");
        UZ_LOG(App, Info, "  WASD / Space / Ctrl  — Camera movement (Shift = sprint)");
        UZ_LOG(App, Info, "  RMB + Mouse          — Mouse look (yaw/pitch)");
        UZ_LOG(App, Info, "  MMB + Mouse          — Orbit camera");
        UZ_LOG(App, Info, "  Scroll               — Dolly zoom");
        UZ_LOG(App, Info, "  Alt + LMB + Mouse    — Rotate object");
        UZ_LOG(App, Info, "  LMB (click mesh)     — Select mesh");
        UZ_LOG(App, Info, "  Escape               — Deselect");
        UZ_LOG(App, Info, "  W (when selected)    — Gizmo: Translate mode");
        UZ_LOG(App, Info, "  E (when selected)    — Gizmo: Rotate mode");
        UZ_LOG(App, Info, "  R (when selected)    — Gizmo: Scale mode");
        UZ_LOG(App, Info, "  O                    — Toggle outline");
        UZ_LOG(App, Info, "  Ctrl + drag          — Snap gizmo");
        UZ_LOG(App, Info, "  Left/Right Arrows    — Cycle material presets");
        UZ_LOG(App, Info, "  Up/Down Arrows       — Cycle mesh types");
        UZ_LOG(App, Info, "  T / Shift+T          — Sun elevation +/- 5 deg");
        UZ_LOG(App, Info, "  R (no selection)     — Environment rotation +/- 15 deg");
        UZ_LOG(App, Info, "  L                    — Toggle point lights");
        UZ_LOG(App, Info, "  B                    — Toggle bloom");
        UZ_LOG(App, Info, "  C                    — Cycle background (Sky/Gray/Black/White)");
        UZ_LOG(App, Info, "  V                    — Toggle vignette");
        UZ_LOG(App, Info, "  P                    — Toggle post-processing (ACES/linear)");
        UZ_LOG(App, Info, "  G                    — Toggle shadow debug overlay");
        UZ_LOG(App, Info, "  Tab                  — Cycle VFX presets (Shift+Tab = toggle)");
        UZ_LOG(App, Info, "  Ctrl+Tab             — Cycle compound presets (Single/Fire/Explosion/Magic)");
        UZ_LOG(App, Info, "  Ctrl+1..8            — Select emitter (multi-emitter mode)");
        UZ_LOG(App, Info, "  Delete               — Reset particles + fire burst");
        UZ_LOG(App, Info, "  Insert               — Toggle Burst / Continuous mode");
        UZ_LOG(App, Info, "  E (no selection)     — Toggle sub-emitter (secondary sparks burst after {:.2f}s)",
               kSubEmitterDelay);
        UZ_LOG(App, Info, "  M                    — Cycle VFX material presets");
        UZ_LOG(App, Info, "  Fireball             — Use toolbar [Fireball] button or File menu");
        UZ_LOG(App, Info, "  Y                    — Toggle trails");
        UZ_LOG(App, Info, "  D                    — Toggle decals");
        UZ_LOG(App, Info, "  X                    — Toggle distortion");
        UZ_LOG(App, Info, "  J                    — Toggle lit particles (NdotL + ambient)");
        UZ_LOG(App, Info, "  Ctrl+F               — Toggle VFX frustum culling (sphere-frustum test)");
        UZ_LOG(App, Info, "  --- VFX Controls (number keys) ---");
        UZ_LOG(App, Info, "  1/2                  — Spawn rate ↑/↓");
        UZ_LOG(App, Info, "  3/4                  — Particle size ↑/↓");
        UZ_LOG(App, Info, "  5/6                  — Gravity Z ↑/↓");
        UZ_LOG(App, Info, "  7/8                  — Velocity ↑/↓");
        UZ_LOG(App, Info, "  9/0                  — Spread angle ↑/↓");
        UZ_LOG(App, Info, "  Shift+1/2            — Lifetime ↑/↓");
        UZ_LOG(App, Info, "  Shift+3/4            — Emissive ↑/↓");
        UZ_LOG(App, Info, "  Shift+5/6            — Damping ↑/↓");
        UZ_LOG(App, Info, "  Shift+7/8            — Spawn radius ↑/↓");
        UZ_LOG(App, Info, "  F1-F8                — Debug view modes");
        UZ_LOG(App, Info, "  F5                   — Screenshot");
        UZ_LOG(App, Info, "  F10                  — VFX parameter panel (emitter config editor)");
        UZ_LOG(App, Info, "  Q                    — VFX module stack panel (module visibility + toggle)");
        UZ_LOG(App, Info, "  Alt+Enter            — Toggle fullscreen");
        UZ_LOG(App, Info, "  H                    — Show this help");
    }

    // Fullscreen toggle (Alt+Enter)
    if (input->IsKeyJustPressed(Key::Enter) &&
        (input->IsKeyDown(Key::LeftAlt) || input->IsKeyDown(Key::RightAlt))) {
        auto* window = GetWindow();
        if (window) {
            if (window->IsFullscreen()) {
                window->SetWindowed();
                UZ_LOG(App, Info, "Windowed mode");
            } else {
                window->SetFullscreen();
                UZ_LOG(App, Info, "Borderless fullscreen");
            }
        }
    }

    // RenderDoc frame capture (F11)
    if (input->IsKeyJustPressed(Key::F11)) {
        if (RenderDocIsLoaded()) {
            RenderDocTriggerCapture();
            UZ_LOG(App, Info, "RenderDoc: capture triggered");
        } else {
            UZ_LOG(App, Warning, "RenderDoc: not loaded (install RenderDoc and relaunch)");
        }
    }

    // Save current preset to JSON (Shift+S)
    // Generates a timestamped filename and writes to content/vfx/presets/.
    // After saving, rescans the directory so the new file appears in Tab cycling.
    const bool shiftHeld = input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift);
    if (shiftHeld && input->IsKeyJustPressed(Key::S)) {
        std::time_t now = std::time(nullptr);
        struct tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &now);
#else
        localtime_r(&now, &tm_buf);
#endif
        char nameBuf[64];
        std::strftime(nameBuf, sizeof(nameBuf), "preset_%Y%m%d_%H%M%S", &tm_buf);

        const std::string filePath = std::string("content/vfx/presets/") + nameBuf + ".json";
        if (VFXPresetSerializer::Save(ActiveEmitter().config, ActiveEmitter().spawnRate, nameBuf, filePath)) {
            UZ_LOG(App, Info, "VFX: Saved preset '{}'", filePath.c_str());
            // Rescan so the new file is immediately available in Tab cycling.
            RefreshPresetList("content/vfx/presets/");
            m_saveHintTimer = 2.0f;
        } else {
            UZ_LOG(App, Warning, "VFX: Failed to save preset to '{}'", filePath.c_str());
        }
    }

    // Preset directory refresh (F6)
    // Rescans content/vfx/presets/ and updates m_presetFiles/m_presetNames.
    if (input->IsKeyJustPressed(Key::F6)) {
        RefreshPresetList("content/vfx/presets/");
        UZ_LOG(App, Info, "VFX: Presets refreshed — {} found",
               static_cast<int>(m_presetFiles.size()));
    }

    // Delete confirmation for the currently-selected file preset (Delete key).
    // First press shows a log prompt; second press within 2 seconds deletes it.
    if (input->IsKeyJustPressed(Key::Delete) && !m_presetFiles.empty() &&
        m_vfxPresetIndex >= 0 && m_vfxPresetIndex < static_cast<int>(m_presetFiles.size())) {
        if (m_deleteConfirmPending && m_deleteConfirmIdx == m_vfxPresetIndex &&
            m_deleteConfirmTimer > 0.0f) {
            // Second press — perform deletion.
            const std::string path = m_presetFiles[m_vfxPresetIndex];
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (!ec) {
                UZ_LOG(App, Info, "VFX: Deleted preset '{}'", path.c_str());
            } else {
                UZ_LOG(App, Warning, "VFX: Failed to delete '{}': {}",
                       path.c_str(), ec.message().c_str());
            }
            RefreshPresetList("content/vfx/presets/");
            // Clamp index after deletion.
            if (!m_presetFiles.empty()) {
                m_vfxPresetIndex = std::min(m_vfxPresetIndex,
                                            static_cast<int>(m_presetFiles.size()) - 1);
            } else {
                m_vfxPresetIndex = 0;
            }
            m_deleteConfirmPending = false;
            m_deleteConfirmIdx     = -1;
        } else {
            // First press — start confirmation window.
            m_deleteConfirmPending = true;
            m_deleteConfirmIdx     = m_vfxPresetIndex;
            m_deleteConfirmTimer   = 2.0f;
            UZ_LOG(App, Info, "VFX: Delete '{}'? Press Del again within 2 seconds to confirm",
                   m_presetNames[m_vfxPresetIndex].c_str());
        }
    }

    // Tick save hint and delete confirm countdowns (using dt from HandleInput's caller).
    // dt is available because HandleInput receives it as a parameter.
    if (m_saveHintTimer > 0.0f) {
        m_saveHintTimer = std::max(0.0f, m_saveHintTimer - dt);
    }
    if (m_deleteConfirmTimer > 0.0f) {
        m_deleteConfirmTimer = std::max(0.0f, m_deleteConfirmTimer - dt);
        if (m_deleteConfirmTimer <= 0.0f) {
            m_deleteConfirmPending = false;
            m_deleteConfirmIdx     = -1;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

} // namespace uz
