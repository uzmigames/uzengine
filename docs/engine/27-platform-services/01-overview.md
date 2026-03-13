# Platform Services — Cross-Platform Integrations & Multiplayer

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/platform/`, `engine/platform/platform_services.h`, `engine/online/`
> **Backends**: Steam SDK, Epic Online Services (EOS), DLSS/FSR/XeSS SDKs, Console GDKs

## Purpose

Platform Services provide a unified abstraction layer over **achievements, leaderboards, matchmaking, cloud saves, social features, and upscaling**. Multiple backends are supported (Steam, EOS, Xbox, PlayStation), and the game never hardcodes dependencies on a specific platform. This enables **single codebase shipping on PC, console, mobile**.

## Architecture

```
Game Code
    │
    ├── uz::Achievements::Unlock("defeated_boss")
    ├── uz::Leaderboards::Submit("score", 9500)
    ├── uz::CloudSaves::Save("profile.json", data)
    ├── uz::Matchmaking::FindGame(player_count=4)
    └── uz::Upscaling::GetActiveUpscaler() // DLSS / FSR / XeSS / None
        │
        ▼
    Platform Services Layer (platform_services.h)
        │
        ├── 1. Query active platform (Steam / EOS / Console / Standalone)
        ├── 2. Dispatch to platform-specific backend
        ├── 3. Handle async results + error codes
        │
        ▼
    Platform Backends (pluggable vtable)
        │
        ├── Steam SDK (Steamworks)
        ├── Epic Online Services (EOS)
        ├── Xbox GDK (Xbox Live)
        ├── PlayStation SDK (PSN)
        └── Standalone (no services)
        │
        ▼
    Platform APIs
        ├── SteamAPI (achievements, leaderboards, cloud saves, overlay, workshop)
        ├── EOS SDK (cross-platform matchmaking, lobbies, progression)
        ├── Console SDKs (console-specific features)
        └── Upscaling Libraries (NVIDIA DLSS, AMD FSR, Intel XeSS)
```

## Key Components

### Achievement System

```cpp
/// Achievement metadata (static, defined at build time).
struct AchievementDef {
    StringView id;                      // "defeated_final_boss"
    StringView display_name;            // Localized display name
    StringView description;             // What player must do
    StringView icon_path;               // .uzimage for achievement icon
    bool hidden = false;                // Hidden until unlocked
    bool one_time_only = true;          // Can only unlock once
};

/// Achievement manager — unlock, query progress, listen to events.
class AchievementManager {
public:
    /// Unlock an achievement (idempotent — safe to call multiple times).
    void Unlock(StringView achievement_id);

    /// Get progress [0, 1] for progress-based achievements.
    float GetProgress(StringView achievement_id) const;

    /// List all achievement definitions.
    Span<const AchievementDef> GetDefinitions() const;

    /// Query whether an achievement is unlocked (local player).
    bool IsUnlocked(StringView achievement_id) const;

    /// Event: fired when achievement unlocked.
    MulticastDelegate<void(StringView id, const AchievementDef&)> OnUnlocked;
};
```

### Leaderboard System

```cpp
/// Per-game leaderboard (e.g., "high_score", "speedrun_best_time").
/// Supports per-level, per-difficulty, per-season scopes.
class Leaderboard {
public:
    enum class SortOrder { HighestFirst, LowestFirst };
    enum class DisplayType { Numeric, Time, Custom };

    StringView id;                      // "high_score"
    StringView display_name;            // User-facing name
    DisplayType display_type;           // Shown as score vs time
    SortOrder sort_order;               // Higher = better vs lower = better

    /// Submit score for local player.
    /// Returns async handle to track completion.
    AsyncHandle<> SubmitScore(int64_t score, StringView metadata = "");

    /// Fetch leaderboard entries (paginated).
    AsyncHandle<std::vector<LeaderboardEntry>> FetchEntries(
        uint32_t start_rank = 1,        // 1-indexed
        uint32_t count = 100
    );

    /// Get local player's rank and score.
    AsyncHandle<LeaderboardEntry> FetchLocalPlayerEntry();

    /// Get entries around local player (e.g., ±5 ranks).
    AsyncHandle<std::vector<LeaderboardEntry>> FetchAroundLocalPlayer(
        uint32_t radius = 5
    );
};

