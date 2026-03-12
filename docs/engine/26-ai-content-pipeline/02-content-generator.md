# Content Generator — LLM Integration & Streaming

> **Status**: Planned (Phase 2–3)
> **Source**: `engine/ai/content_generator.h`, `engine/ai/content_generator.cpp`
> **References**: OpenAI API, Anthropic Claude API, local LLM frameworks (Ollama, vLLM)

## Purpose

The ContentGenerator is a high-level interface to Large Language Models (LLMs) for procedural generation of game content. It abstracts away provider-specific API differences, handles streaming responses incrementally, manages rate limiting and retries, and integrates with the asset hot-reload system to immediately test generated content in-game.

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Game Code / Editor UI                          │
│  Prompt: "Create a spooky castle level..."      │
└────────────────┬────────────────────────────────┘
                 │ GenerateLevel(prompt, context)
                 ▼
┌─────────────────────────────────────────────────┐
│  ContentGenerator                               │
│  ├─ 1. Enhance prompt with game context        │
│  ├─ 2. Select provider (OpenAI / Anthropic)    │
│  ├─ 3. Instantiate PromptTemplate               │
│  ├─ 4. Call LLM with streaming enabled         │
│  ├─ 5. Buffer & validate JSON chunks           │
│  ├─ 6. Rate limit backoff (exponential)        │
│  └─ 7. Invoke OnProgress callback               │
└────────────────┬────────────────────────────────┘
                 │ HTTP/WebSocket to LLM API
                 ▼
┌─────────────────────────────────────────────────┐
│  LLM Provider (Cloud or Local)                  │
│  ├─ OpenAI API: gpt-4-turbo, gpt-4o             │
│  ├─ Anthropic: claude-3-sonnet, claude-3-opus  │
│  ├─ Local (Ollama): Llama 2, Mistral, etc.     │
│  └─ Generates JSON stream in real-time         │
└────────────────┬────────────────────────────────┘
                 │ Streamed JSON chunks
                 ▼
┌─────────────────────────────────────────────────┐
│  Response Parser & Validator                    │
│  ├─ Buffer incomplete JSON (handle mid-key)     │
│  ├─ Validate schema (ContentValidator checks)  │
│  ├─ Skip/fix invalid entries                    │
│  └─ Emit OnChunk callbacks for UI progress      │
└────────────────┬────────────────────────────────┘
                 │ Valid JSON objects
                 ▼
┌─────────────────────────────────────────────────┐
│  Asset Hot-Reload                              │
│  ├─ Write .uzlevel / .uzdialogue to disk       │
│  ├─ FileWatcher detects change                 │
│  ├─ ContentManager reloads & validates         │
│  └─ Scene updates in real-time                 │
└─────────────────────────────────────────────────┘
```

## Provider Abstraction

### LLM Provider Interface

```cpp
/// Abstraction over different LLM APIs (OpenAI, Anthropic, local).
class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;

    /// Call LLM API with streaming support.
    /// Callback invoked once per streamed chunk (e.g., one token or one JSON object).
    /// Returns async handle to track completion and errors.
    virtual AsyncHandle<std::string> CallStream(
        StringView model,                    // "gpt-4-turbo", "claude-3-sonnet", etc.
        const std::vector<Message>& messages, // Conversation history
        const GenerationConfig& config,       // Temp, max_tokens, top_p, etc.
        std::function<void(StringView chunk)> on_chunk,  // Callback per chunk
        std::function<void(const Error&)> on_error = nullptr
    ) = 0;

    /// Get available models for this provider.
    virtual std::vector<std::string> ListModels() const = 0;

    /// Check provider availability (API key valid, endpoint reachable).
    virtual bool IsAvailable() const = 0;
};

/// Concrete provider: OpenAI (gpt-4-turbo, gpt-4o, gpt-3.5-turbo).
class OpenAIProvider : public ILLMProvider {
public:
    OpenAIProvider(StringView api_key) : api_key_(api_key) {}

    AsyncHandle<std::string> CallStream(
        StringView model,
        const std::vector<Message>& messages,
        const GenerationConfig& config,
        std::function<void(StringView chunk)> on_chunk,
        std::function<void(const Error&)> on_error = nullptr
    ) override;

    std::vector<std::string> ListModels() const override {
        return { "gpt-4-turbo", "gpt-4o", "gpt-3.5-turbo" };
    }

private:
    std::string api_key_;
};

/// Concrete provider: Anthropic (claude-3-opus, claude-3-sonnet).
class AnthropicProvider : public ILLMProvider {
public:
    AnthropicProvider(StringView api_key) : api_key_(api_key) {}

