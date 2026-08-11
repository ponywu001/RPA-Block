#include "rpa/recorder/UiaInspector.h"

#include <map>
#include <string>
#include <vector>

#include "rpa/core/TextMatch.h"

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

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

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

namespace {

/// Find a top-level window whose title contains `filter`, or the foreground one
/// when the filter is empty.
HWND windowForDump(const std::string& filter) {
    if (filter.empty()) return GetForegroundWindow();

    struct Search {
        std::string needle;
        HWND found = nullptr;
    } search{filter};

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* state = reinterpret_cast<Search*>(param);
            if (!IsWindowVisible(hwnd)) return TRUE;
            const int length = GetWindowTextLengthW(hwnd);
            if (length <= 0) return TRUE;
            std::wstring title(static_cast<size_t>(length) + 1, L'\0');
            const int copied = GetWindowTextW(hwnd, title.data(), length + 1);
            title.resize(static_cast<size_t>(copied));
            if (wideToUtf8(title).find(state->needle) != std::string::npos) {
                state->found = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));

    return search.found;
}

void describe(IUIAutomationElement* element, int depth, UiaNode& node) {
    node.depth = depth;

    ComPtr<IUIAutomationElement> labeller;
    if (SUCCEEDED(element->get_CurrentLabeledBy(labeller.put())) && labeller) {
        BSTR text = nullptr;
        if (SUCCEEDED(labeller->get_CurrentName(&text))) {
            node.labeledBy = bstrToUtf8(text);
            SysFreeString(text);
        }
    }

    BSTR value = nullptr;
    if (SUCCEEDED(element->get_CurrentName(&value))) {
        node.name = bstrToUtf8(value);
        SysFreeString(value);
    }
    if (SUCCEEDED(element->get_CurrentAutomationId(&value))) {
        node.automationId = bstrToUtf8(value);
        SysFreeString(value);
    }
    if (SUCCEEDED(element->get_CurrentClassName(&value))) {
        node.className = bstrToUtf8(value);
        SysFreeString(value);
    }

    CONTROLTYPEID type = 0;
    if (SUCCEEDED(element->get_CurrentControlType(&type))) {
        node.controlType = controlTypeName(type);
    }

    BOOL flag = FALSE;
    if (SUCCEEDED(element->get_CurrentIsKeyboardFocusable(&flag))) {
        node.keyboardFocusable = flag != FALSE;
    }
    if (SUCCEEDED(element->get_CurrentIsOffscreen(&flag))) {
        node.offscreen = flag != FALSE;
    }

    RECT rect{};
    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rect))) {
        node.bounds = core::Rect{rect.left, rect.top, rect.right - rect.left,
                                 rect.bottom - rect.top};
    }
}

void walk(IUIAutomation* automation,
          IUIAutomationTreeWalker* walker,
          IUIAutomationElement* element,
          int depth,
          int maxDepth,
          int maxNodes,
          std::vector<UiaNode>& out) {
    if (!element || depth > maxDepth || static_cast<int>(out.size()) >= maxNodes) return;

    UiaNode node;
    describe(element, depth, node);
    out.push_back(node);

    ComPtr<IUIAutomationElement> child;
    if (FAILED(walker->GetFirstChildElement(element, child.put())) || !child) return;

    while (child && static_cast<int>(out.size()) < maxNodes) {
        walk(automation, walker, child.get(), depth + 1, maxDepth, maxNodes, out);

        ComPtr<IUIAutomationElement> sibling;
        if (FAILED(walker->GetNextSiblingElement(child.get(), sibling.put())) || !sibling) break;
        child = std::move(sibling);
    }
}

}  // namespace

std::vector<UiaNode> dumpWindowTree(const std::string& titleFilter,
                                    int maxDepth,
                                    int maxNodes,
                                    std::string& error) {
    std::vector<UiaNode> nodes;

    IUIAutomation* automation = threadAutomation();
    if (!automation) {
        error = "UI Automation is unavailable on this system";
        return nodes;
    }

    HWND hwnd = windowForDump(titleFilter);
    if (!hwnd) {
        error = titleFilter.empty() ? "no foreground window"
                                    : "no visible window whose title contains: " + titleFilter;
        return nodes;
    }

    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(hwnd, root.put())) || !root) {
        error = "UI Automation could not describe that window";
        return nodes;
    }

    // The control view rather than the raw view: the raw tree is full of
    // presentational nodes that no automation would ever target, and on a big
    // form that is the difference between a readable dump and thousands of
    // lines of noise.
    ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(automation->get_ControlViewWalker(walker.put())) || !walker) {
        error = "UI Automation has no control-view walker";
        return nodes;
    }

    walk(automation, walker.get(), root.get(), 0, maxDepth, maxNodes, nodes);
    return nodes;
}

