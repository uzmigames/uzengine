# Undo/Redo — Command Pattern & Transaction Grouping

> **Status**: Planned (Phase 2–3)
> **Source**: `extension/editor/undo_stack.ts`, `runtime/editor/command_system.h`
> **References**: Gang of Four Command pattern, UE5 FTransaction, Godot EditorUndoRedo

## Purpose

A robust undo/redo system allows users to confidently experiment and recover from mistakes. The command pattern decouples operations (creating entities, setting properties, moving objects) from their execution, enabling reversal and replay.

## Architecture

```
User Action (e.g., move entity, set property)
    │
    └─ Create Command object (stores before/after state)
        │
        ├─ Execute() → apply change
        │
        ├─ Undo() → revert to before state
        │
        └─ Redo() → re-apply change
            │
            └─ Push to UndoStack
                ├─ Keep on undo_stack_
                ├─ Clear redo_stack_ (new command invalidates redo history)
                └─ Broadcast "undo_stack_changed" event
```

## Command Interface

```cpp
/// Base interface for undoable operations.
class ICommand {
public:
    virtual ~ICommand() = default;

    /// Apply the command for the first time.
    /// Return false if execution failed (e.g., entity deleted between creation and execute).
    virtual bool Execute() = 0;

    /// Revert the command (restore state before Execute).
    virtual void Undo() = 0;

    /// Re-apply command (after Undo was called).
    /// Default implementation calls Execute() again, but subclasses can override
    /// for more efficient replay (e.g., cache results from first Execute).
    virtual void Redo() {
        Execute();
    }

    /// Human-readable label for undo history ("Move entity", "Set property").
    /// Displayed in Edit menu: "Undo: Move entity"
    virtual StringView GetLabel() const { return "Command"; }

    /// Optional: estimated memory footprint (for history size limiting).
    virtual size_t EstimateSize() const { return sizeof(*this); }
};
```

## Concrete Commands

### Transform Command

```cpp
/// Move/Rotate/Scale entity.
class TransformCommand : public ICommand {
public:
    TransformCommand(Entity entity, const Transform& before, const Transform& after)
        : entity_(entity), before_(before), after_(after) {}

    bool Execute() override {
        auto* transform = entity_.Get<TransformComponent>();
        if (!transform) return false;  // Entity deleted

        transform->Set(after_);
        return true;
    }

    void Undo() override {
        auto* transform = entity_.Get<TransformComponent>();
        if (transform) transform->Set(before_);
    }

    StringView GetLabel() const override {
        // Could be smarter: "Move 5.2 units" vs "Rotate 45 degrees"
        return "Transform";
    }

private:
    Entity entity_;
    Transform before_, after_;
};
```

### Property Command

```cpp
/// Set a component property (e.g., color, speed, health).
class PropertyCommand : public ICommand {
public:
    PropertyCommand(Entity entity, StringView component_type, StringView property_name,
                   const Variant& before, const Variant& after)
        : entity_(entity), component_type_(component_type),
          property_name_(property_name), before_(before), after_(after) {}

    bool Execute() override {
        auto* component = entity_.GetComponentByType(component_type_);
        if (!component) return false;
        component->SetProperty(property_name_, after_);
        return true;
    }

    void Undo() override {
        auto* component = entity_.GetComponentByType(component_type_);
        if (component) component->SetProperty(property_name_, before_);
    }

    StringView GetLabel() const override {
        return fmt::format("Set {}.{}", component_type_, property_name_);
    }

private:
    Entity entity_;
    std::string component_type_;
    std::string property_name_;
    Variant before_, after_;
};
```

### Entity Creation/Deletion

```cpp
/// Create a new entity.
class CreateEntityCommand : public ICommand {
public:
    CreateEntityCommand(Scene& scene, StringView name, const nlohmann::json& properties)
        : scene_(scene), entity_name_(name), entity_properties_(properties),
          created_entity_(nullptr) {}

    bool Execute() override {
        created_entity_ = scene_.Create();
        if (!created_entity_) return false;

        // Restore properties
        created_entity_.Get<NameComponent>()->Set(entity_name_);
        for (auto& [key, value] : entity_properties_.items()) {
            // Deserialize and apply properties
        }

        return true;
    }

    void Undo() override {
        if (created_entity_) {
            scene_.Destroy(created_entity_);
        }
    }

    StringView GetLabel() const override {
        return fmt::format("Create {}", entity_name_);
    }

private:
    Scene& scene_;
    std::string entity_name_;
    nlohmann::json entity_properties_;
    Entity created_entity_;
};

/// Delete an entity.
class DeleteEntityCommand : public ICommand {
public:
    DeleteEntityCommand(Entity entity)
        : entity_(entity), entity_state_(SerializeEntity(entity)) {}

    bool Execute() override {
        // First execute deletes the entity (it already exists)
        auto* scene = entity_.GetScene();
        if (scene) {
            scene->Destroy(entity_);
            entity_ = nullptr;
        }
        return true;
    }

    void Undo() override {
        // Reconstruct entity from saved state
        auto* scene = GetScene();  // Somehow need scene reference
        entity_ = DeserializeEntity(entity_state_, *scene);
    }

    StringView GetLabel() const override {
        return fmt::format("Delete entity");
    }

private:
    Entity entity_;
    nlohmann::json entity_state_;

    nlohmann::json SerializeEntity(Entity e) {
        // Serialize all components, properties, children
        return nlohmann::json();  // Simplified
    }

    Entity DeserializeEntity(const nlohmann::json& state, Scene& scene) {
        // Reconstruct entity from JSON
        return nullptr;
    }
};
```

