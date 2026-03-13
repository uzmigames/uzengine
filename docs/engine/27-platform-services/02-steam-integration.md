# Steam Integration — Achievements, Leaderboards, Workshop, and Overlay

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/platform/steam/`, Steamworks SDK
> **References**: Steamworks documentation, ISteamUserStats, ISteamRemoteStorage, ISteamUGC

## Purpose

Steam is the largest PC gaming platform. The Steam integration layer abstracts Steamworks SDK complexity and provides UzEngine-native APIs for achievements, leaderboards, cloud saves, workshop content, overlay notifications, and social features. Games that target Steam can ship with a single codebase while gaining platform-specific features.

## Architecture

```
Game Code (C++)
    │ uz::Achievements::Unlock("boss_defeated")
    │ uz::Leaderboards::Submit("high_score", 9500)
    │ uz::CloudSaves::SaveToCloud("profile.json", data)
    ▼
Platform Services Layer (platform_services.h)
    │
    ├─ Query active platform (Steam / EOS / Standalone)
    │
    └─ Dispatch to Steam backend
           │
           ▼
    Steam Backend (steam_backend.h)
        │
        ├─ Achievements subsystem (SteamUserStats)
        ├─ Leaderboards subsystem (SteamUserStats + Leaderboard API)
        ├─ Cloud saves (SteamRemoteStorage)
        ├─ Workshop content (SteamUGC)
        ├─ Social features (ISteamFriends)
        └─ Overlay integration
           │
           ▼
    Steamworks SDK (third-party library)
        │
        └─ Steam servers (API calls, cloud sync, P2P)
```

## Steamworks Configuration

### App ID & Credentials

Every Steam game has a unique **App ID** registered with Valve. This must be configured at build time and runtime:

```cpp
// Compile-time constant
#define STEAM_APPID 480

/// Initialize Steamworks with App ID.
bool SteamBackend::Initialize(uint32_t app_id) {
    if (!SteamAPI_Init()) {
        UZ_LOG(Platform, Error, "Failed to initialize Steamworks SDK");
        return false;
    }

    app_id_ = app_id;

    // Verify app owns appropriate Steamworks features
    auto stats = SteamUserStats();
    if (!stats) {
        UZ_LOG(Platform, Error, "SteamUserStats not available for this app");
        return false;
    }

    UZ_LOG(Platform, Info, "Steamworks initialized for app {}", app_id);
    return true;
}

/// Poll Steamworks callbacks (call once per frame).
void SteamBackend::Update() {
    SteamAPI_RunCallbacks();
}
```

### stats.schema.xml

Achievements and leaderboards are defined in a Steamworks configuration file (uploaded via Steamworks partner portal):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ugc language="english">

    <!-- Achievements (max 100 per app) -->
    <achievement name="ACH_DEFEAT_BOSS_1">
        <display>
            <name>Dragon Slayer</name>
            <desc>Defeat the first dragon boss</desc>
            <icon>0</icon>  <!-- Image ID in Steamworks portal -->
        </display>
        <stat name="boss_defeats_1" />
        <goalvalue>1</goalvalue>
    </achievement>

    <achievement name="ACH_SPEEDRUN">
        <display>
            <name>Speedrunner</name>
            <desc>Complete game in under 1 hour</desc>
            <icon>1</icon>
        </display>
        <stat name="playtime_seconds" />
        <goalvalue>3600</goalvalue>  <!-- 1 hour in seconds -->
    </achievement>

    <!-- Leaderboards -->
    <leaderboard name="HighScores">
        <display>
            <name>High Scores</name>
            <sortmethod>Descending</sortmethod>  <!-- Higher = better -->
            <displaytype>Numeric</displaytype>
        </display>
        <maxscore>1000000</maxscore>
        <scoretypes>
            <scoretype>Score</scoretype>
        </scoretype>
        <onlyfriendsleaderboard>false</onlyfriendsleaderboard>
        <onlyallowupdates>false</onlyallowupdates>
    </leaderboard>

    <leaderboard name="SpeedrunTimes">
        <display>
            <name>Best Times</name>
            <sortmethod>Ascending</sortmethod>  <!-- Lower = better -->
            <displaytype>Time</displaytype>
        </display>
        <scoretypes>
            <scoretype>Time</scoretype>
        </scoretype>
    </leaderboard>

</ugc>
```

