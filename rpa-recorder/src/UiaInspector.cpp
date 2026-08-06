#include "rpa/recorder/UiaInspector.h"

#include <map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <combaseapi.h>
#include <psapi.h>
#include <uiautomation.h>
#include <wincodec.h>
#endif

namespace rpa::recorder {

#ifdef _WIN32

namespace {

/// Minimal COM smart pointer; pulling in a full ATL/WRL dependency for four
/// interface types isn't worth it.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    T** put() { reset(); return &ptr_; }
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

std::string bstrToUtf8(BSTR value) {
    if (!value) return {};
    const int length = static_cast<int>(SysStringLen(value));
    if (length <= 0) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, length, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        utf8.data(), size, nullptr, nullptr);
    return utf8;
}

const char* controlTypeName(CONTROLTYPEID id) {
    static const std::map<CONTROLTYPEID, const char*> kNames = {
        {UIA_ButtonControlTypeId, "Button"},
        {UIA_EditControlTypeId, "Edit"},
        {UIA_TextControlTypeId, "Text"},
        {UIA_CheckBoxControlTypeId, "CheckBox"},
        {UIA_ComboBoxControlTypeId, "ComboBox"},
        {UIA_ListControlTypeId, "List"},
        {UIA_ListItemControlTypeId, "ListItem"},
        {UIA_MenuControlTypeId, "Menu"},
        {UIA_MenuItemControlTypeId, "MenuItem"},
        {UIA_TabControlTypeId, "Tab"},
        {UIA_TabItemControlTypeId, "TabItem"},
        {UIA_TreeControlTypeId, "Tree"},
        {UIA_TreeItemControlTypeId, "TreeItem"},
        {UIA_HyperlinkControlTypeId, "Hyperlink"},
        {UIA_ImageControlTypeId, "Image"},
        {UIA_WindowControlTypeId, "Window"},
        {UIA_PaneControlTypeId, "Pane"},
        {UIA_DocumentControlTypeId, "Document"},
        {UIA_ToolBarControlTypeId, "ToolBar"},
        {UIA_RadioButtonControlTypeId, "RadioButton"},
        {UIA_DataItemControlTypeId, "DataItem"},
        {UIA_GroupControlTypeId, "Group"},
    };
    auto it = kNames.find(id);
    return it == kNames.end() ? "Unknown" : it->second;
}

/// One automation object per thread. Creating it is expensive relative to a
/// single click, and it must not cross apartments.
IUIAutomation* threadAutomation() {
    thread_local IUIAutomation* automation = nullptr;
    thread_local bool attempted = false;

    if (!attempted) {
        attempted = true;
        const HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                            __uuidof(IUIAutomation),
                                            reinterpret_cast<void**>(&automation));
        if (FAILED(hr)) automation = nullptr;
    }
    return automation;
}

std::string processNameForWindow(HWND hwnd) {
    if (!hwnd) return {};
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) return {};

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return {};

    wchar_t path[MAX_PATH] = {};
    DWORD size = static_cast<DWORD>(std::size(path));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok) return {};

    std::wstring full(path, size);
    const size_t slash = full.find_last_of(L"\\/");
    return wideToUtf8(slash == std::wstring::npos ? full : full.substr(slash + 1));
}

std::string windowTitleFor(HWND hwnd) {
    if (!hwnd) return {};
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root) root = hwnd;

    const int length = GetWindowTextLengthW(root);
    if (length <= 0) return {};
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(root, title.data(), length + 1);
    title.resize(static_cast<size_t>(copied));
    return wideToUtf8(title);
}

ElementInfo describe(IUIAutomationElement* element) {
    ElementInfo info;
    if (!element) return info;

    BSTR name = nullptr;
    if (SUCCEEDED(element->get_CurrentName(&name)) && name) {
        info.name = bstrToUtf8(name);
        SysFreeString(name);
    }

    BSTR automationId = nullptr;
    if (SUCCEEDED(element->get_CurrentAutomationId(&automationId)) && automationId) {
        info.automationId = bstrToUtf8(automationId);
        SysFreeString(automationId);
    }

    BSTR className = nullptr;
    if (SUCCEEDED(element->get_CurrentClassName(&className)) && className) {
        info.className = bstrToUtf8(className);
        SysFreeString(className);
    }

    CONTROLTYPEID controlType = 0;
    if (SUCCEEDED(element->get_CurrentControlType(&controlType))) {
        info.controlType = controlTypeName(controlType);
    }

    RECT bounds{};
    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&bounds))) {
        info.bounds = core::Rect{bounds.left, bounds.top, bounds.right - bounds.left,
                                 bounds.bottom - bounds.top};
    }

    UIA_HWND nativeHandle = nullptr;
    if (SUCCEEDED(element->get_CurrentNativeWindowHandle(&nativeHandle)) && nativeHandle) {
        HWND hwnd = static_cast<HWND>(nativeHandle);
        info.windowTitle = windowTitleFor(hwnd);
        info.processName = processNameForWindow(hwnd);
    }

    return info;
}

}  // namespace

void initializeUiaForThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

void uninitializeUiaForThread() {
    CoUninitialize();
}

ElementInfo inspectElementAt(const core::Point& point) {
    ElementInfo info;

    IUIAutomation* automation = threadAutomation();
    if (automation) {
        ComPtr<IUIAutomationElement> element;
        const POINT pt{point.x, point.y};
        if (SUCCEEDED(automation->ElementFromPoint(pt, element.put())) && element) {
            info = describe(element.get());
        }
    }

    // UI Automation may decline (elevated targets, custom-drawn UI). The plain
    // Win32 window under the cursor is still useful context for the AI.
    if (info.windowTitle.empty()) {
        const POINT pt{point.x, point.y};
        HWND hwnd = WindowFromPoint(pt);
        info.windowTitle = windowTitleFor(hwnd);
        if (info.processName.empty()) info.processName = processNameForWindow(hwnd);
    }
    return info;
}

ElementInfo inspectFocusedElement() {
    ElementInfo info;

    IUIAutomation* automation = threadAutomation();
    if (automation) {
        ComPtr<IUIAutomationElement> element;
        if (SUCCEEDED(automation->GetFocusedElement(element.put())) && element) {
            info = describe(element.get());
        }
    }

    if (info.windowTitle.empty()) {
        info.windowTitle = windowTitleFor(GetForegroundWindow());
        if (info.processName.empty()) {
            info.processName = processNameForWindow(GetForegroundWindow());
        }
    }
    return info;
}

core::Rect desktopBounds() {
    core::Rect bounds;
    bounds.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    bounds.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    bounds.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    bounds.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return bounds;
}

bool captureRegionToPng(const std::string& path, const core::Rect& region, std::string& error) {
    const core::Rect bounds = region.empty() ? desktopBounds() : region;
    if (bounds.width <= 0 || bounds.height <= 0) {
        error = "capture region has zero area";
        return false;
    }

    HDC screen = GetDC(nullptr);
    if (!screen) {
        error = "GetDC failed";
        return false;
    }
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, bounds.width, bounds.height);
    HGDIOBJ previous = SelectObject(memory, bitmap);

    const BOOL blitted = BitBlt(memory, 0, 0, bounds.width, bounds.height, screen, bounds.x,
                                bounds.y, SRCCOPY | CAPTUREBLT);

    bool ok = false;
    if (blitted) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = bounds.width;
        info.bmiHeader.biHeight = -bounds.height;  // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        const size_t stride = static_cast<size_t>(bounds.width) * 4;
        std::vector<BYTE> pixels(stride * bounds.height);
        if (GetDIBits(memory, bitmap, 0, static_cast<UINT>(bounds.height), pixels.data(), &info,
                      DIB_RGB_COLORS) != 0) {
            ComPtr<IWICImagingFactory> factory;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(factory.put())))) {
                const std::wstring widePath(path.begin(), path.end());

                ComPtr<IWICStream> stream;
                ComPtr<IWICBitmapEncoder> encoder;
                ComPtr<IWICBitmapFrameEncode> frame;

                if (SUCCEEDED(factory->CreateStream(stream.put())) &&
                    SUCCEEDED(stream->InitializeFromFilename(widePath.c_str(), GENERIC_WRITE)) &&
                    SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                                     encoder.put())) &&
                    SUCCEEDED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) &&
                    SUCCEEDED(encoder->CreateNewFrame(frame.put(), nullptr)) &&
                    SUCCEEDED(frame->Initialize(nullptr))) {
                    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
                    frame->SetSize(static_cast<UINT>(bounds.width),
                                   static_cast<UINT>(bounds.height));
                    frame->SetPixelFormat(&format);
                    if (SUCCEEDED(frame->WritePixels(static_cast<UINT>(bounds.height),
                                                     static_cast<UINT>(stride),
                                                     static_cast<UINT>(pixels.size()),
                                                     pixels.data())) &&
                        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
                        ok = true;
                    }
                }
            }
        }
    }

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    if (!ok) error = "failed to encode screenshot: " + path;
    return ok;
}

#else

void initializeUiaForThread() {}
void uninitializeUiaForThread() {}
ElementInfo inspectElementAt(const core::Point&) { return {}; }
ElementInfo inspectFocusedElement() { return {}; }
core::Rect desktopBounds() { return {}; }
bool captureRegionToPng(const std::string&, const core::Rect&, std::string& error) {
    error = "screen capture is only implemented on Windows";
    return false;
}

#endif  // _WIN32

}  // namespace rpa::recorder
