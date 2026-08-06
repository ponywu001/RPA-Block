#include "rpa/recorder/Recorder.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "rpa/recorder/UiaInspector.h"

namespace rpa::recorder {

namespace {
namespace fs = std::filesystem;

/// Raw event handed from the hook callback to the worker. Deliberately trivial
/// so the hook procedure stays inside its time budget.
struct RawEvent {
    enum class Kind { MouseDown, KeyDown } kind = Kind::MouseDown;
    int64_t timestampMs = 0;
    core::Point position;
    core::MouseButton button = core::MouseButton::Left;
    DWORD virtualKey = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;
};

std::string virtualKeyName(DWORD vk) {
    switch (vk) {
        case VK_RETURN: return "enter";
        case VK_TAB: return "tab";
        case VK_ESCAPE: return "escape";
        case VK_BACK: return "backspace";
        case VK_DELETE: return "delete";
        case VK_INSERT: return "insert";
        case VK_HOME: return "home";
        case VK_END: return "end";
        case VK_PRIOR: return "pageup";
        case VK_NEXT: return "pagedown";
        case VK_UP: return "up";
        case VK_DOWN: return "down";
        case VK_LEFT: return "left";
        case VK_RIGHT: return "right";
        case VK_SPACE: return "space";
        default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F12) return "f" + std::to_string(vk - VK_F1 + 1);
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::string(1, static_cast<char>(std::tolower(static_cast<int>(vk))));
    }
    return "vk" + std::to_string(vk);
}

/// Translate a key press to the character it would produce, honouring the
/// active layout and modifier state. Returns empty for non-printing keys.
std::string printableCharacter(DWORD vk, bool shift) {
    BYTE keyboard[256] = {};
    if (shift) keyboard[VK_SHIFT] = 0x80;
    if (GetKeyState(VK_CAPITAL) & 1) keyboard[VK_CAPITAL] = 0x01;

    const UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t buffer[8] = {};
    const int produced = ToUnicode(vk, scanCode, keyboard, buffer,
                                   static_cast<int>(std::size(buffer)), 0);
    if (produced <= 0) return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, buffer, produced, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, produced, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

}  // namespace

std::string toString(RecordedEventType type) {
    switch (type) {
        case RecordedEventType::MouseClick: return "click";
        case RecordedEventType::MouseDoubleClick: return "double_click";
        case RecordedEventType::TextInput: return "text";
        case RecordedEventType::KeyCombo: return "keys";
        case RecordedEventType::WindowChange: return "window_change";
    }
    return "click";
}

struct Recorder::Impl {
    RecorderConfig config;
    std::function<void(const RecordedEvent&)> callback;

    std::atomic<bool> recording{false};
    std::atomic<bool> paused{false};
    /// Asks the worker to emit any half-typed text run immediately, rather than
    /// waiting out the coalescing window.
    std::atomic<bool> flushRequested{false};
    std::atomic<DWORD> hookThreadId{0};

    std::thread hookThread;
    std::thread workerThread;

    std::mutex queueMutex;
    std::condition_variable queueSignal;
    std::deque<RawEvent> queue;

    mutable std::mutex sessionMutex;
    RecordingSession session;

    std::chrono::steady_clock::time_point startedAt;

    // Pending text coalescing state, owned by the worker thread.
    std::string pendingText;
    int64_t pendingTextStartMs = 0;
    int64_t pendingTextLastMs = 0;
    ElementInfo pendingTextElement;
    std::string lastWindowTitle;
    int fileCounter = 0;

    static Impl* instance;

    int64_t elapsedMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - startedAt)
            .count();
    }

    void push(RawEvent event) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            queue.push_back(event);
        }
        queueSignal.notify_one();
    }

    /// Hook procedures live on Impl so they can reach `instance`; Windows
    /// requires plain function pointers, hence the static members.
    static LRESULT CALLBACK mouseHookProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam);

    void hookLoop();
    void workerLoop();

    void flushPendingText();
    void emit(RecordedEvent event);
    void handleMouse(const RawEvent& raw);
    void handleKey(const RawEvent& raw);
    std::string captureAround(const core::Point& point, bool fullScreen);
};

Recorder::Impl* Recorder::Impl::instance = nullptr;