    AsyncHandle<std::string> CallStream(
        StringView model,
        const std::vector<Message>& messages,
        const GenerationConfig& config,
        std::function<void(StringView chunk)> on_chunk,
        std::function<void(const Error&)> on_error = nullptr
    ) override;

    std::vector<std::string> ListModels() const override {
        return { "claude-3-opus-20240229", "claude-3-sonnet-20240229" };
    }

private:
    std::string api_key_;
};

/// Concrete provider: Local LLM via Ollama REST API.
class OllamaProvider : public ILLMProvider {
public:
    OllamaProvider(StringView endpoint = "http://localhost:11434")
        : endpoint_(endpoint) {}

    AsyncHandle<std::string> CallStream(
        StringView model,
        const std::vector<Message>& messages,
        const GenerationConfig& config,
        std::function<void(StringView chunk)> on_chunk,
        std::function<void(const Error&)> on_error = nullptr
    ) override;

    std::vector<std::string> ListModels() const override;  // Queries Ollama for available models

private:
    std::string endpoint_;
};
```

## Message Format

```cpp
/// A single message in an LLM conversation.
struct Message {
    enum class Role { System, User, Assistant };

    Role role;                      // Role of sender
    std::string content;            // Text content
    StringView metadata = "";       // Optional: JSON context, tool calls, etc.
};

/// Configuration for generation (temperature, max tokens, etc.).
struct GenerationConfig {
    float temperature = 0.7f;       // 0.0 (deterministic) to 2.0 (creative)
    uint32_t max_tokens = 4096;     // Max output length
    float top_p = 0.95f;            // Nucleus sampling
    float top_k = 40;               // Token filtering (if supported)
    float frequency_penalty = 0.0f; // Penalize repeated tokens
    float presence_penalty = 0.0f;  // Penalize new topics
    bool stream = true;             // Enable streaming mode
};
```

## Prompt Templates

### Template System

Prompt templates are **not hardcoded strings**. They are data files (`.uzprompt`) that developers and LLMs can edit, parameterize, and version-control.

```cpp
/// Templated prompt with placeholders and context substitution.
class PromptTemplate {
public:
    /// Load template from disk (.uzprompt file).
    static PromptTemplate Load(StringView template_path);

    /// Render template with context variables.
    /// Example: "Create a {{level_type}} with {{num_rooms}} rooms" → "Create a castle with 5 rooms"
    std::string Render(const std::map<std::string, std::string>& context) const;

    /// Get list of required placeholders (e.g., ["level_type", "num_rooms"]).
    std::vector<std::string> GetPlaceholders() const;

    /// Get template metadata (version, author, description).
    const Metadata& GetMetadata() const { return metadata_; }

private:
    std::string template_text_;
    Metadata metadata_;
    std::vector<std::string> placeholders_;
};
```

### Example Template File

```json
{
    "metadata": {
        "name": "Forest Level Generator",
        "version": "1.0",
        "author": "UzEngine Team",
        "description": "Generate a forest level with encounters and treasures"
    },

    "system_prompt": "You are an expert game level designer. Create detailed, balanced levels that are fun and beatable.",

    "user_prompt": "Design a {{difficulty}} forest level with {{num_encounters}} combat encounters and {{num_treasures}} treasure locations.\n\nContext:\n- Player level: {{player_level}}\n- Current biome: {{current_biome}}\n- Engine: UzEngine\n\nOutput the level as a valid .uzlevel JSON file. Ensure:\n- All entity references are valid\n- Encounter difficulty matches player level\n- Treasure placement is balanced\n- Environment description is vivid",

    "examples": [
        {
            "input": { "difficulty": "Hard", "num_encounters": 3, "num_treasures": 2, "player_level": 10, "current_biome": "Ancient Forest" },
            "output": "{ ... full .uzlevel JSON ... }"
        }
    ]
}
```

## Prompt Enhancement

Game context is automatically added to every prompt to ensure LLM generates appropriate content:

```cpp
/// Enhance user prompt with game context (entities, stats, progression, etc.).
class PromptEnhancer {
public:
    /// Gather game state (current entities, completed quests, player stats).
    std::map<std::string, std::string> GatherContext(const GameState& state) {
        return {
            { "player_level", std::to_string(state.player_level) },
            { "current_level", state.current_level_name },
            { "completed_quests", fmt::format("{}", state.completed_quest_count) },
            { "inventory_items", SerializeInventory(state.inventory) },
            { "current_biome", state.current_biome },
        };
    }

