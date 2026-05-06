# Proposal: phase18_slate-ui-window-focus-dnd (Step 9)

## Why

Step 9 do plano-master: "Window/focus/tooltip/drag-and-drop". Última
camada de plumbing antes do VFX editor:
- **Window**: hospedar múltiplas janelas (modal "Save As", popout viewport).
- **FocusManager**: tab cycle, focus claim/release, OnFocusReceived/Lost.
- **Tooltip**: hover delay 500ms → tooltip widget at cursor.
- **DragDrop**: PointerDown sustained move > 4px → DragOperation com
  payload arbitrário; receivers detectam OnDragEnter/Leave/Drop.

VFX editor precisa: drag de Module na Stack para reordenar; drag asset
do Asset Browser para Sprite Renderer slot; tooltip nos botões da
toolbar; modal "Confirmar discard de unsaved changes".

## What Changes

```cpp
namespace uz::ui {

// Window — wraps a HWND + swapchain + root widget.
class Window {
public:
    Window(WindowDesc desc, SP<Widget> root);
    ~Window();
    void Show();
    void Hide();
    bool IsModal() const { return m_modal; }
    SP<Widget> GetRoot() const { return m_root; }
    Vec2 GetSize() const;
    HWND GetHwnd() const { return m_hwnd; }

private:
    HWND       m_hwnd{};
    SP<Widget> m_root;
    bool       m_modal{false};
};

// FocusManager — global focus singleton.
class FocusManager {
public:
    void SetFocus(SP<Widget> w);   // nullptr = none
    SP<Widget> GetFocus() const;
    void CycleNext();   // Tab
    void CyclePrev();   // Shift+Tab
};
inline FocusManager& Focus();

// Tooltip — hover-delay reveal.
class Tooltip {
public:
    static void Register(Widget* owner, std::function<SP<Widget>()> contentFactory, float delaySec = 0.5f);
};

// DragDrop.
struct DragOperation {
    std::any payload;        // user-defined (e.g. AssetRef, ModuleId)
    std::string typeTag;     // "asset.uzmesh" / "vfx.module" etc.
    SP<Widget> ghost;        // visual that follows cursor
};

class DragDropManager {
public:
    void Begin(SP<DragOperation> op);
    void Update(Vec2 cursor);
    void End(Vec2 cursor);  // resolves drop on hovered widget
    bool IsDragging() const;
};
inline DragDropManager& DragDrop();

// Widget hooks for drop.
class Widget {
public:
    virtual EventReply OnDragEnter(const DragOperation& op) { return Unhandled; }
    virtual EventReply OnDragLeave() { return Unhandled; }
    virtual EventReply OnDrop(const DragOperation& op) { return Unhandled; }
};

} // namespace uz::ui
```

Window: usa Win32 HWND + DXGI swapchain (já existe no engine — reusa
SwapchainManager). Cada Window tem seu InputRouter independente.

FocusManager: armazena WP<Widget>; OnFocusReceived/OnFocusLost são
chamados no transition.

Tooltip: Widget Register hook seta callback. App tracking hover-time
via Tick; após delay, instancia ghost widget no TopMost layer at cursor.
Move = re-position; Leave = destroy.

DragDrop: PointerDown → 4px threshold → Begin(op). Update follows
cursor com ghost. End → hit-test alvo → OnDrop. Visual feedback:
sob hover válido (OnDragEnter retorna Handled) cursor muda + halo.

## Impact

- Affected specs: `slate-ui-window-focus-dnd` (NEW).
- Affected code: NEW `runtime/engine/ui/slate_window.{h,cpp}`,
  `slate_focus.{h,cpp}`, `slate_tooltip.{h,cpp}`, `slate_dragdrop.{h,cpp}`.
  Modify Widget base para incluir OnDrag* virtuals.
- Breaking change: NO (todos novos virtuais default Unhandled).
- User benefit: pré-requisito direto do VFX editor. Demo Step 9 mostra
  modal "Save As?" + tooltip nos botões + drag-drop reordenando lista.

## Anti-padrões a evitar

```
NÃO FAÇA:
- Modal sem disable de input no parent — clicks no parent funcionam.
- FocusManager com SP<Widget> (cria ciclo); sempre WP<Widget>.
- Tooltip que persiste após widget destroyed.
- DragDrop com payload void* — usa std::any + typeTag para validação.

FAÇA:
- Modal Window: input do parent é gated em IsModalShowing().
- Tab cycle: FocusManager mantém lista de focusable widgets ordenada
  por tree DFS; CycleNext encontra próximo na lista.
- Tooltip: Register callback retorna SP<Widget> (factory); destroy on
  hover-leave automaticamente.
- DragDrop ghost: sempre no TopMost, follow cursor a 60Hz.
```

## Dependencies

- `phase18_slate-ui-declarative-layer` arquivado.