namespace {

bool roleAccepts(core::ElementRole role, const std::string& controlType, bool focusable) {
    switch (role) {
        case core::ElementRole::Input:
            // ComboBox counts: to someone filling in a form, a drop-down is
            // another thing you put a value into.
            return controlType == "Edit" || controlType == "ComboBox" ||
                   controlType == "Document";
        case core::ElementRole::Button:
            return controlType == "Button" || controlType == "Hyperlink";
        case core::ElementRole::Checkbox:
            return controlType == "CheckBox" || controlType == "RadioButton";
        case core::ElementRole::Any:
            // Anything a user could interact with, minus the containers that
            // are focusable only because their children are.
            return focusable && controlType != "Pane" && controlType != "Window" &&
                   controlType != "Group";
    }
    return false;
}

int centreX(const core::Rect& r) { return r.x + r.width / 2; }
int centreY(const core::Rect& r) { return r.y + r.height / 2; }

/// Distance from the anchor to a candidate in the wanted direction, or -1 when
/// the candidate is not in that direction at all.
///
/// Requires the two to overlap on the perpendicular axis, which is what stops a
/// field from a different row being picked just because it is nearer in a
/// straight line.
int directedDistance(const core::Rect& anchor, const core::Rect& candidate,
                     core::Direction direction) {
    const bool overlapsVertically =
        candidate.y < anchor.y + anchor.height && anchor.y < candidate.y + candidate.height;
    const bool overlapsHorizontally =
        candidate.x < anchor.x + anchor.width && anchor.x < candidate.x + candidate.width;

    switch (direction) {
        case core::Direction::Right:
            if (!overlapsVertically) return -1;
            if (candidate.x < anchor.x + anchor.width / 2) return -1;
            return candidate.x - (anchor.x + anchor.width);
        case core::Direction::Left:
            if (!overlapsVertically) return -1;
            if (candidate.x + candidate.width > centreX(anchor)) return -1;
            return anchor.x - (candidate.x + candidate.width);
        case core::Direction::Below:
            if (!overlapsHorizontally) return -1;
            if (candidate.y < anchor.y + anchor.height / 2) return -1;
            return candidate.y - (anchor.y + anchor.height);
        case core::Direction::Above:
            if (!overlapsHorizontally) return -1;
            if (candidate.y + candidate.height > centreY(anchor)) return -1;
            return anchor.y - (candidate.y + candidate.height);
    }
    return -1;
}

}  // namespace

UiaMatch findRelativeElement(const std::string& anchorText,
                             core::MatchMode match,
                             core::Direction direction,
                             core::ElementRole role,
                             int maxDistance,
                             const std::string& windowTitle) {
    UiaMatch result;

    std::string error;
    const auto nodes = dumpWindowTree(windowTitle, 40, 4000, error);
    if (!error.empty()) {
        result.diagnosis = error;
        return result;
    }
    if (nodes.empty()) {
        result.diagnosis = "UI Automation returned no controls for the foreground window";
        return result;
    }

    // Strategy 1: the control already carries the label as its own name. A
    // standard Win32 form does this for free, and when it holds it is exact --
    // no coordinates, so nothing about layout, font size or DPI can break it.
    //
    // Only when exactly one control matches. A `contains` anchor can easily hit
    // several ("時區" is inside "變更時區(Z)..."), and this strategy has no
    // notion of direction to tell them apart -- so more than one hit means
    // falling through to the geometry pass, which does. Taking the first in
    // tree order would be picking by an accident of the control hierarchy.
    const UiaNode* named = nullptr;
    int namedCount = 0;
    for (const auto& node : nodes) {
        if (node.offscreen || node.bounds.empty()) continue;
        if (!roleAccepts(role, node.controlType, node.keyboardFocusable)) continue;
        if (!core::textMatches(node.name, anchorText, match)) continue;
        ++namedCount;
        if (!named) named = &node;
    }

    if (namedCount == 1) {
        result.found = true;
        result.bounds = named->bounds;
        result.name = named->name;
        result.controlType = named->controlType;
        result.strategy = UiaMatchStrategy::ByName;
        return result;
    }

    // Strategy 2: find the label itself, then the nearest accepting control in
    // the requested direction.
    int roleCount = 0;
    int anchorCount = 0;
    int best = -1;

    for (const auto& anchor : nodes) {
        if (anchor.offscreen || anchor.bounds.empty()) continue;
        if (!core::textMatches(anchor.name, anchorText, match)) continue;
        ++anchorCount;

        for (const auto& candidate : nodes) {
            if (candidate.offscreen || candidate.bounds.empty()) continue;
            if (!roleAccepts(role, candidate.controlType, candidate.keyboardFocusable)) continue;
            ++roleCount;

            const int distance = directedDistance(anchor.bounds, candidate.bounds, direction);
            if (distance < 0 || distance > maxDistance) continue;
            if (best >= 0 && distance >= best) continue;

            best = distance;
            result.found = true;
            result.bounds = candidate.bounds;
            result.name = candidate.name;
            result.controlType = candidate.controlType;
            result.strategy = UiaMatchStrategy::ByGeometry;
        }
    }

    if (!result.found) {
        // Which of the three ways this can fail decides what the user has to
        // change, so say which one it was rather than "not found".
        if (namedCount > 1) {
            // Both passes declined, and the first one declined because the
            // anchor was not specific enough. Say that, rather than letting it
            // look like the control is missing.
            result.diagnosis = std::to_string(namedCount) + " controls match '" + anchorText +
                               "' and none of them sits " + core::toString(direction) +
                               " of a label with that text -- make the anchor more specific";
        } else if (anchorCount == 0) {
            result.diagnosis = "UI Automation found no element named '" + anchorText + "' in " +
                               std::to_string(nodes.size()) + " controls";
        } else if (roleCount == 0) {
            result.diagnosis = "found the label '" + anchorText +
                               "' but this window exposes no control of that kind to automation";
        } else {
            result.diagnosis = "found the label '" + anchorText + "' but nothing " +
                               core::toString(direction) + " of it within " +
                               std::to_string(maxDistance) + "px";
        }
    }
    return result;
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
std::vector<UiaNode> dumpWindowTree(const std::string&, int, int, std::string& error) {
    error = "UI Automation is only available on Windows";
    return {};
}
UiaMatch findRelativeElement(const std::string&, core::MatchMode, core::Direction,
                             core::ElementRole, int, const std::string&) {
    UiaMatch match;
    match.diagnosis = "UI Automation is only available on Windows";
    return match;
}
bool captureRegionToPng(const std::string&, const core::Rect&, std::string& error) {
    error = "screen capture is only implemented on Windows";
    return false;
}

#endif  // _WIN32

}  // namespace rpa::recorder