    /// Inject world rules, balance constraints, and style guide into prompt.
    std::string EnhanceWithRules(StringView user_prompt, const GameRules& rules) {
        return fmt::format(
            "{}\n\n# Style Guide & Constraints\n{}\n# Balance Rules\n{}",
            user_prompt,
            rules.style_guide,
            rules.balance_rules
        );
    }
};
```

## Response Parsing & Buffering

Streaming JSON responses must be buffered and validated incrementally, handling partial chunks:

```cpp
/// Parse streaming JSON responses (handle mid-key, mid-value cases).
class StreamingJSONParser {
public:
    /// Feed chunk of text from LLM stream.
    /// Returns list of complete JSON objects found in this batch.
    std::vector<nlohmann::json> FeedChunk(StringView chunk) {
        buffer_ += chunk;
        std::vector<nlohmann::json> complete_objects;

        // Try to parse complete JSON objects from buffer
        while (true) {
            // Skip whitespace
            size_t start = buffer_.find_first_not_of(" \n\r\t");
            if (start == std::string::npos) break;

            // Find balanced closing brace
            size_t close = FindBalancedBrace(buffer_, start);
            if (close == std::string::npos) break;  // Incomplete object

            // Extract and parse complete object
            std::string obj_str = buffer_.substr(start, close - start + 1);
            try {
                auto json_obj = nlohmann::json::parse(obj_str);
                complete_objects.push_back(json_obj);
            } catch (const std::exception& e) {
                UZ_LOG(AI, Warning, "Invalid JSON in stream: {}", e.what());
            }

            // Remove parsed object from buffer
            buffer_.erase(0, close + 1);
        }

        return complete_objects;
    }

    /// Get unparsed buffer remainder (for logging incomplete response).
    StringView GetRemainder() const { return buffer_; }

private:
    std::string buffer_;

    size_t FindBalancedBrace(const std::string& str, size_t start) {
        if (str[start] != '{') return std::string::npos;
        int depth = 0;
        for (size_t i = start; i < str.length(); ++i) {
            if (str[i] == '{') depth++;
            else if (str[i] == '}') {
                depth--;
                if (depth == 0) return i;
            }
        }
        return std::string::npos;
    }
};
```

## Rate Limiting & Backoff

Intelligent retry logic with exponential backoff to handle API rate limits and transient failures:

```cpp
/// Rate limiter with token bucket + exponential backoff for LLM API calls.
class APIRateLimiter {
public:
    APIRateLimiter(uint32_t requests_per_minute = 60, uint32_t tokens_per_minute = 90000)
        : rpm_(requests_per_minute), tpm_(tokens_per_minute) {}

    /// Check if request is allowed now; if not, return time to wait (milliseconds).
    uint32_t GetWaitTimeMs() {
        auto now = std::chrono::steady_clock::now();

        // Token bucket refill
        auto elapsed = now - last_refill_;
        if (elapsed > std::chrono::minutes(1)) {
            available_tokens_ = tpm_;
            last_refill_ = now;
        }

        // Estimate tokens for request (rough: tokens ≈ words * 1.3)
        if (available_tokens_ < 1000) {  // Too few tokens
            auto until_refill = std::chrono::minutes(1) - elapsed;
            return std::chrono::duration_cast<std::chrono::milliseconds>(until_refill).count();
        }

        return 0;  // OK to proceed
    }

    /// Record successful request (deduct tokens).
    void RecordSuccess(uint32_t tokens_used) {
        available_tokens_ -= tokens_used;
    }

    /// Record API error; return backoff time (milliseconds).
    uint32_t RecordError() {
        retry_count_++;
        auto backoff_ms = static_cast<uint32_t>(100 * std::pow(2, retry_count_));
        backoff_ms = std::min(backoff_ms, 300000u);  // Cap at 5 minutes
        return backoff_ms;
    }

    /// Reset after successful request.
    void ResetRetry() {
        retry_count_ = 0;
    }

private:
    uint32_t rpm_, tpm_;
    uint32_t available_tokens_ = 90000;
    std::chrono::steady_clock::time_point last_refill_ = std::chrono::steady_clock::now();
    uint32_t retry_count_ = 0;
};
```

## Main ContentGenerator API

```cpp
/// High-level content generation interface.
class ContentGenerator {
public:
    /// Configure active LLM provider.
    static void SetProvider(std::unique_ptr<ILLMProvider> provider) {
        g_provider = std::move(provider);
    }