## Achievement System

### Implementation

```cpp
/// Steam achievement manager — unlock, query progress, listen to events.
class SteamAchievementManager {
public:
    /// Unlock an achievement by name (idempotent).
    void Unlock(StringView achievement_id) override;

    /// Set progress for multi-stage achievements (e.g., "Kill 100 enemies").
    /// Progress: [0, 1]
    void SetProgress(StringView achievement_id, uint32_t current, uint32_t goal) override;

    /// Query whether an achievement is unlocked (local player).
    bool IsUnlocked(StringView achievement_id) const override;

    /// Get progress [0, 100] for cumulative achievements.
    float GetProgress(StringView achievement_id) const override;

    /// Fetch achievement definitions (icons, names, descriptions).
    Span<const AchievementDef> GetDefinitions() const override;

    /// Event fired when player unlocks achievement.
    /// Note: May fire for already-unlocked achievements on subsequent unlocks; must deduplicate.
    MulticastDelegate<void(StringView id)> OnUnlocked;

private:
    /// Callback from SteamAPI when user stats are received.
    STEAM_CALLBACK(SteamAchievementManager, OnUserStatsReceived, UserStatsReceived_t);
    STEAM_CALLBACK(SteamAchievementManager, OnAchievementUnlocked, UserAchievementStored_t);

    ISteamUserStats* steam_stats_ = nullptr;
    std::map<std::string, AchievementDef> achievement_defs_;
    std::set<std::string> unlocked_;  // Local cache of unlocked achievements
};

/// Implementation of unlock:
void SteamAchievementManager::Unlock(StringView achievement_id) {
    if (!steam_stats_) return;

    UZ_LOG(Platform, Info, "Unlocking achievement: {}", achievement_id);

    // Set achievement on Steam backend
    steam_stats_->SetAchievement(achievement_id.data());

    // Flush to servers (async)
    steam_stats_->StoreStats();
}

STEAM_CALLBACK(SteamAchievementManager, OnAchievementUnlocked, UserAchievementStored_t,
    result) {
    if (result.m_bAchieved && !unlocked_.count(result.m_rgchAchievementName)) {
        unlocked_.insert(result.m_rgchAchievementName);
        OnUnlocked.Broadcast(result.m_rgchAchievementName);
    }
}
```

### Usage

```lua
-- Lua binding
ctx.steam:unlock("ACH_DEFEAT_BOSS_1")
ctx.steam:set_progress("ACH_COLLECT_COINS", 50, 100)  -- 50/100 coins

-- Listen to unlock event
ctx.events:subscribe("achievement_unlocked", function(achievement_id)
    print("You unlocked: " .. achievement_id)
    ctx.ui:show_toast("Achievement unlocked!")
end)
```

## Leaderboard System

### Implementation

```cpp
/// Steam leaderboard manager — submit scores, fetch entries, display rankings.
class SteamLeaderboard {
public:
    /// Submit score for local player (async).
    AsyncHandle<LeaderboardResult> SubmitScore(
        int64_t score,
        StringView metadata = "",
        bool upload_only = false  // If true, don't re-download
    ) override;

    /// Fetch top entries (paginated).
    AsyncHandle<std::vector<LeaderboardEntry>> FetchEntries(
        uint32_t start_rank = 1,
        uint32_t count = 100
    ) override;

    /// Get local player's rank and score.
    AsyncHandle<LeaderboardEntry> FetchLocalPlayerEntry() override;

    /// Get entries around local player (±N ranks).
    AsyncHandle<std::vector<LeaderboardEntry>> FetchAroundLocalPlayer(
        uint32_t radius = 5
    ) override;

private:
    STEAM_CALLBACK(SteamLeaderboard, OnLeaderboardScoresReceived,
                   LeaderboardScoresDownloaded_t);

    SteamLeaderboardHandle_t leaderboard_handle_ = 0;
    ISteamUserStats* steam_stats_ = nullptr;
};

/// Steam-specific result details.
struct LeaderboardResult {
    int64_t score = 0;
    uint32_t rank = 0;
    bool is_new_personal_best = false;
    uint32_t previous_rank = 0;  // 0 if first submit
};
```

