#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rpa/core/Types.h"

namespace rpa::core {

/// Keyboard and mouse actuation. The Win32 implementation drives `SendInput`;
/// tests substitute a recording fake.
class IInputBackend {
public:
    virtual ~IInputBackend() = default;

    virtual bool moveMouse(Point p, std::string& error) = 0;
    virtual bool click(Point p, MouseButton button, int count, std::string& error) = 0;
    virtual bool typeText(const std::string& utf8, int perCharDelayMs, std::string& error) = 0;

    /// `combo` uses the `ctrl+shift+s` form; modifiers are held for the
    /// duration of the final key.
    virtual bool pressKeys(const std::string& combo, std::string& error) = 0;
};

/// Window lookup and focus.
class IWindowBackend {
public:
    virtual ~IWindowBackend() = default;

    virtual bool activateWindow(const std::string& titleMatch, MatchMode mode, std::string& error) = 0;
    virtual bool foregroundWindowRect(Rect& out, std::string& error) = 0;
};

/// Parse `ctrl+shift+s` into the modifier list plus the terminal key name.
/// Exposed for testing and for the property panel's validation.
bool parseKeyCombo(const std::string& combo,
                   std::vector<std::string>& modifiers,
                   std::string& key);

#ifdef _WIN32
std::unique_ptr<IInputBackend> makeWin32InputBackend();
std::unique_ptr<IWindowBackend> makeWin32WindowBackend();
#endif

}  // namespace rpa::core