/// Single leaderboard entry.
struct LeaderboardEntry {
    uint32_t rank = 0;                  // 1-indexed
    StringView player_name;
    uint64_t player_id;
    int64_t score = 0;
    time_t timestamp = 0;
    StringView metadata;                // Game-defined (difficulty, level, etc.)
};
```

### Cloud Saves

```cpp
/// Cloud save manager — upload/download save files.
class CloudSaveManager {
public:
    enum class ConflictResolution { Local, Remote, Merge, Prompt };

    /// Save data to cloud (asynchronous).
    /// Local copy written immediately; upload happens in background.
    AsyncHandle<> SaveToCloud(
        StringView save_name,           // "profile.json"
        const std::vector<uint8_t>& data,
        bool wait_for_upload = false
    );

    /// Load data from cloud (async).
    /// If cloud is newer: return remote copy.
    /// If local is newer: return local copy (and optionally sync).
    AsyncHandle<std::vector<uint8_t>> LoadFromCloud(
        StringView save_name,
        ConflictResolution conflict_mode = ConflictResolution::Remote
    );

    /// Delete cloud save (cannot be undone).
    AsyncHandle<> DeleteCloudSave(StringView save_name);

    /// Get metadata for a cloud save (timestamp, size, hash).
    AsyncHandle<CloudSaveMetadata> GetMetadata(StringView save_name);

    /// Event: cloud sync conflict detected.
    MulticastDelegate<void(const CloudSaveConflict&)> OnConflict;
};

struct CloudSaveMetadata {
    StringView name;
    uint64_t size_bytes = 0;
    time_t timestamp = 0;
    uint64_t content_hash = 0;          // xxHash64 of data
    bool is_local = false;
    bool is_remote = false;
};
```

### Matchmaking & Lobbies

```cpp
/// Matchmaking request — find a game with constraints.
struct MatchmakingRequest {
    uint32_t player_count = 2;          // Desired player count
    uint32_t max_latency_ms = 150;      // Max acceptable ping
    StringView map_filter;              // "forest_map" (optional)
    StringView region_filter;           // "us-west" (optional)
    std::map<StringView, int> attributes; // Custom filters (skill_level, etc.)
};

/// Lobby — persistent room for players to gather before/after match.
class Lobby {
public:
    /// Get lobby properties (map, difficulty, max players).
    StringView GetProperty(StringView key) const;

    /// Set lobby property (host-only).
    void SetProperty(StringView key, StringView value);

    /// Invite another player to lobby.
    void Invite(uint64_t player_id);

    /// Kick a player (host-only).
    void Kick(uint64_t player_id);

    /// Get member list.
    Span<const LobbymMember> GetMembers() const;

    /// Leave lobby.
    void Leave();

    /// Event: player joined.
    MulticastDelegate<void(const LobbymMember&)> OnMemberJoined;

    /// Event: player left.
    MulticastDelegate<void(uint64_t player_id)> OnMemberLeft;
};

/// Matchmaking manager — find games, create/join lobbies.
class MatchmakingManager {
public:
    /// Start matchmaking search.
    AsyncHandle<Lobby*> FindGame(const MatchmakingRequest& request);

    /// Cancel in-flight matchmaking.
    void CancelSearch();

    /// Create a lobby (host-only).
    AsyncHandle<Lobby*> CreateLobby(
        uint32_t max_players = 4,
        StringView map_name = ""
    );

    /// Join existing lobby by ID.
    AsyncHandle<Lobby*> JoinLobby(uint64_t lobby_id);

    /// List available lobbies (quick-play browser).
    AsyncHandle<std::vector<LobbySummary>> BrowseLobbies();
};
```

### Upscaling Integration

```cpp
/// Upscaler type (GPU vendor specific).
enum class UpscalerType { None, DLSS, FSR, XeSS };

/// Upscaler settings.
struct UpscalerSettings {
    UpscalerType type = UpscalerType::None;
    float scale_ratio = 1.5f;           // Output / Input resolution
    bool motion_vectors = true;         // Enable temporal data
    bool auto_exposure = false;         // Post-process exposure
};

/// Upscaling manager — initialize, render, query info.
class UpscalingManager {
public:
    /// Detect available upscalers on this system.
    std::vector<UpscalerType> GetAvailableUpscalers() const;

    /// Initialize upscaler (must be called before rendering).
    /// Returns false if unavailable or initialization failed.
    bool Initialize(UpscalerType type, const UpscalerSettings& settings);

    /// Render upscaled frame.
    /// Input: LDR color + depth + motion vectors (optional).
    /// Output: upscaled color buffer.
    void Render(
        GPUTextureHandle input_color,
        GPUTextureHandle input_depth,
        GPUTextureHandle input_motion,   // Can be nullptr
        GPUTextureHandle output_color
    );