LRESULT CALLBACK Recorder::Impl::mouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    auto* impl = Recorder::Impl::instance;
    if (code == HC_ACTION && impl && impl->recording.load() && !impl->paused.load()) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

        RawEvent event;
        event.kind = RawEvent::Kind::MouseDown;
        event.timestampMs = impl->elapsedMs();
        event.position = core::Point{info->pt.x, info->pt.y};

        bool interesting = true;
        switch (wParam) {
            case WM_LBUTTONDOWN: event.button = core::MouseButton::Left; break;
            case WM_RBUTTONDOWN: event.button = core::MouseButton::Right; break;
            case WM_MBUTTONDOWN: event.button = core::MouseButton::Middle; break;
            default: interesting = false; break;
        }
        if (interesting) impl->push(event);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK Recorder::Impl::keyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
    auto* impl = Recorder::Impl::instance;
    if (code == HC_ACTION && impl && impl->recording.load() && !impl->paused.load() &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);

        RawEvent event;
        event.kind = RawEvent::Kind::KeyDown;
        event.timestampMs = impl->elapsedMs();
        event.virtualKey = info->vkCode;
        event.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        event.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        event.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        event.win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                    (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

        // Modifier presses on their own are noise; they arrive again as flags
        // on the key they modify.
        const DWORD vk = info->vkCode;
        const bool isModifier = vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                                vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                                vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                                vk == VK_LWIN || vk == VK_RWIN;
        if (!isModifier) impl->push(event);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void Recorder::Impl::hookLoop() {
    hookThreadId.store(GetCurrentThreadId());

    HHOOK mouseHook =
        SetWindowsHookExW(WH_MOUSE_LL, &Impl::mouseHookProc, GetModuleHandleW(nullptr), 0);
    HHOOK keyboardHook =
        SetWindowsHookExW(WH_KEYBOARD_LL, &Impl::keyboardHookProc, GetModuleHandleW(nullptr), 0);

    if (mouseHook && keyboardHook) {
        // Low-level hooks are only dispatched to a thread that pumps messages,
        // which is why this loop exists even though it handles nothing itself.
        MSG message;
        while (recording.load() && GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (mouseHook) UnhookWindowsHookEx(mouseHook);
    if (keyboardHook) UnhookWindowsHookEx(keyboardHook);
    hookThreadId.store(0);
}

std::string Recorder::Impl::captureAround(const core::Point& point, bool fullScreen) {
    if (!config.captureClickScreenshots || config.assetDirectory.empty()) return {};

    std::error_code ec;
    fs::create_directories(config.assetDirectory, ec);

    const std::string name = (fullScreen ? "frame_" : "click_") +
                             std::to_string(++fileCounter) + ".png";
    const std::string path = (fs::path(config.assetDirectory) / name).string();

    core::Rect region;
    if (!fullScreen) {
        const int radius = config.clickCaptureRadius;
        region = core::Rect{point.x - radius, point.y - radius, radius * 2, radius * 2};

        const core::Rect desktop = session.desktopBounds;
        region.x = std::max(region.x, desktop.x);
        region.y = std::max(region.y, desktop.y);
        const int maxRight = desktop.x + desktop.width;
        const int maxBottom = desktop.y + desktop.height;
        region.width = std::min(region.width, maxRight - region.x);
        region.height = std::min(region.height, maxBottom - region.y);
        if (region.width <= 0 || region.height <= 0) return {};
    }

    std::string error;
    if (!captureRegionToPng(path, fullScreen ? core::Rect{} : region, error)) return {};
    return path;
}

void Recorder::Impl::emit(RecordedEvent event) {
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        session.events.push_back(event);
    }
    if (callback) callback(event);
}

void Recorder::Impl::flushPendingText() {
    if (pendingText.empty()) return;

    RecordedEvent event;
    event.type = RecordedEventType::TextInput;
    event.timestampMs = pendingTextStartMs;
    event.text = pendingText;
    event.element = pendingTextElement;
    emit(std::move(event));

    pendingText.clear();
    pendingTextElement = ElementInfo{};
}

void Recorder::Impl::handleMouse(const RawEvent& raw) {
    flushPendingText();

    RecordedEvent event;
    event.type = RecordedEventType::MouseClick;
    event.timestampMs = raw.timestampMs;
    event.position = raw.position;
    event.button = raw.button;

    if (config.captureElementInfo) {
        event.element = inspectElementAt(raw.position);
    }

    // A window switch is worth a full frame so the AI can see the new context.
    const bool windowChanged =
        !event.element.windowTitle.empty() && event.element.windowTitle != lastWindowTitle;
    if (windowChanged) {
        lastWindowTitle = event.element.windowTitle;
        event.fullScreenshotPath = captureAround(raw.position, true);
    }

    event.screenshotPath = captureAround(raw.position, false);

    // Merge a second click at (nearly) the same spot into a double click,
    // matching the system's own double-click window.
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        if (!session.events.empty()) {
            RecordedEvent& previous = session.events.back();
            const bool sameSpot = std::abs(previous.position.x - event.position.x) <= 4 &&
                                  std::abs(previous.position.y - event.position.y) <= 4;
            const bool quick = event.timestampMs - previous.timestampMs <
                               static_cast<int64_t>(GetDoubleClickTime());
            if (previous.type == RecordedEventType::MouseClick && sameSpot && quick &&
                previous.button == event.button) {
                previous.type = RecordedEventType::MouseDoubleClick;
                return;
            }
        }
    }

    emit(std::move(event));
}

void Recorder::Impl::handleKey(const RawEvent& raw) {
    const bool hasNonShiftModifier = raw.ctrl || raw.alt || raw.win;
    const std::string character =
        hasNonShiftModifier ? std::string{} : printableCharacter(raw.virtualKey, raw.shift);

    if (!character.empty()) {
        // Break the run when the user pauses, so separate fields don't collapse
        // into one giant string.
        if (!pendingText.empty() &&
            raw.timestampMs - pendingTextLastMs > config.textCoalesceMs) {
            flushPendingText();
        }
        if (pendingText.empty()) {
            pendingTextStartMs = raw.timestampMs;
            if (config.captureElementInfo) {
                pendingTextElement = inspectFocusedElement();
            }
        }
        pendingText += character;
        pendingTextLastMs = raw.timestampMs;
        return;
    }

    flushPendingText();

    std::string combo;
    if (raw.ctrl) combo += "ctrl+";
    if (raw.alt) combo += "alt+";
    if (raw.shift) combo += "shift+";
    if (raw.win) combo += "win+";
    combo += virtualKeyName(raw.virtualKey);

    RecordedEvent event;
    event.type = RecordedEventType::KeyCombo;
    event.timestampMs = raw.timestampMs;
    event.keys = combo;
    if (config.captureElementInfo) event.element = inspectFocusedElement();
    emit(std::move(event));
}

void Recorder::Impl::workerLoop() {
    initializeUiaForThread();

    while (true) {
        RawEvent raw;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueSignal.wait_for(lock, std::chrono::milliseconds(200), [this] {
                return !queue.empty() || !recording.load();
            });

            if (queue.empty()) {
                if (!recording.load()) break;
                lock.unlock();

                // Idle tick: close out a text run the user has stopped typing,
                // or that a pause asked us to finalise straight away.
                const bool onRequest = flushRequested.exchange(false);
                const bool wentQuiet =
                    !pendingText.empty() &&
                    elapsedMs() - pendingTextLastMs > config.textCoalesceMs;
                if (onRequest || wentQuiet) flushPendingText();
                continue;
            }

            raw = queue.front();
            queue.pop_front();
        }

        if (raw.kind == RawEvent::Kind::MouseDown) {
            handleMouse(raw);
        } else {
            handleKey(raw);
        }
    }

    flushPendingText();
    uninitializeUiaForThread();
}