### Leaderboard Design Patterns

```cpp
// Single leaderboard (global high scores)
struct SingleLeaderboard {
    std::string name = "HighScores";
    SteamLeaderboardHandle_t handle = 0;
};

// Multiple leaderboards per difficulty
struct DifficultyLeaderboards {
    SteamLeaderboardHandle_t easy = 0;
    SteamLeaderboardHandle_t medium = 0;
    SteamLeaderboardHandle_t hard = 0;
    SteamLeaderboardHandle_t nightmare = 0;
};

// Per-level leaderboards
struct LevelLeaderboards {
    std::map<std::string, SteamLeaderboardHandle_t> by_level;  // "level_01" → handle

    SteamLeaderboardHandle_t GetHandle(StringView level_id) {
        auto it = by_level.find(level_id.data());
        return it != by_level.end() ? it->second : 0;
    }
};

// Season leaderboards (reset monthly/quarterly)
struct SeasonLeaderboard {
    uint32_t season_id = 0;
    SteamLeaderboardHandle_t handle = 0;
    time_t season_end_time = 0;
};
```

### Usage

```lua
-- Submit score
local score = 9500
ctx.steam:leaderboard("HighScores"):submit_score(score, {
    difficulty = "hard",
    char_level = 15
})

-- Fetch top 10
local top_10 = ctx.steam:leaderboard("HighScores"):fetch_entries(1, 10)
for i, entry in ipairs(top_10) do
    print(i .. ". " .. entry.player_name .. ": " .. entry.score)
end

-- Get player's rank
local my_entry = ctx.steam:leaderboard("HighScores"):fetch_local_player_entry()
print("Your rank: " .. my_entry.rank .. " / Score: " .. my_entry.score)
```

## Cloud Saves

### Implementation

```cpp
/// Steam cloud save manager (uses Steam Cloud, not ISteamRemoteStorage).
class SteamCloudSaveManager : public CloudSaveManager {
public:
    /// Save data to cloud (async).
    /// Writes locally immediately; uploads in background.
    AsyncHandle<> SaveToCloud(
        StringView save_name,
        const std::vector<uint8_t>& data,
        bool wait_for_upload = false
    ) override;

    /// Load from cloud (async).
    /// Returns remote copy if newer; local if newer and remote is stale.
    AsyncHandle<std::vector<uint8_t>> LoadFromCloud(
        StringView save_name,
        ConflictResolution conflict_mode = ConflictResolution::Remote
    ) override;

    /// Delete cloud save (cannot be undone).
    AsyncHandle<> DeleteCloudSave(StringView save_name) override;

    /// Get metadata (timestamp, size, hash).
    AsyncHandle<CloudSaveMetadata> GetMetadata(StringView save_name) override;

    /// Event: cloud sync conflict detected.
    MulticastDelegate<void(const CloudSaveConflict&)> OnConflict;

private:
    STEAM_CALLBACK(SteamCloudSaveManager, OnRemoteStorageFileShareResult,
                   RemoteStorageFileShareResult_t);

    ISteamRemoteStorage* steam_remote_ = nullptr;
    std::map<std::string, CloudSaveMetadata> metadata_cache_;
};

struct CloudSaveConflict {
    std::string name;
    std::vector<uint8_t> local_data;
    std::vector<uint8_t> remote_data;
    time_t local_timestamp = 0;
    time_t remote_timestamp = 0;
};

/// Implementation (simplified):
AsyncHandle<> SteamCloudSaveManager::SaveToCloud(
    StringView save_name,
    const std::vector<uint8_t>& data,
    bool wait_for_upload
) {
    auto path = fmt::format("saves/{}", save_name);

    // Write locally immediately
    bool local_ok = steam_remote_->FileWrite(
        path.c_str(),
        data.data(),
        data.size()
    );

    if (!local_ok) {
        UZ_LOG(Platform, Error, "Failed to write cloud save: {}", save_name);
        return AsyncHandle<>::Error("FileWrite failed");
    }

    UZ_LOG(Platform, Info, "Saved to cloud: {} ({} bytes)", save_name, data.size());

    if (wait_for_upload) {
        // Block until uploaded (not recommended; use background upload instead)
        while (!steam_remote_->IsCloudEnabledForAccount()) {
            SteamAPI_RunCallbacks();
        }
    }

    return AsyncHandle<>::Ok();
}
```