## Composite/Macro Commands

Group multiple commands into a single undo step:

```cpp
/// Composite command — multiple sub-commands undone/redone as one unit.
class CompositeCommand : public ICommand {
public:
    CompositeCommand(std::vector<std::unique_ptr<ICommand>> commands,
                    StringView label = "Multiple Changes")
        : commands_(std::move(commands)), label_(label) {}

    bool Execute() override {
        for (auto& cmd : commands_) {
            if (!cmd->Execute()) {
                return false;  // If any subcommand fails, rollback
            }
        }
        return true;
    }

    void Undo() override {
        // Undo in reverse order
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
            (*it)->Undo();
        }
    }

    void Redo() override {
        for (auto& cmd : commands_) {
            cmd->Redo();
        }
    }

    StringView GetLabel() const override { return label_; }

private:
    std::vector<std::unique_ptr<ICommand>> commands_;
    std::string label_;
};
```

## Undo Stack

```cpp
/// Manage undo/redo history.
class UndoStack {
public:
    /// Execute a command and push to undo stack.
    /// If redo_stack_ was non-empty, it gets cleared
    /// (new action invalidates future history).
    void ExecuteCommand(std::unique_ptr<ICommand> command) {
        if (!command->Execute()) {
            UZ_LOG(Editor, Error, "Command execution failed");
            return;
        }

        undo_stack_.push(std::move(command));
        redo_stack_ = {};  // Clear redo on new action

        NotifyChanged();
    }

    /// Undo last command.
    bool Undo() {
        if (undo_stack_.empty()) {
            UZ_LOG(Editor, Warning, "Nothing to undo");
            return false;
        }

        auto cmd = std::move(undo_stack_.top());
        undo_stack_.pop();

        cmd->Undo();
        redo_stack_.push(std::move(cmd));

        NotifyChanged();
        return true;
    }

    /// Redo last undone command.
    bool Redo() {
        if (redo_stack_.empty()) {
            UZ_LOG(Editor, Warning, "Nothing to redo");
            return false;
        }

        auto cmd = std::move(redo_stack_.top());
        redo_stack_.pop();

        cmd->Redo();
        undo_stack_.push(std::move(cmd));

        NotifyChanged();
        return true;
    }

    /// Get label of next undo action (for Edit menu).
    std::string GetUndoLabel() const {
        if (undo_stack_.empty()) return "Undo";
        return fmt::format("Undo: {}", undo_stack_.top()->GetLabel());
    }

    /// Get label of next redo action.
    std::string GetRedoLabel() const {
        if (redo_stack_.empty()) return "Redo";
        return fmt::format("Redo: {}", redo_stack_.top()->GetLabel());
    }

    /// Clear history (for memory management or on project open).
    void Clear() {
        undo_stack_ = {};
        redo_stack_ = {};
        NotifyChanged();
    }

    /// Check memory usage, warn if excessive.
    size_t EstimateMemoryUsage() const {
        size_t total = 0;
        // Iterate stacks and sum EstimateSize()
        return total;
    }

private:
    std::stack<std::unique_ptr<ICommand>> undo_stack_;
    std::stack<std::unique_ptr<ICommand>> redo_stack_;

    void NotifyChanged() {
        // Broadcast event: UI listens and updates Edit menu buttons
        OnUndoStackChanged.Broadcast(GetUndoLabel(), GetRedoLabel());
    }

    MulticastDelegate<void(std::string undo_label, std::string redo_label)> OnUndoStackChanged;
};
```

## Transaction API

For operations requiring multiple commands to be grouped:

```cpp
/// RAII transaction — destructor auto-commits.
class UndoTransaction {
public:
    UndoTransaction(UndoStack& stack, StringView label = "")
        : stack_(stack), label_(label), commands_() {}

    ~UndoTransaction() {
        if (!commands_.empty()) {
            auto composite = std::make_unique<CompositeCommand>(
                std::move(commands_), label_
            );
            stack_.ExecuteCommand(std::move(composite));
        }
    }

    /// Add command to transaction (does NOT execute yet).
    void Add(std::unique_ptr<ICommand> command) {
        commands_.push_back(std::move(command));
    }

    /// Prevent copying
    UndoTransaction(const UndoTransaction&) = delete;
    UndoTransaction& operator=(const UndoTransaction&) = delete;

private:
    UndoStack& stack_;
    std::string label_;
    std::vector<std::unique_ptr<ICommand>> commands_;
};

// Usage:
{
    UndoTransaction transaction(undo_stack, "Rotate & Scale");

    transaction.Add(std::make_unique<TransformCommand>(entity, before_transform, after_rot));
    transaction.Add(std::make_unique<TransformCommand>(entity, after_rot, after_scale));

    // On scope exit: both commands grouped into single undo step
}
```