    /// Get current upscaler settings.
    const UpscalerSettings& GetSettings() const;

    /// Get recommended quality vs performance tradeoff.
    struct QualityTier {
        UpscalerType type;
        float scale_ratio;              // 1.3x, 1.5x, 2.0x
        StringView description;         // "Performance", "Balanced", "Quality"
    };
    std::vector<QualityTier> GetRecommendedTiers() const;
};
```

## Platform-Specific Compilation

Game code is **never hardcoded to one platform**. Instead, use compile-time flags and runtime detection:

```cpp
// ✅ CORRECT — conditional at compile time
#if defined(UZ_PLATFORM_STEAMDECK) || defined(UZ_PLATFORM_WIN32)
    // Use Steam SDK features
#endif

// ✅ CORRECT — query at runtime
if (PlatformServices::GetActivePlatform() == Platform::Steam) {
    achievements_manager->Unlock("trophy");
}

// ❌ WRONG — hardcoded platform dependency
#if defined(STEAM_ONLY)
    SteamUserStats()->SetAchievement("trophy");
#endif
```

## Platform Support Matrix

| Feature | Steam | EOS | Xbox GDK | PS SDK | Standalone |
|---------|-------|-----|----------|--------|-----------|
| **Achievements** | ✓ (30 max) | ✓ | ✓ | ✓ (Trophies) | ✗ |
| **Leaderboards** | ✓ (Steam Cloud) | ✓ | ✓ | ✓ | ✗ |
| **Cloud Saves** | ✓ (Steam Cloud) | ✓ (EOS) | ✓ | ✓ | ✓ (local only) |
| **Matchmaking** | Partial (lobbies) | ✓ | ✓ | ✓ | ✗ |
| **Overlay** | ✓ | ✓ | ✓ | ✓ | ✗ |
| **Workshop** | ✓ (mod upload) | — | — | — | ✗ |
| **DLSS** | ✓ (Win32) | ✓ | ✓ | — | ✓ (Win32) |
| **FSR** | ✓ (Win32) | ✓ | ✓ | ✓ | ✓ (all) |
| **XeSS** | ✓ (Win32) | ✓ | ✓ | — | ✓ (Intel GPU) |

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|------------|
| Achievements | Online Subsystem (per-platform) | None | PlayGameServices | **Unified abstraction (Steam/EOS/Console)** |
| Leaderboards | Online Subsystem | None | PlayGameServices | **Unified abstraction** |
| Cloud Saves | FOnlineSubsystem | None | Cloud Save | **Unified abstraction + conflict resolution** |
| Matchmaking | Online Subsystem | None | Netcode | **EOS Lobbies + custom relay** |
| Upscaling | DLSS/FSR plugins | None | URP plugin | **DLSS/FSR/XeSS native, switchable** |
| Cross-platform | Via Online Subsystem | None | PlayFab | **Yes (EOS) + Steam + console-specific** |
| Console support | Xbox GDK, PSN | None | Managed partner | **Planned (Xbox GDK, PS SDK, Nintendo SDK)** |

## Upscaling Availability by Platform

| GPU Vendor | DLSS | FSR | XeSS |
|------------|------|-----|------|
| **NVIDIA (GeForce 3000+)** | ✓ 3.0+ | ✓ | ✓ |
| **AMD (RDNA 2+)** | — | ✓ 3.0 | ✓ |
| **Intel (Arc, iGPU 12th+)** | ✓ | ✓ | ✓ |
| **Mobile (ARM)** | ✓ (DLSS Super) | ✓ (FSR Mobile) | — |
| **Fallback (no vendor)** | — | ✓ (universal) | — |

## File Formats

| Format | Extension | Content |
|--------|-----------|---------|
| Achievement Definitions | `.uzachievements` | JSON: list of achievements, icons, descriptions |
| Leaderboard Config | `.uzleaderboard` | JSON: display type, sort order, scope |
| Cloud Save Metadata | N/A (auto) | JSON: timestamps, hashes, conflict info |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Achievements, Leaderboards, Cloud Saves (Steam + standalone), Upscaling (DLSS/FSR/XeSS on Win32) |
| **Phase 4** | EOS integration (cross-platform matchmaking), Lobbies, Xbox GDK support, PlayStation SDK support, Workshop (Steam content creator tools), Cloud progression sync (MMO) |