    /// Generate a level from text prompt.
    /// Returns async handle; callback invoked as chunks arrive.
    AsyncHandle<LevelAsset> GenerateLevel(
        StringView prompt,
        const GameContext& context,
        StringView output_path = "generated_levels/",
        ProgressCallback* on_progress = nullptr
    );

    /// Generate a dialogue tree for an NPC.
    AsyncHandle<DialogueTree> GenerateDialogue(
        StringView character_name,
        StringView character_description,
        const QuestContext& quest_context,
        StringView output_path = "generated_dialogue/",
        ProgressCallback* on_progress = nullptr
    );

    /// Generate a quest chain from story premise.
    AsyncHandle<std::vector<QuestAsset>> GenerateQuests(
        StringView story_premise,
        uint32_t num_quests = 5,
        StringView output_path = "generated_quests/",
        ProgressCallback* on_progress = nullptr
    );

    /// Generate character definitions (stats, dialogue, inventory).
    AsyncHandle<CharacterAsset> GenerateCharacter(
        StringView character_archetype,    // "warrior", "mage", "rogue", etc.
        uint32_t level = 1,
        StringView output_path = "generated_characters/",
        ProgressCallback* on_progress = nullptr
    );

    /// Cancel in-flight generation.
    bool CancelGeneration(const AsyncHandle<>& handle);

    /// Event: generation complete (before validation).
    MulticastDelegate<void(StringView asset_type, StringView output_path)> OnGenerationComplete;

    /// Event: generation failed (error message provided).
    MulticastDelegate<void(StringView asset_type, const Error&)> OnGenerationFailed;

private:
    static std::unique_ptr<ILLMProvider> g_provider;
    std::map<uint64_t, GenerationTask> in_flight_;
};

/// Callback invoked during streaming generation.
using ProgressCallback = std::function<void(float percent_done, StringView status_message)>;

/// Holds state of an in-flight generation request.
struct GenerationTask {
    std::string asset_type;         // "level", "dialogue", "quest", etc.
    std::string output_path;        // Where to write asset
    std::string accumulated_json;   // Buffered JSON response
    ProgressCallback* progress_cb;
    uint32_t chunk_count = 0;
};
```

## Usage Example

```cpp
// Initialize provider
uz::ContentGenerator::SetProvider(
    std::make_unique<uz::OpenAIProvider>("sk-...")
);

// Generate a level
uz::ContentGenerator gen;
auto handle = gen.GenerateLevel(
    "Create a spooky castle dungeon with 3 combat encounters and 2 treasure chests. Make it suitable for level 8 players.",
    uz::GameContext { .player_level = 8, .current_biome = "Ruins" },
    "content/generated_levels/",
    new uz::ProgressCallback([](float pct, uz::StringView status) {
        UZ_LOG(Game, Info, "Generation {}%: {}", static_cast<int>(pct * 100), status);
    })
);

// Wait for completion (or handle async)
auto [status, level_asset] = handle.Wait();
if (status == uz::AsyncStatus::Success) {
    UZ_LOG(Game, Info, "Level generated: {}", level_asset->name);
    // Hot-reload happens automatically via FileWatcher
}
```

## Error Handling & Retry

```cpp
/// Result of generation attempt.
struct GenerationResult {
    enum class Status { Success, Validation, APIError, Timeout, Cancelled };

    Status status;
    std::string error_message;
    std::string suggestion;  // Hint for LLM retry
    nlohmann::json raw_response;  // For debugging

    operator bool() const { return status == Status::Success; }
};
```

## Comparison

| Feature | ChatGPT API | Claude API | Local (Ollama) |
|---------|-------------|-----------|----------------|
| **Setup** | API key | API key | Self-hosted |
| **Cost** | $0.01–$0.03 / 1K tokens | $0.015–$0.075 / 1K tokens | Free |
| **Speed** | 100ms–500ms per request | 100ms–500ms | <50ms (local) |
| **Latency** | 20–100ms | 20–100ms | < 5ms |
| **Quality (Level Gen)** | Excellent | Excellent | Good (Mistral) |
| **Streaming** | ✓ | ✓ | ✓ |
| **Context Window** | 128K (gpt-4o) | 200K (opus) | Varies (4K–8K typical) |
| **Availability** | Cloud | Cloud | Local |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | OpenAIProvider (basic streaming), PromptTemplate loading, StreamingJSONParser, rate limiting, async generation API |
| **Phase 3** | AnthropicProvider, PromptEnhancer (game context injection), PromptTemplate editor in VSCode extension, hotlink to validation |
| **Phase 4** | OllamaProvider (local LLM), prompt versioning & history, prompt A/B testing (compare OpenAI vs Anthropic outputs) |