### Storage Quota

Steam Cloud has per-app quota (default 256 MB, can be increased). Monitor usage:

```cpp
uint32_t cloud_quota = 0, cloud_used = 0;
steam_remote_->GetQuotaInfo(nullptr, &cloud_quota, &cloud_used, nullptr);
float utilization = 100.0f * cloud_used / cloud_quota;
UZ_LOG(Platform, Info, "Cloud storage: {:.1f}% ({} / {} bytes)", utilization, cloud_used, cloud_quota);
```

## Workshop (User-Generated Content)

### Implementation

```cpp
/// Steam Workshop manager — publish items, subscribe to content, auto-download.
class SteamWorkshopManager {
public:
    /// Publish mod/map to workshop.
    /// If item_id == 0: create new; else update existing.
    AsyncHandle<uint64_t> PublishItem(
        StringView title,
        StringView description,
        StringView content_directory,  // Directory containing .mod or .map files
        const std::vector<std::string>& tags = { "Mods" },
        uint64_t item_id = 0  // 0 = new, else update
    );

    /// Subscribe to workshop item (auto-downloads on game launch).
    AsyncHandle<> SubscribeItem(uint64_t item_id);

    /// Unsubscribe from workshop item (dequeues download if pending).
    AsyncHandle<> UnsubscribeItem(uint64_t item_id);

    /// Get list of subscribed items (local user).
    std::vector<WorkshopItem> GetSubscribedItems() const;

    /// Get folder where workshop item is installed.
    /// Returns empty if not subscribed or not yet downloaded.
    std::string GetItemPath(uint64_t item_id) const;

    /// Mount workshop items into game VFS.
    /// Calls are: Mount("content/mods/", workshop_items).
    bool MountWorkshopItems(StringView vfs_prefix, const std::vector<uint64_t>& item_ids);

    /// Event: item downloaded and ready to use.
    MulticastDelegate<void(uint64_t item_id, StringView path)> OnItemReady;

    /// Event: item unsubscribed or deleted.
    MulticastDelegate<void(uint64_t item_id)> OnItemRemoved;

private:
    STEAM_CALLBACK(SteamWorkshopManager, OnItemDownloaded,
                   DownloadItemResult_t);
    STEAM_CALLBACK(SteamWorkshopManager, OnItemInstalled,
                   ItemInstalled_t);

    ISteamUGC* steam_ugc_ = nullptr;
    std::map<uint64_t, WorkshopItem> subscribed_items_;
};

struct WorkshopItem {
    uint64_t item_id = 0;
    std::string title;
    std::string author;
    std::vector<std::string> tags;
    time_t published_date = 0;
    time_t updated_date = 0;
    std::string content_path;  // Local install directory
    bool is_subscribed = false;
    bool is_installed = false;
    float download_progress = 0.0f;  // [0, 1] while downloading
};
```

### Mod Integration Example