## Memory Management

Limit undo history size to prevent excessive memory usage:

```cpp
/// Undo stack with memory limit.
class BoundedUndoStack : public UndoStack {
private:
    static constexpr size_t kMaxMemoryBytes = 100 * 1024 * 1024;  // 100 MB

public:
    void ExecuteCommand(std::unique_ptr<ICommand> command) override {
        UndoStack::ExecuteCommand(std::move(command));

        // Check memory usage
        size_t usage = EstimateMemoryUsage();
        if (usage > kMaxMemoryBytes) {
            UZ_LOG(Editor, Warning, "Undo stack exceeded {} MB, clearing oldest entries",
                kMaxMemoryBytes / 1024 / 1024);
            // Pop oldest entries from bottom of undo_stack_
            DropOldestCommands();
        }
    }

private:
    void DropOldestCommands() {
        // Stack doesn't support random access; need different data structure
        // Could use deque instead of stack
    }
};
```

## TypeScript UI Integration

```typescript
// Listen for undo stack changes and update Edit menu
class EditorUndoUI {
    constructor(private undoStack: UndoStack) {
        this.undoStack.onChanged.subscribe((undoLabel, redoLabel) => {
            this.updateEditMenu(undoLabel, redoLabel);
        });
    }

    private updateEditMenu(undoLabel: string, redoLabel: string) {
        // Update menu items
        this.undoMenuItem.label = undoLabel;
        this.redoMenuItem.label = redoLabel;

        // Enable/disable based on availability
        this.undoMenuItem.enabled = undoLabel !== "Undo";
        this.redoMenuItem.enabled = redoLabel !== "Redo";
    }

    onUndo() {
        this.undoStack.Undo();
    }

    onRedo() {
        this.undoStack.Redo();
    }
}
```

## Common Patterns

### Pattern 1: Modal Edit (Gizmo Drag)

```cpp
// User drags gizmo from start to end
void OnGizmoDragStart() {
    initial_transform_ = entity_.Get<TransformComponent>()->Get();
    is_dragging_ = true;
}

void OnGizmoDragMove(Vec3 new_position) {
    // Update entity in real-time (preview)
    entity_.Get<TransformComponent>()->Set(new_position);
}

void OnGizmoDragEnd(Vec3 final_position) {
    // Create single undo command for entire drag
    auto command = std::make_unique<TransformCommand>(
        entity_,
        initial_transform_,
        final_position
    );
    undo_stack_.ExecuteCommand(std::move(command));
    is_dragging_ = false;
}
```

### Pattern 2: Multi-Entity Edit

```cpp
// Move all selected entities together
{
    UndoTransaction transaction(undo_stack_, "Move 3 entities");

    for (auto entity : selected_entities_) {
        auto old_pos = entity.Get<TransformComponent>()->Get();
        auto new_pos = old_pos;
        new_pos.position += delta;

        transaction.Add(std::make_unique<TransformCommand>(
            entity, old_pos, new_pos
        ));
    }
}
```

### Pattern 3: Error Recovery

```cpp
// If operation fails partway, undo is invalid (partial state)
bool PerformComplexOperation(UndoStack& stack) {
    {
        UndoTransaction transaction(stack, "Complex Op");

        transaction.Add(CreateCommand1());
        transaction.Add(CreateCommand2());

        if (!SomeValidationCheck()) {
            // Exception triggers transaction destructor, which WILL execute
            // -> We get partial undo. Bad!
            // Solution: validate BEFORE adding commands, or use try-catch
            throw std::runtime_error("Validation failed");
        }

        transaction.Add(CreateCommand3());
    }
    return true;
}
```

Better pattern:
```cpp
bool PerformComplexOperation(UndoStack& stack) {
    // Validate before creating transaction
    if (!SomeValidationCheck()) {
        UZ_LOG(Editor, Error, "Validation failed, no undo created");
        return false;
    }

    // Now safe to proceed
    {
        UndoTransaction transaction(stack, "Complex Op");
        transaction.Add(CreateCommand1());
        transaction.Add(CreateCommand2());
        transaction.Add(CreateCommand3());
    }
    return true;
}
```

## Comparison

| Feature | Unreal | Godot | Unity | **UzEngine** |
|---|---|---|---|---|
| **Command pattern** | FTransaction + Modify() | EditorUndoRedo | Undo.RecordObject | **Explicit ICommand** |
| **Grouping** | Transaction (implicit) | action_version_added | Undo groups (implicit) | **Explicit or RAII** |
| **Memory limit** | Unlimited (danger) | Configurable | Configurable | **Configurable** |
| **History UI** | Hidden | Tree view | Linear list | **Linear (extensible)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | ICommand interface, TransformCommand, PropertyCommand, basic UndoStack, Edit menu integration |
| **Phase 3** | CreateEntityCommand, DeleteEntityCommand, transaction API, memory limiting, undo history visualization |
| **Phase 4** | Selective undo (undo specific entity), branching history (visualize redo branches), undo checkpoint system |

