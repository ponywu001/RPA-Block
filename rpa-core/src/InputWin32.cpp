#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <regex>
#include <sstream>

#include "rpa/core/Input.h"
#include "rpa/core/TextMatch.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <thread>
#endif

namespace rpa::core {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

bool parseKeyCombo(const std::string& combo,
                   std::vector<std::string>& modifiers,
                   std::string& key) {
    modifiers.clear();
    key.clear();

    std::vector<std::string> parts;
    std::istringstream stream(combo);
    std::string token;
    while (std::getline(stream, token, '+')) {
        token = toLower(trim(token));
        if (!token.empty()) parts.push_back(token);
    }
    if (parts.empty()) return false;

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string& m = parts[i];
        if (m != "ctrl" && m != "control" && m != "alt" && m != "shift" && m != "win") {
            return false;
        }
        modifiers.push_back(m == "control" ? "ctrl" : m);
    }
    key = parts.back();
    return true;
}

#ifdef _WIN32

namespace {

WORD virtualKeyFor(const std::string& name) {
    // The VK_* macros are ints; the explicit WORD cast keeps the pair
    // construction from tripping a narrowing warning on every entry.
    static const std::map<std::string, WORD> kNamed = {
        {"enter", WORD(VK_RETURN)},   {"return", WORD(VK_RETURN)}, {"tab", WORD(VK_TAB)},
        {"esc", WORD(VK_ESCAPE)},     {"escape", WORD(VK_ESCAPE)}, {"space", WORD(VK_SPACE)},
        {"backspace", WORD(VK_BACK)}, {"delete", WORD(VK_DELETE)}, {"del", WORD(VK_DELETE)},
        {"insert", WORD(VK_INSERT)},  {"home", WORD(VK_HOME)},     {"end", WORD(VK_END)},
        {"pageup", WORD(VK_PRIOR)},   {"pagedown", WORD(VK_NEXT)}, {"up", WORD(VK_UP)},
        {"down", WORD(VK_DOWN)},      {"left", WORD(VK_LEFT)},     {"right", WORD(VK_RIGHT)},
        {"f1", WORD(VK_F1)},   {"f2", WORD(VK_F2)},   {"f3", WORD(VK_F3)},   {"f4", WORD(VK_F4)},
        {"f5", WORD(VK_F5)},   {"f6", WORD(VK_F6)},   {"f7", WORD(VK_F7)},   {"f8", WORD(VK_F8)},
        {"f9", WORD(VK_F9)},   {"f10", WORD(VK_F10)}, {"f11", WORD(VK_F11)}, {"f12", WORD(VK_F12)},
        {"ctrl", WORD(VK_CONTROL)},   {"alt", WORD(VK_MENU)},
        {"shift", WORD(VK_SHIFT)},    {"win", WORD(VK_LWIN)},
    };

    auto it = kNamed.find(name);
    if (it != kNamed.end()) return it->second;

    if (name.size() == 1) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<WORD>(c);
        }
        const SHORT scan = VkKeyScanA(name[0]);
        if (scan != -1) return static_cast<WORD>(scan & 0xFF);
    }
    return 0;
}

void sendVirtualKey(WORD vk, bool keyUp) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof(INPUT));
}

/// Send a UTF-16 code unit as a literal character, bypassing the keyboard
/// layout. This is what makes Chinese text entry work regardless of the
/// active IME.
void sendUnicodeUnit(wchar_t unit) {
    INPUT input[2] = {};
    for (int i = 0; i < 2; ++i) {
        input[i].type = INPUT_KEYBOARD;
        input[i].ki.wScan = unit;
        input[i].ki.dwFlags = KEYEVENTF_UNICODE | (i == 1 ? KEYEVENTF_KEYUP : 0);
    }
    SendInput(2, input, sizeof(INPUT));
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        wide.data(), size);
    return wide;
}

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                         static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                        utf8.data(), size, nullptr, nullptr);
    return utf8;
}