```lua
-- Load mods from workshop
local workshop = ctx.steam:workshop()
local mods = workshop:get_subscribed_items()

for i, mod in ipairs(mods) do
    local mod_path = workshop:get_item_path(mod.item_id)
    if mod_path then
        -- Mount mod into content directory
        ctx.asset_system:mount_directory(mod.title, mod_path)
        print("Loaded mod: " .. mod.title)
    end
end
```

## Overlay Integration

Steam Overlay provides in-game friends list, chat, web browser, and screenshots.

```cpp
/// Detect and handle Steam Overlay state changes.
class SteamOverlayManager {
public:
    /// Register callback for overlay visibility changes (pause/resume game).
    void Initialize(std::function<void(bool overlay_open)> on_state_change) {
        overlay_callback_ = on_state_change;
    }

    /// Check if overlay is currently visible.
    bool IsOverlayOpen() const {
        auto friends = SteamFriends();
        return friends ? friends->IsGameOverlayVisible() : false;
    }

    /// Open overlay to specific page (friends, achievements, leaderboard).
    void OpenOverlay(StringView dialog) {
        auto friends = SteamFriends();
        if (friends) friends->ActivateGameOverlay(dialog.data());
    }

    /// Show notification toast (appears on screen regardless of overlay state).
    void ShowNotification(StringView title, StringView message) {
        SteamAPI_ISteamUtils_ShowGamepadTextInput(
            SteamUtils(),
            GamepadTextInputModeMultiline,
            title.data(),
            0,
            message.data()
        );
    }

private:
    STEAM_CALLBACK(SteamOverlayManager, OnGamepadTextInputDismissed,
                   GamepadTextInputDismissed_t);
    std::function<void(bool)> overlay_callback_;
};

/// Implementation of overlay state detection:
STEAM_CALLBACK(SteamOverlayManager, OnGamepadTextInputDismissed,
    GamepadTextInputDismissed_t, result) {
    bool overlay_visible = IsOverlayOpen();
    if (overlay_callback_) {
        overlay_callback_(overlay_visible);
    }
}
```

### Game Pause During Overlay

```cpp
// In game loop:
bool overlay_was_open = false;

void UpdateGameLoop() {
    bool overlay_is_open = steam_overlay_->IsOverlayOpen();

    if (overlay_is_open && !overlay_was_open) {
        // Overlay just opened: pause game
        game->SetPaused(true);
        UZ_LOG(Game, Info, "Game paused (Steam Overlay open)");
    } else if (!overlay_is_open && overlay_was_open) {
        // Overlay just closed: resume game
        game->SetPaused(false);
        UZ_LOG(Game, Info, "Game resumed");
    }

    overlay_was_open = overlay_is_open;
}
```

## Comparison

| Feature | Steam | Epic (EOS) | Xbox Live | **UzEngine Abstraction** |
|---------|-------|-----------|-----------|--------------------------|
| **Achievements** | ISteamUserStats | EOS_Stats_* | XboxLiveAPI | Unified `AchievementManager` |
| **Leaderboards** | SteamLeaderboard | EOS_Leaderboards_* | Xbox Leaderboard API | Unified `Leaderboard` |
| **Cloud Saves** | ISteamRemoteStorage | EOS_PlayerDataStorage_* | Xbox Save API | Unified `CloudSaveManager` |
| **Workshop** | ISteamUGC | Impossible (no UGC) | Game Pass (limited) | SteamWorkshopManager |
| **Overlay** | ISteamFriends (GameOverlay) | Not integrated | Xbox app | SteamOverlayManager |
| **Networking** | ISteamNetworking | EOS_P2P_* | Xbox Networking | Platform-agnostic (§24) |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Achievements, Leaderboards (single), Cloud Saves (basic), stats.schema.xml generation |
| **Phase 4** | Workshop (publish/subscribe), multiple leaderboards (per-difficulty), overlay integration, seasonal leaderboards, Steam Deck validation |