Recorder::Recorder() : impl_(std::make_unique<Impl>()) {}

Recorder::~Recorder() {
    stop();
}

void Recorder::setEventCallback(std::function<void(const RecordedEvent&)> callback) {
    impl_->callback = std::move(callback);
}

bool Recorder::isRecording() const {
    return impl_->recording.load();
}

void Recorder::setPaused(bool paused) {
    const bool wasPaused = impl_->paused.exchange(paused);

    // Entering a pause finalises any half-typed run, so resuming cannot glue
    // text from either side of the pause into a single event.
    if (paused && !wasPaused) {
        impl_->flushRequested.store(true);
        impl_->queueSignal.notify_all();
    }
}

bool Recorder::isPaused() const {
    return impl_->paused.load();
}

RecordingSession Recorder::session() const {
    std::lock_guard<std::mutex> lock(impl_->sessionMutex);
    return impl_->session;
}

void Recorder::clear() {
    std::lock_guard<std::mutex> lock(impl_->sessionMutex);
    impl_->session.events.clear();
}

bool Recorder::start(const RecorderConfig& config, std::string& error) {
    if (impl_->recording.load()) {
        error = "already recording";
        return false;
    }
    if (Impl::instance != nullptr) {
        error = "another Recorder instance already owns the input hooks";
        return false;
    }

    impl_->config = config;
    impl_->startedAt = std::chrono::steady_clock::now();
    impl_->fileCounter = 0;
    impl_->pendingText.clear();
    impl_->lastWindowTitle.clear();
    impl_->paused.store(false);
    impl_->flushRequested.store(false);

    {
        std::lock_guard<std::mutex> lock(impl_->sessionMutex);
        impl_->session = RecordingSession{};
        impl_->session.assetDirectory = config.assetDirectory;
        impl_->session.desktopBounds = desktopBounds();
    }

    Impl::instance = impl_.get();
    impl_->recording.store(true);

    impl_->hookThread = std::thread([this] { impl_->hookLoop(); });
    impl_->workerThread = std::thread([this] { impl_->workerLoop(); });

    // Give the hook thread a moment to install; a failure there leaves
    // hookThreadId at zero.
    for (int i = 0; i < 50 && impl_->hookThreadId.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (impl_->hookThreadId.load() == 0) {
        error = "failed to install low-level input hooks";
        stop();
        return false;
    }

    return true;
}

void Recorder::stop() {
    if (!impl_->recording.exchange(false)) {
        if (impl_->hookThread.joinable()) impl_->hookThread.join();
        if (impl_->workerThread.joinable()) impl_->workerThread.join();
        return;
    }

    // Wake the hook thread's blocking GetMessage so it can unhook and exit.
    const DWORD threadId = impl_->hookThreadId.load();
    if (threadId != 0) PostThreadMessageW(threadId, WM_QUIT, 0, 0);

    impl_->queueSignal.notify_all();

    if (impl_->hookThread.joinable()) impl_->hookThread.join();
    if (impl_->workerThread.joinable()) impl_->workerThread.join();

    Impl::instance = nullptr;
}

}  // namespace rpa::recorder
