/// UzEngine Drag & Drop Implementation (Windows)
///
/// OLE IDropTarget for receiving file drops from Explorer and other apps.
/// Extracts file paths from CF_HDROP via DragQueryFileW, converts to UTF-8.

#include "drag_drop.h"

#ifdef _WIN32

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>

UZ_DEFINE_LOG_CATEGORY(DragDrop, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// EngineDropTarget — IDropTarget COM implementation
// ─────────────────────────────────────────────────────────────────────────────

/// Internal COM object implementing IDropTarget for the engine window.
///
/// Ref-counted via AddRef/Release. Created by EnableDragDrop, destroyed
/// when RevokeDragDrop releases the last reference.
class EngineDropTarget final : public IDropTarget {
public:
    EngineDropTarget(HWND hwnd, FileDropCallback onDrop, void* userData)
        : m_hwnd(hwnd)
        , m_onDrop(onDrop)
        , m_userData(userData) {}

    // ── IUnknown ─────────────────────────────────────────────────────────

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) {
            delete this;
        }
        return static_cast<ULONG>(count);
    }

    // ── IDropTarget ──────────────────────────────────────────────────────

    HRESULT STDMETHODCALLTYPE DragEnter(
            IDataObject* pDataObj, DWORD /*grfKeyState*/,
            POINTL /*pt*/, DWORD* pdwEffect) override {
        // Check if the drag contains files (CF_HDROP)
        FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        m_hasFiles = (pDataObj->QueryGetData(&fmt) == S_OK);
        *pdwEffect = m_hasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(
            DWORD /*grfKeyState*/, POINTL pt, DWORD* pdwEffect) override {
        *pdwEffect = m_hasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;

        if (m_onHover && m_hasFiles) {
            // Convert screen coords to client coords
            POINT clientPt = {pt.x, pt.y};
            ScreenToClient(m_hwnd, &clientPt);
            m_onHover(static_cast<float>(clientPt.x),
                      static_cast<float>(clientPt.y),
                      m_hoverUserData);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        m_hasFiles = false;
        if (m_onLeave) {
            m_onLeave(m_leaveUserData);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(
            IDataObject* pDataObj, DWORD /*grfKeyState*/,
            POINTL pt, DWORD* pdwEffect) override {
        *pdwEffect = DROPEFFECT_NONE;

        FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM stg = {};

        if (FAILED(pDataObj->GetData(&fmt, &stg))) {
            UZ_LOG(DragDrop, Warning, "Drop: GetData(CF_HDROP) failed");
            return S_OK;
        }

        auto* hDrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
        if (!hDrop) {
            ReleaseStgMedium(&stg);
            return S_OK;
        }

        DropData dropData;

        // Convert screen → client coordinates
        POINT clientPt = {pt.x, pt.y};
        ScreenToClient(m_hwnd, &clientPt);
        dropData.mouseX = static_cast<float>(clientPt.x);
        dropData.mouseY = static_cast<float>(clientPt.y);

        const UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        dropData.filePaths.reserve(fileCount);

        for (UINT i = 0; i < fileCount; ++i) {
            const UINT charCount = DragQueryFileW(hDrop, i, nullptr, 0);
            if (charCount == 0) continue;

            // +1 for null terminator
            std::vector<wchar_t> widePath(static_cast<size_t>(charCount) + 1);
            DragQueryFileW(hDrop, i, widePath.data(), charCount + 1);

            // Wide → UTF-8
            const int utf8Len = WideCharToMultiByte(
                CP_UTF8, 0, widePath.data(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Len > 1) {
                std::string path(static_cast<size_t>(utf8Len - 1), '\0');
                WideCharToMultiByte(
                    CP_UTF8, 0, widePath.data(), -1,
                    path.data(), utf8Len, nullptr, nullptr);
                dropData.filePaths.push_back(std::move(path));
            }
        }

        GlobalUnlock(stg.hGlobal);
        ReleaseStgMedium(&stg);

        if (!dropData.filePaths.empty() && m_onDrop) {
            UZ_LOG(DragDrop, Info, "Drop: {} file(s) at ({:.0f}, {:.0f})",
                   dropData.filePaths.size(), dropData.mouseX, dropData.mouseY);
            m_onDrop(dropData, m_userData);
        }

        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    // ── Hover/Leave callback setters ─────────────────────────────────────

    void SetHoverCallbacks(DragHoverCallback onHover, DragLeaveCallback onLeave, void* userData) {
        m_onHover = onHover;
        m_onLeave = onLeave;
        m_hoverUserData = userData;
        m_leaveUserData = userData;
    }

private:
    LONG m_refCount = 1;
    HWND m_hwnd = nullptr;
    bool m_hasFiles = false;

    FileDropCallback m_onDrop = nullptr;
    void* m_userData = nullptr;

    DragHoverCallback m_onHover = nullptr;
    void* m_hoverUserData = nullptr;

    DragLeaveCallback m_onLeave = nullptr;
    void* m_leaveUserData = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Module State
// ─────────────────────────────────────────────────────────────────────────────

/// Single active drop target. Only one window is supported at a time.
/// This matches UzEngine's single-window architecture.
static EngineDropTarget* s_dropTarget = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool EnableDragDrop(void* hwnd, FileDropCallback onDrop, void* userData) {
    if (!hwnd || !onDrop) return false;

    auto* hWnd = static_cast<HWND>(hwnd);

    // Initialize OLE (required for RegisterDragDrop).
    // S_OK = first init, S_FALSE = already initialized — both are success.
    const HRESULT oleHr = OleInitialize(nullptr);
    if (FAILED(oleHr)) {
        UZ_LOG(DragDrop, Error, "OleInitialize failed: hr=0x{:08X}", static_cast<unsigned>(oleHr));
        return false;
    }

    // Clean up any previous target
    if (s_dropTarget) {
        RevokeDragDrop(hWnd);
        s_dropTarget->Release();
        s_dropTarget = nullptr;
    }

    s_dropTarget = new EngineDropTarget(hWnd, onDrop, userData);

    const HRESULT hr = RegisterDragDrop(hWnd, s_dropTarget);
    if (FAILED(hr)) {
        UZ_LOG(DragDrop, Error, "RegisterDragDrop failed: hr=0x{:08X}", static_cast<unsigned>(hr));
        s_dropTarget->Release();
        s_dropTarget = nullptr;
        return false;
    }

    UZ_LOG(DragDrop, Info, "Drag-drop enabled for window");
    return true;
}

void SetDragHoverCallbacks(DragHoverCallback onHover, DragLeaveCallback onLeave, void* userData) {
    if (s_dropTarget) {
        s_dropTarget->SetHoverCallbacks(onHover, onLeave, userData);
    }
}

void DisableDragDrop(void* hwnd) {
    if (!hwnd) return;

    auto* hWnd = static_cast<HWND>(hwnd);
    RevokeDragDrop(hWnd);

    if (s_dropTarget) {
        s_dropTarget->Release();
        s_dropTarget = nullptr;
    }

    UZ_LOG(DragDrop, Info, "Drag-drop disabled");
}

} // namespace uz

#endif // _WIN32