class Win32InputBackend : public IInputBackend {
public:
    bool moveMouse(Point p, std::string& error) override {
        if (!SetCursorPos(p.x, p.y)) {
            error = "SetCursorPos failed (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        return true;
    }

    bool click(Point p, MouseButton button, int count, std::string& error) override {
        if (!moveMouse(p, error)) return false;

        // Give the target window a moment to process the move before the press;
        // some controls only arm their hit-testing on WM_MOUSEMOVE.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        DWORD down = MOUSEEVENTF_LEFTDOWN;
        DWORD up = MOUSEEVENTF_LEFTUP;
        if (button == MouseButton::Right) {
            down = MOUSEEVENTF_RIGHTDOWN;
            up = MOUSEEVENTF_RIGHTUP;
        } else if (button == MouseButton::Middle) {
            down = MOUSEEVENTF_MIDDLEDOWN;
            up = MOUSEEVENTF_MIDDLEUP;
        }

        for (int i = 0; i < std::max(1, count); ++i) {
            INPUT input[2] = {};
            input[0].type = INPUT_MOUSE;
            input[0].mi.dwFlags = down;
            input[1].type = INPUT_MOUSE;
            input[1].mi.dwFlags = up;
            if (SendInput(2, input, sizeof(INPUT)) != 2) {
                error = "SendInput failed (error " + std::to_string(GetLastError()) + ")";
                return false;
            }
            if (i + 1 < count) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(GetDoubleClickTime() / 3));
            }
        }
        return true;
    }

    bool typeText(const std::string& utf8, int perCharDelayMs, std::string& error) override {
        const std::wstring wide = utf8ToWide(utf8);
        if (wide.empty() && !utf8.empty()) {
            error = "text is not valid UTF-8";
            return false;
        }
        for (wchar_t unit : wide) {
            if (unit == L'\n') {
                sendVirtualKey(VK_RETURN, false);
                sendVirtualKey(VK_RETURN, true);
            } else if (unit == L'\t') {
                sendVirtualKey(VK_TAB, false);
                sendVirtualKey(VK_TAB, true);
            } else if (unit != L'\r') {
                sendUnicodeUnit(unit);
            }
            if (perCharDelayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(perCharDelayMs));
            }
        }
        return true;
    }

    bool pressKeys(const std::string& combo, std::string& error) override {
        std::vector<std::string> modifiers;
        std::string key;
        if (!parseKeyCombo(combo, modifiers, key)) {
            error = "cannot parse key combo: " + combo;
            return false;
        }

        std::vector<WORD> modifierKeys;
        for (const auto& m : modifiers) {
            const WORD vk = virtualKeyFor(m);
            if (vk == 0) {
                error = "unknown modifier: " + m;
                return false;
            }
            modifierKeys.push_back(vk);
        }

        const WORD keyVk = virtualKeyFor(key);
        if (keyVk == 0) {
            error = "unknown key: " + key;
            return false;
        }

        for (WORD vk : modifierKeys) sendVirtualKey(vk, false);
        sendVirtualKey(keyVk, false);
        sendVirtualKey(keyVk, true);
        for (auto it = modifierKeys.rbegin(); it != modifierKeys.rend(); ++it) {
            sendVirtualKey(*it, true);
        }
        return true;
    }
};

struct WindowSearch {
    std::string needle;
    MatchMode mode;
    HWND found = nullptr;
};


BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);

    if (!IsWindowVisible(hwnd)) return TRUE;
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return TRUE;

    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, title.data(), length + 1);
    title.resize(static_cast<size_t>(copied));

    if (textMatches(wideToUtf8(title), search->needle, search->mode)) {
        search->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

class Win32WindowBackend : public IWindowBackend {
public:
    bool activateWindow(const std::string& titleMatch, MatchMode mode, std::string& error) override {
        WindowSearch search;
        search.needle = titleMatch;
        search.mode = mode;
        EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&search));

        if (!search.found) {
            error = "no visible window matching: " + titleMatch;
            return false;
        }

        if (IsIconic(search.found)) ShowWindow(search.found, SW_RESTORE);

        // SetForegroundWindow is refused unless the calling thread shares an
        // input queue with the target; attaching first is the standard way to
        // satisfy that rule.
        const DWORD targetThread = GetWindowThreadProcessId(search.found, nullptr);
        const DWORD currentThread = GetCurrentThreadId();
        const bool attached = targetThread != currentThread &&
                              AttachThreadInput(currentThread, targetThread, TRUE);

        const BOOL ok = SetForegroundWindow(search.found);

        if (attached) AttachThreadInput(currentThread, targetThread, FALSE);

        if (!ok) {
            error = "SetForegroundWindow refused for: " + titleMatch;
            return false;
        }
        return true;
    }

    bool foregroundWindowRect(Rect& out, std::string& error) override {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) {
            error = "no foreground window";
            return false;
        }
        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) {
            error = "GetWindowRect failed";
            return false;
        }
        out.x = rect.left;
        out.y = rect.top;
        out.width = rect.right - rect.left;
        out.height = rect.bottom - rect.top;
        return true;
    }
};

}  // namespace

std::unique_ptr<IInputBackend> makeWin32InputBackend() {
    return std::make_unique<Win32InputBackend>();
}

std::unique_ptr<IWindowBackend> makeWin32WindowBackend() {
    return std::make_unique<Win32WindowBackend>();
}

#endif  // _WIN32

}  // namespace rpa::core
